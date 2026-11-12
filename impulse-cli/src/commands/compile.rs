use crate::parquet_reader::{
    build_padded_validity_bitmap, read_parquet_column_data, read_parquet_id_strings,
    read_text_column_data, NativeColumn,
};
use impulse_graph::spec::{
    KeyType, IMP_REL_CARDINALITY_1_1, IMP_REL_CARDINALITY_1_M, IMP_REL_CARDINALITY_M_1,
    IMP_REL_CARDINALITY_M_N,
};
use impulse_graph::SnapshotWriter;
use serde::Deserialize;
use std::collections::HashMap;
use std::error::Error;
use std::fs::{self, File};
use std::io::{self, BufRead, BufReader, BufWriter, Write};
use std::path::Path;

#[derive(Debug, Deserialize)]
struct Manifest {
    #[allow(dead_code)]
    version: Option<String>,
    domains: Vec<DomainDef>,
    relations: Vec<RelationDef>,
    embeddings: Option<Vec<String>>,
    vector_indices: Option<Vec<String>>,
    metadata: Option<HashMap<String, String>>,
}

#[derive(Debug, Deserialize)]
struct AttributeDef {
    name: String,
    column: String,
    #[serde(rename = "type")]
    type_name: Option<String>,
    dimension: Option<u32>,
}

#[derive(Debug, Deserialize)]
struct DomainDef {
    id: u16,
    name: String,
    key_type: String,
    file: Option<String>,
    format: Option<String>,
    id_column: Option<String>,
    key_attribute: Option<String>,
    attributes: Option<Vec<AttributeDef>>,
}

#[derive(Debug, Deserialize)]
struct RelationDef {
    name: Option<String>,
    src_domain: u16,
    tgt_domain: u16,
    encoding: Option<String>,
    cardinality: Option<String>,
    node_id_width: Option<u8>,
    edge_index_width: Option<u8>,
    virtual_components: Option<Vec<String>>,
    file: Option<String>,
    format: Option<String>,
    src_column: Option<String>,
    tgt_column: Option<String>,
    pub include_csc: Option<bool>,
    attributes: Option<Vec<AttributeDef>>,
}

fn parse_key_type(s: &str) -> KeyType {
    match s.trim().to_lowercase().as_str() {
        "int8" | "i8" => KeyType::Int8,
        "int16" | "i16" => KeyType::Int16,
        "int32" | "i32" | "int" | "integer" => KeyType::Int32,
        "int64" | "i64" | "long" => KeyType::Int64,
        "uint8" | "u8" => KeyType::Uint8,
        "uint16" | "u16" => KeyType::Uint16,
        "uint32" | "u32" => KeyType::Uint32,
        "uint64" | "u64" => KeyType::Uint64,
        "float32" | "f32" | "float" => KeyType::Float32,
        "float64" | "f64" | "double" => KeyType::Float64,
        "string" | "str" | "utf8" | "text" | "var_string" => KeyType::String,
        "uuid" | "uuid128" | "fixed_bytes" => KeyType::Uuid128,
        _ => KeyType::Int32,
    }
}

fn parse_cardinality_bits(s: &str) -> u64 {
    match s.trim().to_lowercase().as_str() {
        "m:1" | "m_1" | "many_to_one" => IMP_REL_CARDINALITY_M_1,
        "1:m" | "1_m" | "one_to_many" => IMP_REL_CARDINALITY_1_M,
        "1:1" | "1_1" | "one_to_one" => IMP_REL_CARDINALITY_1_1,
        _ => IMP_REL_CARDINALITY_M_N,
    }
}

fn parse_encoding_id(s: &str) -> u8 {
    match s.trim().to_lowercase().as_str() {
        "zstd" | "delta_vbyte" | "1" | "0x01" => 1,
        "raw_u16" | "u16" | "2" | "0x02" => 2,
        "coo" | "hybrid_16_32" | "3" | "0x03" => 3,
        _ => 0,
    }
}

fn is_parquet_file(file_path: &Path, format_opt: Option<&str>) -> bool {
    if let Some(fmt) = format_opt {
        if fmt.eq_ignore_ascii_case("parquet") || fmt.eq_ignore_ascii_case("pq") {
            return true;
        }
    }
    if let Some(ext) = file_path.extension() {
        let ext_str = ext.to_string_lossy().to_lowercase();
        return ext_str == "parquet" || ext_str == "pq";
    }
    false
}

fn base_type_element_size(base_type: u8, dimension: u32) -> usize {
    let base_size = match base_type {
        0x01 => 1,  // INT8
        0x02 => 2,  // INT16
        0x03 => 4,  // INT32
        0x04 => 8,  // INT64
        0x05 => 2,  // FLOAT16
        0x06 => 4,  // FLOAT32
        0x07 => 8,  // FLOAT64
        0x08 => 8,  // TIMESTAMP_MS
        0x09 => 8,  // TIMESTAMP_NS
        0x0A => 1,  // FIXED_BYTES
        0x0D => 8,  // INTERVAL_SEC_32 (u32 start_sec, u32 duration_sec)
        0x0E => 16, // INTERVAL_MS_64 (u64 start_ms, u64 duration_ms)
        _ => 4,
    };
    base_size * (dimension.max(1) as usize)
}

fn reorder_native_column(col: &NativeColumn, permutation: &[usize]) -> NativeColumn {
    let is_nullable = (col.type_code & 0x80) != 0;
    let base_type = col.type_code & 0x7F;
    let old_count = col.row_count;
    let new_count = permutation.len();

    let (old_bitmap_words, raw_payload) = if is_nullable {
        let word_count = old_count.div_ceil(64);
        let raw_bitmap_bytes = word_count * 8;
        let padded_bitmap_bytes = if raw_bitmap_bytes == 0 {
            0
        } else {
            (raw_bitmap_bytes + 127) & !127
        };
        let words: Vec<u64> = col.data[..raw_bitmap_bytes]
            .as_chunks::<8>()
            .0
            .iter()
            .copied()
            .map(u64::from_le_bytes)
            .collect();
        (Some(words), &col.data[padded_bitmap_bytes..])
    } else {
        (None, &col.data[..])
    };

    let new_valid_flags: Vec<bool> = if let Some(ref words) = old_bitmap_words {
        permutation
            .iter()
            .map(|&orig| {
                if orig < old_count {
                    (words[orig / 64] & (1u64 << (orig % 64))) != 0
                } else {
                    false
                }
            })
            .collect()
    } else {
        vec![true; new_count]
    };

    let new_null_count = new_valid_flags.iter().filter(|&&v| !v).count();

    if base_type == 0x0B || base_type == 0x0C {
        let old_offsets = col.offsets.as_deref().unwrap_or(&[]);
        let mut new_offsets = vec![0u32];
        let mut new_payload = Vec::new();

        for &orig in permutation {
            if orig + 1 < old_offsets.len() {
                let start = old_offsets[orig] as usize;
                let end = old_offsets[orig + 1] as usize;
                if start <= end && end <= raw_payload.len() {
                    new_payload.extend_from_slice(&raw_payload[start..end]);
                }
            }
            new_offsets.push(new_payload.len() as u32);
        }

        let final_data = if is_nullable {
            let mut buf = build_padded_validity_bitmap(&new_valid_flags);
            buf.extend_from_slice(&new_payload);
            buf
        } else {
            new_payload
        };

        NativeColumn {
            name: col.name.clone(),
            type_code: col.type_code,
            dimension: col.dimension,
            data: final_data,
            offsets: Some(new_offsets),
            null_count: new_null_count,
            row_count: new_count,
        }
    } else {
        let elem_size = base_type_element_size(base_type, col.dimension);
        let mut new_payload = Vec::with_capacity(new_count * elem_size);
        let empty_elem = vec![0u8; elem_size];

        for &orig in permutation {
            let start = orig * elem_size;
            let end = start + elem_size;
            if end <= raw_payload.len() {
                new_payload.extend_from_slice(&raw_payload[start..end]);
            } else {
                new_payload.extend_from_slice(&empty_elem);
            }
        }

        let final_data = if is_nullable {
            let mut buf = build_padded_validity_bitmap(&new_valid_flags);
            buf.extend_from_slice(&new_payload);
            buf
        } else {
            new_payload
        };

        NativeColumn {
            name: col.name.clone(),
            type_code: col.type_code,
            dimension: col.dimension,
            data: final_data,
            offsets: None,
            null_count: new_null_count,
            row_count: new_count,
        }
    }
}

fn read_text_id_strings(
    file_path: &Path,
    id_col_name: &str,
) -> Result<Vec<String>, Box<dyn Error>> {
    let file = File::open(file_path)?;
    let mut reader = BufReader::new(file);
    let mut first_line = String::new();
    reader.read_line(&mut first_line)?;

    let delimiter = if first_line.contains('\t') {
        b'\t'
    } else if first_line.contains(',') {
        b','
    } else {
        b' '
    };

    let has_headers = {
        let trimmed = first_line.trim();
        let tokens: Vec<&str> = if delimiter == b'\t' {
            trimmed.split('\t').collect()
        } else if delimiter == b',' {
            trimmed.split(',').collect()
        } else {
            trimmed.split_whitespace().collect()
        };
        tokens.iter().any(|t| t.trim() == id_col_name)
    };

    if delimiter == b' ' && !has_headers {
        let file = File::open(file_path)?;
        let reader = BufReader::new(file);
        let mut ids = Vec::new();
        for line_res in reader.lines() {
            let line = line_res?;
            let trimmed = line.trim();
            if trimmed.is_empty() || trimmed.starts_with('#') {
                continue;
            }
            let parts: Vec<&str> = trimmed.split_whitespace().collect();
            if !parts.is_empty() {
                ids.push(parts[0].to_string());
            }
        }
        Ok(ids)
    } else {
        let mut csv_reader = csv::ReaderBuilder::new()
            .delimiter(delimiter)
            .has_headers(has_headers)
            .flexible(true)
            .from_path(file_path)?;

        let target_idx = if has_headers {
            let headers = csv_reader.headers()?.clone();
            headers
                .iter()
                .position(|h| h.trim() == id_col_name)
                .unwrap_or(0)
        } else {
            0
        };

        let mut ids = Vec::new();
        for rec in csv_reader.records() {
            let record = rec?;
            if let Some(val) = record.get(target_idx) {
                let trimmed = val.trim();
                if !trimmed.is_empty() {
                    ids.push(trimmed.to_string());
                }
            }
        }
        Ok(ids)
    }
}

fn read_text_edge_endpoints(
    file_path: &Path,
    src_col: &str,
    tgt_col: &str,
) -> Result<(Vec<String>, Vec<String>), Box<dyn Error>> {
    let file = File::open(file_path)?;
    let mut reader = BufReader::new(file);
    let mut first_line = String::new();
    reader.read_line(&mut first_line)?;

    let delimiter = if first_line.contains('\t') {
        b'\t'
    } else if first_line.contains(',') {
        b','
    } else {
        b' '
    };

    let has_headers = {
        let trimmed = first_line.trim();
        let tokens: Vec<&str> = if delimiter == b'\t' {
            trimmed.split('\t').collect()
        } else if delimiter == b',' {
            trimmed.split(',').collect()
        } else {
            trimmed.split_whitespace().collect()
        };
        tokens
            .iter()
            .any(|t| t.trim() == src_col || t.trim() == tgt_col)
    };

    if delimiter == b' ' && !has_headers {
        let file = File::open(file_path)?;
        let reader = BufReader::new(file);
        let mut srcs = Vec::new();
        let mut tgts = Vec::new();
        for line_res in reader.lines() {
            let line = line_res?;
            let trimmed = line.trim();
            if trimmed.is_empty() || trimmed.starts_with('#') {
                continue;
            }
            let parts: Vec<&str> = trimmed.split_whitespace().collect();
            if parts.len() >= 2 {
                srcs.push(parts[0].to_string());
                tgts.push(parts[1].to_string());
            }
        }
        Ok((srcs, tgts))
    } else {
        let mut csv_reader = csv::ReaderBuilder::new()
            .delimiter(delimiter)
            .has_headers(has_headers)
            .flexible(true)
            .from_path(file_path)?;

        let (src_idx, tgt_idx) = if has_headers {
            let headers = csv_reader.headers()?.clone();
            (
                headers
                    .iter()
                    .position(|h| h.trim() == src_col)
                    .unwrap_or(0),
                headers
                    .iter()
                    .position(|h| h.trim() == tgt_col)
                    .unwrap_or(1),
            )
        } else {
            (0, 1)
        };

        let mut srcs = Vec::new();
        let mut tgts = Vec::new();
        for rec in csv_reader.records() {
            let record = rec?;
            if src_idx < record.len() && tgt_idx < record.len() {
                srcs.push(record[src_idx].trim().to_string());
                tgts.push(record[tgt_idx].trim().to_string());
            }
        }
        Ok((srcs, tgts))
    }
}

pub fn run(manifest_path: &Path, output_path: &Path) -> Result<(), Box<dyn Error>> {
    run_with_options(manifest_path, output_path, &[], &[])
}

pub fn run_with_options(
    manifest_path: &Path,
    output_path: &Path,
    cli_embeddings: &[String],
    cli_vector_indices: &[String],
) -> Result<(), Box<dyn Error>> {
    let is_stdout = output_path.to_str() == Some("-") || output_path.to_str() == Some("stdout");

    if !is_stdout {
        println!(
            "Compiling snapshot using manifest: {}...",
            manifest_path.display()
        );
    }

    let manifest_str = fs::read_to_string(manifest_path)?;
    let manifest: Manifest = serde_json::from_str(&manifest_str)?;
    let base_dir = manifest_path.parent().unwrap_or_else(|| Path::new("."));

    let mut writer = SnapshotWriter::new(output_path.to_str().unwrap_or("snapshot.imps"));

    // Maps domain_id -> (key -> NodeIndex)
    let mut domain_node_maps: HashMap<u16, HashMap<String, u32>> = HashMap::new();
    for d in &manifest.domains {
        domain_node_maps.insert(d.id, HashMap::new());
    }

    // Process optional Domain files (for predefined nodes and node attributes)
    let mut compiled_domain_attributes: HashMap<u16, Vec<NativeColumn>> = HashMap::new();

    for d in &manifest.domains {
        if let Some(ref file_name) = d.file {
            let domain_file_path = base_dir.join(file_name);
            if !is_stdout {
                println!("  Reading domain file: {}...", domain_file_path.display());
            }

            let id_col = d.id_column.as_deref().unwrap_or("id");

            if is_parquet_file(&domain_file_path, d.format.as_deref()) {
                let ids = read_parquet_id_strings(&domain_file_path, id_col)?;
                let map = domain_node_maps.get_mut(&d.id).unwrap();
                for key in ids {
                    let next_idx = map.len() as u32;
                    map.entry(key).or_insert(next_idx);
                }

                if let Some(ref attrs) = d.attributes {
                    let reqs: Vec<(&str, Option<&str>, Option<u32>)> = attrs
                        .iter()
                        .map(|a| (a.column.as_str(), a.type_name.as_deref(), a.dimension))
                        .collect();
                    let cols = read_parquet_column_data(&domain_file_path, &reqs)?;
                    let mut domain_cols = Vec::new();
                    for attr in attrs {
                        if let Some(mut col) = cols.get(&attr.column).cloned() {
                            col.name = attr.name.clone();
                            domain_cols.push(col);
                        }
                    }
                    compiled_domain_attributes.insert(d.id, domain_cols);
                }
            } else {
                let id_strings = read_text_id_strings(&domain_file_path, id_col)?;
                let map = domain_node_maps.get_mut(&d.id).unwrap();
                for key in id_strings {
                    let next_idx = map.len() as u32;
                    map.entry(key).or_insert(next_idx);
                }

                if let Some(ref attrs) = d.attributes {
                    let reqs: Vec<(&str, Option<&str>, Option<u32>)> = attrs
                        .iter()
                        .map(|a| (a.column.as_str(), a.type_name.as_deref(), a.dimension))
                        .collect();
                    let cols = read_text_column_data(&domain_file_path, &reqs)?;
                    let mut domain_cols = Vec::new();
                    for attr in attrs {
                        if let Some(mut col) = cols.get(&attr.column).cloned() {
                            col.name = attr.name.clone();
                            domain_cols.push(col);
                        }
                    }
                    compiled_domain_attributes.insert(d.id, domain_cols);
                }
            }
        }
    }

    if let Some(ref meta) = manifest.metadata {
        if !meta.is_empty() {
            writer.set_metadata(meta.clone());
        }
    }

    // Validate relation uniqueness (keyed by endpoints, file, and explicit relation name)
    let mut seen_relations = std::collections::HashSet::new();
    for r in &manifest.relations {
        if !seen_relations.insert((r.src_domain, r.tgt_domain, &r.file, &r.name)) {
            return Err(format!(
                "Duplicate relation definition in manifest for src_domain {} -> tgt_domain {} (name: {:?}, file: {:?})",
                r.src_domain, r.tgt_domain, r.name, r.file
            )
            .into());
        }
    }

    struct CompiledRelation {
        manifest_idx: usize,
        src_domain: u16,
        tgt_domain: u16,
        edges_count: u64,
        row_offsets: Vec<u32>,
        col_indices: Vec<u32>,
        attributes: Vec<NativeColumn>,
    }

    let mut compiled_relations = Vec::new();
    let mut deferred_virtual_relations: Vec<usize> = Vec::new();

    // Process Relations and build Node Maps & Edge Attributes
    for (manifest_idx, r) in manifest.relations.iter().enumerate() {
        if r.virtual_components.is_some() {
            deferred_virtual_relations.push(manifest_idx);
            continue;
        }

        let mut edges: Vec<(u32, u32, usize)> = Vec::new(); // (src_idx, tgt_idx, original_row_index)
        let mut raw_attr_cols: HashMap<String, NativeColumn> = HashMap::new();

        if let Some(ref rel_file) = r.file {
            let edge_file_path = base_dir.join(rel_file);
            if !is_stdout {
                println!("  Reading edge file: {}...", edge_file_path.display());
            }

            let src_col = r.src_column.as_deref().unwrap_or("src");
            let tgt_col = r.tgt_column.as_deref().unwrap_or("tgt");

            if is_parquet_file(&edge_file_path, r.format.as_deref()) {
                let src_keys = read_parquet_id_strings(&edge_file_path, src_col)?;
                let tgt_keys = read_parquet_id_strings(&edge_file_path, tgt_col)?;

                for i in 0..src_keys.len() {
                    let src_idx = {
                        let src_map = domain_node_maps.get_mut(&r.src_domain).unwrap();
                        let next_idx = src_map.len() as u32;
                        *src_map.entry(src_keys[i].clone()).or_insert(next_idx)
                    };

                    let tgt_idx = {
                        let tgt_map = domain_node_maps.get_mut(&r.tgt_domain).unwrap();
                        let next_idx = tgt_map.len() as u32;
                        *tgt_map.entry(tgt_keys[i].clone()).or_insert(next_idx)
                    };

                    edges.push((src_idx, tgt_idx, i));
                }

                if let Some(ref attrs) = r.attributes {
                    let reqs: Vec<(&str, Option<&str>, Option<u32>)> = attrs
                        .iter()
                        .map(|a| (a.column.as_str(), a.type_name.as_deref(), a.dimension))
                        .collect();
                    raw_attr_cols = read_parquet_column_data(&edge_file_path, &reqs)?;
                }
            } else {
                let (src_keys, tgt_keys) =
                    read_text_edge_endpoints(&edge_file_path, src_col, tgt_col)?;

                for i in 0..src_keys.len() {
                    let src_idx = {
                        let src_map = domain_node_maps.get_mut(&r.src_domain).unwrap();
                        let next_idx = src_map.len() as u32;
                        *src_map.entry(src_keys[i].clone()).or_insert(next_idx)
                    };

                    let tgt_idx = {
                        let tgt_map = domain_node_maps.get_mut(&r.tgt_domain).unwrap();
                        let next_idx = tgt_map.len() as u32;
                        *tgt_map.entry(tgt_keys[i].clone()).or_insert(next_idx)
                    };

                    edges.push((src_idx, tgt_idx, i));
                }

                if let Some(ref attrs) = r.attributes {
                    let reqs: Vec<(&str, Option<&str>, Option<u32>)> = attrs
                        .iter()
                        .map(|a| (a.column.as_str(), a.type_name.as_deref(), a.dimension))
                        .collect();
                    raw_attr_cols = read_text_column_data(&edge_file_path, &reqs)?;
                }
            }
        }

        // Sort edges by src_idx then tgt_idx for CSR construction
        edges.sort_by(|a, b| a.0.cmp(&b.0).then_with(|| a.1.cmp(&b.1)));

        let src_domain_count = domain_node_maps.get(&r.src_domain).unwrap().len() as u32;
        let mut row_offsets = vec![0u32; (src_domain_count + 1) as usize];
        let mut col_indices = Vec::with_capacity(edges.len());
        let mut permutation = Vec::with_capacity(edges.len());

        for &(src, tgt, orig_idx) in &edges {
            row_offsets[(src + 1) as usize] += 1;
            col_indices.push(tgt);
            permutation.push(orig_idx);
        }

        // Cumulative sum for row offsets
        for i in 0..src_domain_count as usize {
            row_offsets[i + 1] += row_offsets[i];
        }

        // Process relation attributes aligned with sorted CSR edge indices
        let mut compiled_attrs = Vec::new();
        if let Some(ref attrs) = r.attributes {
            for attr in attrs {
                if let Some(raw_col) = raw_attr_cols.get(&attr.column) {
                    let mut reordered = reorder_native_column(raw_col, &permutation);
                    reordered.name = attr.name.clone();
                    compiled_attrs.push(reordered);
                }
            }
        }

        compiled_relations.push(CompiledRelation {
            manifest_idx,
            src_domain: r.src_domain,
            tgt_domain: r.tgt_domain,
            edges_count: edges.len() as u64,
            row_offsets,
            col_indices,
            attributes: compiled_attrs,
        });
    }

    // Add domain metadata to SnapshotWriter
    for d in &manifest.domains {
        let node_count = domain_node_maps
            .get(&d.id)
            .map(|m| m.len() as u64)
            .unwrap_or(0);
        writer.add_domain(d.id, parse_key_type(&d.key_type), &d.name);
        writer.set_domain_node_count(d.id, node_count);
    }

    // Add physical relations to SnapshotWriter
    let mut rel_name_to_id: HashMap<String, u16> = HashMap::new();
    for (rel_idx, mut cr) in compiled_relations.into_iter().enumerate() {
        let r_def = &manifest.relations[cr.manifest_idx];
        let final_src_count = domain_node_maps
            .get(&cr.src_domain)
            .map(|m| m.len())
            .unwrap_or(0);
        while cr.row_offsets.len() < final_src_count + 1 {
            cr.row_offsets.push(cr.edges_count as u32);
        }

        writer.add_relation(
            cr.src_domain,
            cr.tgt_domain,
            final_src_count as u64,
            cr.edges_count,
            cr.row_offsets,
            cr.col_indices,
        );

        let default_rel_name = r_def
            .file
            .as_deref()
            .and_then(|f| Path::new(f).file_stem())
            .and_then(|s| s.to_str())
            .unwrap_or("RELATION");
        let rel_name = r_def.name.as_deref().unwrap_or(default_rel_name);
        writer.set_relation_name(rel_idx, rel_name);
        rel_name_to_id.insert(rel_name.to_string(), rel_idx as u16);

        if r_def.include_csc.unwrap_or(false) {
            writer.set_relation_include_csc(rel_idx, true);
        }

        let mut node_w = r_def.node_id_width.unwrap_or(4);
        let edge_w = r_def.edge_index_width.unwrap_or(4);
        if let Some(ref enc_str) = r_def.encoding {
            let enc_id = parse_encoding_id(enc_str);
            writer.set_relation_encoding(rel_idx, enc_id);
            if enc_id == 2 && r_def.node_id_width.is_none() {
                node_w = 2;
            }
        }
        writer.set_relation_widths(rel_idx, node_w, edge_w);

        if let Some(ref card_str) = r_def.cardinality {
            writer.set_relation_section_features(rel_idx, parse_cardinality_bits(card_str));
        }

        for attr in cr.attributes {
            writer.add_attribute_to_relation(
                rel_idx,
                &attr.name,
                attr.type_code,
                attr.dimension,
                attr.data,
                attr.offsets,
            );
        }
    }

    // Add virtual super-relations (Spec v0.10.0 §2.4 IMP_REL_FEATURE_VIRTUAL)
    for manifest_idx in deferred_virtual_relations {
        let r_def = &manifest.relations[manifest_idx];
        let v_name = r_def.name.as_deref().unwrap_or("VIRTUAL_RELATION");
        let comp_names = r_def.virtual_components.as_ref().unwrap();
        let mut comp_ids = Vec::with_capacity(comp_names.len());
        for cname in comp_names {
            let cid = rel_name_to_id.get(cname).copied().ok_or_else(|| {
                format!(
                    "Virtual relation '{}' references unknown physical relation '{}'",
                    v_name, cname
                )
            })?;
            comp_ids.push(cid);
        }
        let src_node_count = domain_node_maps
            .get(&r_def.src_domain)
            .map(|m| m.len() as u64)
            .unwrap_or(0);
        let v_idx = writer.add_virtual_relation(
            r_def.src_domain,
            r_def.tgt_domain,
            src_node_count,
            v_name,
            comp_ids,
        );
        rel_name_to_id.insert(v_name.to_string(), v_idx as u16);
    }

    // Add inline domain attributes to SnapshotWriter & resolve key_attr_index (Spec v0.10.0 §5.1)
    let mut domain_key_attr_indices: HashMap<u16, u16> = HashMap::new();
    for d in &manifest.domains {
        let id_col = d.id_column.as_deref().unwrap_or("id");
        let mut resolved_key_attr: Option<u16> = None;

        if let Some(ref attr_defs) = d.attributes {
            for (idx, ad) in attr_defs.iter().enumerate() {
                if let Some(ref explicit_key_attr) = d.key_attribute {
                    if ad.name == *explicit_key_attr {
                        resolved_key_attr = Some(idx as u16);
                        break;
                    }
                } else if ad.column == id_col {
                    resolved_key_attr = Some(idx as u16);
                    break;
                }
            }
        }

        if let Some(domain_attrs) = compiled_domain_attributes.remove(&d.id) {
            for attr in domain_attrs {
                writer.add_attribute_to_domain(
                    d.id,
                    &attr.name,
                    attr.type_code,
                    attr.dimension,
                    attr.data,
                    attr.offsets,
                );
            }
        }

        if let Some(k_idx) = resolved_key_attr {
            writer.set_domain_key_attr(d.id, k_idx);
            domain_key_attr_indices.insert(d.id, k_idx);
        }
    }

    // Attach external sidecar embeddings from manifest.embeddings + CLI --embeddings
    let mut all_embedding_specs: Vec<String> = manifest.embeddings.clone().unwrap_or_default();
    all_embedding_specs.extend_from_slice(cli_embeddings);
    for emb_spec_str in &all_embedding_specs {
        let espec = crate::embeddings::EmbeddingAttachmentSpec::parse(emb_spec_str)?;
        let dom = manifest
            .domains
            .iter()
            .find(|d| d.name == espec.domain_name || d.id.to_string() == espec.domain_name)
            .ok_or_else(|| {
                format!(
                    "Domain '{}' in --embeddings spec not found in manifest",
                    espec.domain_name
                )
            })?;
        let resolved_path = if espec.file_path.is_absolute() || espec.file_path.exists() {
            espec.file_path.clone()
        } else {
            base_dir.join(&espec.file_path)
        };
        let loaded = crate::embeddings::load_embeddings_from_path(
            &resolved_path,
            espec.selector.as_deref(),
            espec.normalize,
        )?;
        let dom_map = domain_node_maps.get(&dom.id);
        let mut dom_node_count = dom_map.map(|m| m.len()).unwrap_or(0);
        if dom_node_count == 0 && loaded.num_vectors > 0 {
            dom_node_count = loaded.num_vectors;
            writer.set_domain_node_count(dom.id, dom_node_count as u64);
        }
        let aligned_vecs = loaded.align_to_domain(dom_node_count, dom_map)?;
        let mut vec_bytes = Vec::with_capacity(aligned_vecs.len() * 4);
        for &v in &aligned_vecs {
            vec_bytes.extend_from_slice(&v.to_le_bytes());
        }
        if !is_stdout {
            println!(
                "  Attached embedding attribute '{}.{}' ({}x{} Float32 from {})",
                dom.name,
                espec.attr_name,
                dom_node_count,
                loaded.dimension,
                resolved_path.display()
            );
        }
        writer.add_attribute_to_domain(
            dom.id,
            &espec.attr_name,
            impulse_graph::spec::BaseDataType::Float32 as u8,
            loaded.dimension as u32,
            vec_bytes,
            None,
        );
    }

    // Add Domain Indexes
    for d in &manifest.domains {
        let map = domain_node_maps.get(&d.id).unwrap();
        let key_count = std::cmp::max(16, (map.len() as f64 * 1.5) as u64);
        let seed = 0x1234567890ABCDEF_u64;

        let mut string_pool = vec![0u8];
        let mut offsets = Vec::new();
        let mut keys = Vec::new();
        let mut node_ids = Vec::new();

        for (k, v) in map {
            keys.push(k.clone());
            node_ids.push(*v);
            offsets.push(string_pool.len() as u32);
            string_pool.extend_from_slice(k.as_bytes());
            string_pool.push(0);
        }

        let string_table_bytes = string_pool.len() as u32;
        let mut table = vec![0u8; (key_count as usize) * 8];

        for i in 0..keys.len() {
            let k = &keys[i];
            let off = offsets[i];
            let nid = node_ids[i];

            let mut h = 14695981039346656037_u64 ^ seed;
            for &b in k.as_bytes() {
                h ^= b as u64;
                h = h.wrapping_mul(1099511628211);
            }

            let mut slot = (h % key_count) as usize;
            let start_slot = slot;
            loop {
                let mut k_off = [0u8; 4];
                k_off.copy_from_slice(&table[slot * 8..slot * 8 + 4]);
                if u32::from_le_bytes(k_off) == 0 {
                    table[slot * 8..slot * 8 + 4].copy_from_slice(&off.to_le_bytes());
                    table[slot * 8 + 4..slot * 8 + 8].copy_from_slice(&nid.to_le_bytes());
                    break;
                }
                slot = (slot + 1) % (key_count as usize);
                if slot == start_slot {
                    panic!("Hash table full!");
                }
            }
        }

        let mut index_data = Vec::new();
        index_data.extend_from_slice(&key_count.to_le_bytes());
        index_data.extend_from_slice(&seed.to_le_bytes());
        index_data.extend_from_slice(&string_table_bytes.to_le_bytes());
        index_data.extend_from_slice(&[0u8; 12]);
        index_data.extend_from_slice(&string_pool);
        index_data.extend_from_slice(&table);

        let key_attr_idx = domain_key_attr_indices.get(&d.id).copied().unwrap_or(0);
        writer.add_index(
            d.id,
            0xFFFF,
            key_attr_idx,
            4, // IMP_INDEX_MINIMAL_PERFECT_HASH
            "_domain_index",
            index_data,
        );
    }

    // Build Vamana ANN Secondary Indices (IMP_INDEX_VAMANA_ANN = 8)
    let mut all_vector_indices: Vec<String> = manifest.vector_indices.clone().unwrap_or_default();
    all_vector_indices.extend_from_slice(cli_vector_indices);
    for v_spec_str in &all_vector_indices {
        let vspec = crate::embeddings::VectorIndexSpec::parse(v_spec_str)?;
        let dom = manifest
            .domains
            .iter()
            .find(|d| d.name == vspec.domain_name || d.id.to_string() == vspec.domain_name)
            .ok_or_else(|| {
                format!(
                    "Domain '{}' in --vector-index spec not found in manifest",
                    vspec.domain_name
                )
            })?;
        if !is_stdout {
            println!(
                "  Building IMP_INDEX_VAMANA_ANN (0x08) '{}' on {}.{} (R={}, L={}, alpha={:.2}, metric={}, quant={})...",
                vspec.index_name,
                dom.name,
                vspec.attr_name,
                vspec.config.max_degree_r,
                vspec.config.ef_construction,
                vspec.config.alpha,
                vspec.config.metric.name(),
                vspec.config.quant_type.name()
            );
        }
        writer.add_vamana_index(dom.id, &vspec.attr_name, &vspec.index_name, &vspec.config)?;
    }

    if is_stdout {
        let stdout = io::stdout();
        let mut handle = BufWriter::new(stdout.lock());
        writer.finalize_to_writer(&mut handle)?;
        handle.flush()?;
    } else {
        writer.finalize()?;
        println!("SUCCESS: Compiled snapshot to {}", output_path.display());
    }

    Ok(())
}
