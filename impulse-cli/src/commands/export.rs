use arrow_array::builder::*;
use arrow_array::*;
use arrow_schema::{DataType, Field, Schema};
use impulse_graph::spec::IMPULSE_TYPE_MASK;
use impulse_graph::SnapshotReader;
use parquet::arrow::ArrowWriter;
use parquet::file::properties::WriterProperties;
use serde::Serialize;
use std::error::Error;
use std::fs::{self, File};
use std::io::{BufWriter, Write};
use std::path::Path;
use std::sync::Arc;

#[derive(Serialize)]
struct ManifestOutput {
    version: String,
    domains: Vec<DomainDefOutput>,
    relations: Vec<RelationDefOutput>,
}

#[derive(Serialize)]
struct DomainDefOutput {
    id: u16,
    name: String,
    key_type: String,
    file: String,
    format: String,
    id_column: String,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    attributes: Vec<AttributeDefOutput>,
}

#[derive(Serialize)]
struct RelationDefOutput {
    name: String,
    src_domain: u16,
    tgt_domain: u16,
    file: String,
    format: String,
    src_column: String,
    tgt_column: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    include_csc: Option<bool>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    attributes: Vec<AttributeDefOutput>,
}

#[derive(Serialize)]
struct AttributeDefOutput {
    name: String,
    column: String,
    #[serde(rename = "type")]
    type_name: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    dimension: Option<u32>,
}

pub fn run(file: &Path, out_dir: &Path, format_str: &str) -> Result<(), Box<dyn Error>> {
    println!(
        "Exporting snapshot {} to {} in [{}] format...",
        file.display(),
        out_dir.display(),
        format_str
    );
    fs::create_dir_all(out_dir)?;

    let reader = SnapshotReader::open(file)?;
    let domains_dir = out_dir.join("domains");
    let relations_dir = out_dir.join("relations");
    fs::create_dir_all(&domains_dir)?;
    fs::create_dir_all(&relations_dir)?;

    let fmt = format_str.trim().to_lowercase();
    let is_parquet = fmt == "parquet" || fmt == "pq";
    let is_jsonl = fmt == "jsonl";
    let is_csv = fmt == "csv";
    let file_ext = if is_parquet {
        "parquet"
    } else if is_jsonl {
        "jsonl"
    } else if is_csv {
        "csv"
    } else {
        "tsv"
    };

    let mut manifest_domains = Vec::new();
    let mut manifest_relations = Vec::new();

    // 1. Export Domains
    for d in reader.domains() {
        let n = d.node_count as usize;
        let dom_file_name = format!("{}.{}", d.name, file_ext);
        let dom_rel_path = format!("domains/{}", dom_file_name);
        let dom_out_path = domains_dir.join(&dom_file_name);

        println!(
            "  Exporting Domain '{}' (N={}) -> {}",
            d.name,
            n,
            dom_out_path.display()
        );

        // Collect Spec v0.10.0 inline domain attributes (falling back to v0.9.0 carrier relations)
        let mut dom_attrs: Vec<impulse_graph::AttributeInfo> = d.attributes.clone();
        if dom_attrs.is_empty() {
            let expected_carrier_name = format!("{}_nodes", d.name);
            for r in reader.relations() {
                let is_carrier = (r.name == expected_carrier_name
                    || r.name == format!("Domain_{}_nodes", d.domain_id)
                    || (r.name.ends_with("_nodes") && r.edge_count == 0))
                    && r.src_domain_id == d.domain_id
                    && r.tgt_domain_id == d.domain_id;
                if is_carrier {
                    for a in &r.attributes {
                        dom_attrs.push(a.clone());
                    }
                }
            }
        }

        // Fetch string keys if available from Section 4 or domain attribute
        let mut keys_list = Vec::with_capacity(n);
        if let Ok(data) = reader.get_domain_attribute_data(d.domain_id, "_key") {
            if let Ok(Some(offsets)) = reader.get_domain_attribute_offsets(d.domain_id, "_key") {
                for i in 0..n {
                    if i + 1 < offsets.len() {
                        let s = offsets[i] as usize;
                        let e = offsets[i + 1] as usize;
                        if s <= e && e <= data.len() {
                            let str_val = std::str::from_utf8(&data[s..e]).unwrap_or("");
                            keys_list.push(str_val.to_string());
                        } else {
                            keys_list.push(i.to_string());
                        }
                    } else {
                        keys_list.push(i.to_string());
                    }
                }
            }
        }
        if keys_list.len() != n {
            keys_list = (0..n).map(|i| i.to_string()).collect();
        }

        // Build Manifest Domain Def
        let mut attr_defs = Vec::new();
        for a in &dom_attrs {
            if a.name == "_key" {
                continue;
            }
            let is_nullable = (a.type_code & 0x80) != 0;
            let base_type = a.type_code & IMPULSE_TYPE_MASK;
            let base_str = match base_type {
                0x01 => "int8",
                0x02 => "int16",
                0x03 => "int32",
                0x04 => "int64",
                0x05 => "float16",
                0x06 => "float32",
                0x07 => "float64",
                0x08 => "timestamp_ms",
                0x09 => "timestamp_ns",
                0x0A => "fixed_bytes",
                0x0B => "string",
                0x0C => "var_bytes",
                _ => "int32",
            };
            let type_str = if is_nullable {
                format!("nullable_{}", base_str)
            } else {
                base_str.to_string()
            };
            attr_defs.push(AttributeDefOutput {
                name: a.name.clone(),
                column: a.name.clone(),
                type_name: type_str,
                dimension: if a.dimension > 1 {
                    Some(a.dimension)
                } else {
                    None
                },
            });
        }

        manifest_domains.push(DomainDefOutput {
            id: d.domain_id,
            name: d.name.clone(),
            key_type: format!("{:?}", d.key_type).to_lowercase(),
            file: dom_rel_path,
            format: if is_parquet {
                "parquet".into()
            } else {
                "csv".into()
            },
            id_column: "id".to_string(),
            attributes: attr_defs,
        });

        // Write Domain File
        if is_parquet {
            export_domain_parquet(
                &dom_out_path,
                &keys_list,
                &dom_attrs,
                &reader,
                d.domain_id,
                n,
            )?;
        } else {
            export_domain_text(
                &dom_out_path,
                &keys_list,
                &dom_attrs,
                &reader,
                d.domain_id,
                n,
                is_jsonl,
                is_csv,
            )?;
        }
    }

    // 2. Export Relations (filtering out synthetic `*_nodes` carrier relations)
    for (r_idx, r) in reader.relations().iter().enumerate() {
        if (r.name.ends_with("_nodes") && r.src_domain_id == r.tgt_domain_id && r.edge_count == 0)
            || r.is_virtual()
        {
            continue;
        }

        let rel_name = if r.name.is_empty() {
            format!("rel_{}_{}", r.src_domain_id, r.tgt_domain_id)
        } else {
            r.name.clone()
        };

        let rel_file_name = format!("{}.{}", rel_name, file_ext);
        let rel_rel_path = format!("relations/{}", rel_file_name);
        let rel_out_path = relations_dir.join(&rel_file_name);

        println!(
            "  Exporting Relation '{}' (E={}) -> {}",
            rel_name,
            r.edge_count,
            rel_out_path.display()
        );

        let (row_offsets_vec, col_indices_vec, _) =
            crate::commands::index::extract_relation_csr_and_coo(&reader, r_idx, r)?;
        let row_offsets = row_offsets_vec.as_slice();
        let col_indices = col_indices_vec.as_slice();

        let mut attr_defs = Vec::new();
        for a in &r.attributes {
            let is_nullable = (a.type_code & 0x80) != 0;
            let base_type = a.type_code & IMPULSE_TYPE_MASK;
            let base_str = match base_type {
                0x01 => "int8",
                0x02 => "int16",
                0x03 => "int32",
                0x04 => "int64",
                0x05 => "float16",
                0x06 => "float32",
                0x07 => "float64",
                0x08 => "timestamp_ms",
                0x09 => "timestamp_ns",
                0x0A => "fixed_bytes",
                0x0B => "string",
                0x0C => "var_bytes",
                _ => "float32",
            };
            let type_str = if is_nullable {
                format!("nullable_{}", base_str)
            } else {
                base_str.to_string()
            };
            attr_defs.push(AttributeDefOutput {
                name: a.name.clone(),
                column: a.name.clone(),
                type_name: type_str,
                dimension: if a.dimension > 1 {
                    Some(a.dimension)
                } else {
                    None
                },
            });
        }

        manifest_relations.push(RelationDefOutput {
            name: rel_name,
            src_domain: r.src_domain_id,
            tgt_domain: r.tgt_domain_id,
            file: rel_rel_path,
            format: if is_parquet {
                "parquet".into()
            } else {
                "csv".into()
            },
            src_column: "src".to_string(),
            tgt_column: "tgt".to_string(),
            include_csc: if r.has_csc() { Some(true) } else { None },
            attributes: attr_defs,
        });

        // Write Relation File
        if is_parquet {
            export_relation_parquet(&rel_out_path, r, row_offsets, col_indices, &reader, r_idx)?;
        } else {
            export_relation_text(
                &rel_out_path,
                r,
                row_offsets,
                col_indices,
                &reader,
                r_idx,
                is_jsonl,
                is_csv,
            )?;
        }
    }

    // 3. Emit manifest.json for round-trip builds
    let manifest = ManifestOutput {
        version: "0.10.0".to_string(),
        domains: manifest_domains,
        relations: manifest_relations,
    };
    let manifest_path = out_dir.join("manifest.json");
    let manifest_file = File::create(&manifest_path)?;
    serde_json::to_writer_pretty(BufWriter::new(manifest_file), &manifest)?;

    println!(
        "SUCCESS: Exported snapshot and generated round-trip manifest to {}",
        manifest_path.display()
    );
    Ok(())
}

#[allow(clippy::too_many_arguments)]
fn export_domain_text(
    path: &Path,
    keys: &[String],
    attrs: &[impulse_graph::AttributeInfo],
    reader: &SnapshotReader,
    domain_id: u16,
    _node_count: usize,
    is_jsonl: bool,
    is_csv: bool,
) -> Result<(), Box<dyn Error>> {
    let file = File::create(path)?;
    let mut writer = BufWriter::new(file);
    let sep = if is_csv { "," } else { "\t" };

    if !is_jsonl {
        write!(writer, "id")?;
        for a in attrs {
            if a.name != "_key" {
                write!(writer, "{}{}", sep, a.name)?;
            }
        }
        writeln!(writer)?;
    }

    for (u, key) in keys.iter().enumerate() {
        if is_jsonl {
            let mut map = serde_json::Map::new();
            map.insert("id".to_string(), serde_json::Value::String(key.clone()));
            for a in attrs {
                if a.name == "_key" {
                    continue;
                }
                if let Ok(val) = extract_attribute_string_value(reader, domain_id, a, u) {
                    map.insert(a.name.clone(), serde_json::Value::String(val));
                }
            }
            writeln!(writer, "{}", serde_json::Value::Object(map))?;
        } else {
            write!(writer, "{}", key)?;
            for a in attrs {
                if a.name == "_key" {
                    continue;
                }
                let val =
                    extract_attribute_string_value(reader, domain_id, a, u).unwrap_or_default();
                write!(writer, "{}{}", sep, val)?;
            }
            writeln!(writer)?;
        }
    }
    writer.flush()?;
    Ok(())
}

#[allow(clippy::too_many_arguments)]
fn export_relation_text(
    path: &Path,
    rel: &impulse_graph::RelationInfo,
    row_offsets: &[u32],
    col_indices: &[u32],
    reader: &SnapshotReader,
    rel_idx: usize,
    is_jsonl: bool,
    is_csv: bool,
) -> Result<(), Box<dyn Error>> {
    let file = File::create(path)?;
    let mut writer = BufWriter::new(file);
    let sep = if is_csv { "," } else { "\t" };

    if !is_jsonl {
        write!(writer, "src{}tgt", sep)?;
        for a in &rel.attributes {
            write!(writer, "{}{}", sep, a.name)?;
        }
        writeln!(writer)?;
    }

    let mut edge_counter = 0;
    for u in 0..rel.node_count as usize {
        if u + 1 >= row_offsets.len() {
            break;
        }
        let start = row_offsets[u] as usize;
        let end = (row_offsets[u + 1] as usize).min(col_indices.len());
        for &v in &col_indices[start..end] {
            if is_jsonl {
                let mut map = serde_json::Map::new();
                map.insert(
                    "src".to_string(),
                    serde_json::Value::Number(serde_json::Number::from(u)),
                );
                map.insert(
                    "tgt".to_string(),
                    serde_json::Value::Number(serde_json::Number::from(v)),
                );
                for (a_idx, a) in rel.attributes.iter().enumerate() {
                    if let Ok(val) =
                        extract_edge_attribute_string_value(reader, rel_idx, a_idx, edge_counter, a)
                    {
                        map.insert(a.name.clone(), serde_json::Value::String(val));
                    }
                }
                writeln!(writer, "{}", serde_json::Value::Object(map))?;
            } else {
                write!(writer, "{}{}{}", u, sep, v)?;
                for (a_idx, a) in rel.attributes.iter().enumerate() {
                    let val = extract_edge_attribute_string_value(
                        reader,
                        rel_idx,
                        a_idx,
                        edge_counter,
                        a,
                    )
                    .unwrap_or_default();
                    write!(writer, "{}{}", sep, val)?;
                }
                writeln!(writer)?;
            }
            edge_counter += 1;
        }
    }
    writer.flush()?;
    Ok(())
}

fn extract_attribute_string_value(
    reader: &SnapshotReader,
    domain_id: u16,
    a: &impulse_graph::AttributeInfo,
    idx: usize,
) -> Result<String, Box<dyn Error>> {
    let data = reader.get_domain_attribute_data(domain_id, &a.name)?;
    let base_t = a.type_code & IMPULSE_TYPE_MASK;
    let is_nullable = (a.type_code & 0x80) != 0;

    let n_nodes = reader
        .domains()
        .iter()
        .find(|d| d.domain_id == domain_id)
        .map(|d| d.node_count as usize)
        .unwrap_or(idx + 1);

    let (valid_bitmap, actual_data) = if is_nullable {
        let word_count = n_nodes.div_ceil(64);
        let bitmap_bytes = word_count * 8;
        let padded_bitmap_bytes = if bitmap_bytes == 0 {
            0
        } else {
            (bitmap_bytes + 127) & !127
        };
        if data.len() >= padded_bitmap_bytes {
            (&data[0..bitmap_bytes], &data[padded_bitmap_bytes..])
        } else {
            (&[][..], data)
        }
    } else {
        (&[][..], data)
    };

    if is_nullable && !valid_bitmap.is_empty() {
        let byte_pos = idx / 8;
        let bit_pos = idx % 8;
        if byte_pos < valid_bitmap.len() && (valid_bitmap[byte_pos] & (1 << bit_pos)) == 0 {
            return Ok("".to_string());
        }
    }

    match base_t {
        0x01 => {
            if idx < actual_data.len() {
                Ok((actual_data[idx] as i8).to_string())
            } else {
                Ok("0".into())
            }
        }
        0x02 => {
            if (idx + 1) * 2 <= actual_data.len() {
                let v = i16::from_le_bytes(actual_data[idx * 2..(idx + 1) * 2].try_into()?);
                Ok(v.to_string())
            } else {
                Ok("0".into())
            }
        }
        0x03 => {
            if (idx + 1) * 4 <= actual_data.len() {
                let v = i32::from_le_bytes(actual_data[idx * 4..(idx + 1) * 4].try_into()?);
                Ok(v.to_string())
            } else {
                Ok("0".into())
            }
        }
        0x04 | 0x08 | 0x09 => {
            if (idx + 1) * 8 <= actual_data.len() {
                let v = i64::from_le_bytes(actual_data[idx * 8..(idx + 1) * 8].try_into()?);
                Ok(v.to_string())
            } else {
                Ok("0".into())
            }
        }
        0x06 => {
            let dim = a.dimension.max(1) as usize;
            if dim == 1 {
                if (idx + 1) * 4 <= actual_data.len() {
                    let v = f32::from_le_bytes(actual_data[idx * 4..(idx + 1) * 4].try_into()?);
                    Ok(v.to_string())
                } else {
                    Ok("0.0".into())
                }
            } else {
                let mut vals = Vec::with_capacity(dim);
                let start = idx * dim * 4;
                for d in 0..dim {
                    let off = start + d * 4;
                    if off + 4 <= actual_data.len() {
                        vals.push(
                            f32::from_le_bytes(actual_data[off..off + 4].try_into()?).to_string(),
                        );
                    }
                }
                Ok(vals.join(";"))
            }
        }
        0x07 => {
            if (idx + 1) * 8 <= actual_data.len() {
                let v = f64::from_le_bytes(actual_data[idx * 8..(idx + 1) * 8].try_into()?);
                Ok(v.to_string())
            } else {
                Ok("0.0".into())
            }
        }
        0x0B => {
            if let Ok(Some(offsets)) = reader.get_domain_attribute_offsets(domain_id, &a.name) {
                if idx + 1 < offsets.len() {
                    let s = offsets[idx] as usize;
                    let e = offsets[idx + 1] as usize;
                    if s <= e && e <= actual_data.len() {
                        let raw = std::str::from_utf8(&actual_data[s..e]).unwrap_or("");
                        return Ok(raw.trim_end_matches('\0').to_string());
                    }
                }
            }
            Ok("".into())
        }
        _ => Ok("".into()),
    }
}

fn extract_edge_attribute_string_value(
    reader: &SnapshotReader,
    rel_idx: usize,
    attr_idx: usize,
    edge_idx: usize,
    a: &impulse_graph::AttributeInfo,
) -> Result<String, Box<dyn Error>> {
    let data = reader.get_attribute_data(rel_idx, attr_idx)?;
    let base_t = a.type_code & IMPULSE_TYPE_MASK;
    let is_nullable = (a.type_code & 0x80) != 0;

    let m_edges = reader
        .relations()
        .get(rel_idx)
        .map(|r| r.edge_count as usize)
        .unwrap_or(edge_idx + 1);

    let (valid_bitmap, actual_data) = if is_nullable {
        let word_count = m_edges.div_ceil(64);
        let bitmap_bytes = word_count * 8;
        let padded_bitmap_bytes = if bitmap_bytes == 0 {
            0
        } else {
            (bitmap_bytes + 127) & !127
        };
        if data.len() >= padded_bitmap_bytes {
            (&data[0..bitmap_bytes], &data[padded_bitmap_bytes..])
        } else {
            (&[][..], data)
        }
    } else {
        (&[][..], data)
    };

    if is_nullable && !valid_bitmap.is_empty() {
        let byte_pos = edge_idx / 8;
        let bit_pos = edge_idx % 8;
        if byte_pos < valid_bitmap.len() && (valid_bitmap[byte_pos] & (1 << bit_pos)) == 0 {
            return Ok("".to_string());
        }
    }

    match base_t {
        0x01 => {
            if edge_idx < actual_data.len() {
                Ok((actual_data[edge_idx] as i8).to_string())
            } else {
                Ok("0".into())
            }
        }
        0x02 => {
            if (edge_idx + 1) * 2 <= actual_data.len() {
                let v =
                    i16::from_le_bytes(actual_data[edge_idx * 2..(edge_idx + 1) * 2].try_into()?);
                Ok(v.to_string())
            } else {
                Ok("0".into())
            }
        }
        0x03 => {
            if (edge_idx + 1) * 4 <= actual_data.len() {
                let v =
                    i32::from_le_bytes(actual_data[edge_idx * 4..(edge_idx + 1) * 4].try_into()?);
                Ok(v.to_string())
            } else {
                Ok("0".into())
            }
        }
        0x04 | 0x08 | 0x09 => {
            if (edge_idx + 1) * 8 <= actual_data.len() {
                let v =
                    i64::from_le_bytes(actual_data[edge_idx * 8..(edge_idx + 1) * 8].try_into()?);
                Ok(v.to_string())
            } else {
                Ok("0".into())
            }
        }
        0x06 => {
            let dim = a.dimension.max(1) as usize;
            if dim == 1 {
                if (edge_idx + 1) * 4 <= actual_data.len() {
                    let v = f32::from_le_bytes(
                        actual_data[edge_idx * 4..(edge_idx + 1) * 4].try_into()?,
                    );
                    Ok(v.to_string())
                } else {
                    Ok("0.0".into())
                }
            } else {
                let mut vals = Vec::with_capacity(dim);
                let start = edge_idx * dim * 4;
                for d in 0..dim {
                    let off = start + d * 4;
                    if off + 4 <= actual_data.len() {
                        vals.push(
                            f32::from_le_bytes(actual_data[off..off + 4].try_into()?).to_string(),
                        );
                    }
                }
                Ok(vals.join(";"))
            }
        }
        0x07 => {
            if (edge_idx + 1) * 8 <= actual_data.len() {
                let v =
                    f64::from_le_bytes(actual_data[edge_idx * 8..(edge_idx + 1) * 8].try_into()?);
                Ok(v.to_string())
            } else {
                Ok("0.0".into())
            }
        }
        0x0B => {
            if let Ok(Some(offsets)) = reader.get_attribute_offsets(rel_idx, attr_idx) {
                if edge_idx + 1 < offsets.len() {
                    let s = offsets[edge_idx] as usize;
                    let e = offsets[edge_idx + 1] as usize;
                    if s <= e && e <= actual_data.len() {
                        let raw = std::str::from_utf8(&actual_data[s..e]).unwrap_or("");
                        return Ok(raw.trim_end_matches('\0').to_string());
                    }
                }
            }
            Ok("".into())
        }
        _ => Ok("".into()),
    }
}

fn export_domain_parquet(
    path: &Path,
    keys: &[String],
    attrs: &[impulse_graph::AttributeInfo],
    reader: &SnapshotReader,
    domain_id: u16,
    node_count: usize,
) -> Result<(), Box<dyn Error>> {
    let mut fields = vec![Field::new("id", DataType::Utf8, false)];
    for a in attrs {
        if a.name == "_key" {
            continue;
        }
        let base_t = a.type_code & IMPULSE_TYPE_MASK;
        let is_nullable = (a.type_code & 0x80) != 0;
        let dt = match base_t {
            0x01 => DataType::Int8,
            0x02 => DataType::Int16,
            0x03 => DataType::Int32,
            0x04 => DataType::Int64,
            0x06 => {
                if a.dimension > 1 {
                    DataType::FixedSizeList(
                        Arc::new(Field::new("item", DataType::Float32, false)),
                        a.dimension as i32,
                    )
                } else {
                    DataType::Float32
                }
            }
            0x07 => DataType::Float64,
            0x08 | 0x09 => DataType::Int64,
            0x0B => DataType::Utf8,
            _ => DataType::Utf8,
        };
        fields.push(Field::new(&a.name, dt, is_nullable));
    }

    let schema = Arc::new(Schema::new(fields));
    let mut columns: Vec<Arc<dyn Array>> = Vec::new();

    // 1. Column: id
    let mut id_builder = StringBuilder::with_capacity(node_count, node_count * 8);
    for k in keys {
        id_builder.append_value(k);
    }
    columns.push(Arc::new(id_builder.finish()));

    // 2. Attributes
    for a in attrs {
        if a.name == "_key" {
            continue;
        }
        let base_t = a.type_code & IMPULSE_TYPE_MASK;
        match base_t {
            0x01 => {
                let mut b = Int8Builder::with_capacity(node_count);
                for u in 0..node_count {
                    let s = extract_attribute_string_value(reader, domain_id, a, u)?;
                    if s.is_empty() {
                        b.append_null();
                    } else {
                        b.append_value(s.parse::<i8>().unwrap_or(0));
                    }
                }
                columns.push(Arc::new(b.finish()));
            }
            0x02 => {
                let mut b = Int16Builder::with_capacity(node_count);
                for u in 0..node_count {
                    let s = extract_attribute_string_value(reader, domain_id, a, u)?;
                    if s.is_empty() {
                        b.append_null();
                    } else {
                        b.append_value(s.parse::<i16>().unwrap_or(0));
                    }
                }
                columns.push(Arc::new(b.finish()));
            }
            0x03 => {
                let mut b = Int32Builder::with_capacity(node_count);
                for u in 0..node_count {
                    let s = extract_attribute_string_value(reader, domain_id, a, u)?;
                    if s.is_empty() {
                        b.append_null();
                    } else {
                        b.append_value(s.parse::<i32>().unwrap_or(0));
                    }
                }
                columns.push(Arc::new(b.finish()));
            }
            0x04 | 0x08 | 0x09 => {
                let mut b = Int64Builder::with_capacity(node_count);
                for u in 0..node_count {
                    let s = extract_attribute_string_value(reader, domain_id, a, u)?;
                    if s.is_empty() {
                        b.append_null();
                    } else {
                        b.append_value(s.parse::<i64>().unwrap_or(0));
                    }
                }
                columns.push(Arc::new(b.finish()));
            }
            0x06 => {
                if a.dimension > 1 {
                    let dim = a.dimension as usize;
                    let mut item_builder = Float32Builder::with_capacity(node_count * dim);
                    for u in 0..node_count {
                        let s = extract_attribute_string_value(reader, domain_id, a, u)?;
                        let parts: Vec<&str> = s.split(';').collect();
                        for d in 0..dim {
                            let f = parts
                                .get(d)
                                .and_then(|p| p.parse::<f32>().ok())
                                .unwrap_or(0.0);
                            item_builder.append_value(f);
                        }
                    }
                    let item_arr = Arc::new(item_builder.finish());
                    let fsl = FixedSizeListArray::new(
                        Arc::new(Field::new("item", DataType::Float32, false)),
                        a.dimension as i32,
                        item_arr,
                        None,
                    );
                    columns.push(Arc::new(fsl));
                } else {
                    let mut b = Float32Builder::with_capacity(node_count);
                    for u in 0..node_count {
                        let s = extract_attribute_string_value(reader, domain_id, a, u)?;
                        if s.is_empty() {
                            b.append_null();
                        } else {
                            b.append_value(s.parse::<f32>().unwrap_or(0.0));
                        }
                    }
                    columns.push(Arc::new(b.finish()));
                }
            }
            0x07 => {
                let mut b = Float64Builder::with_capacity(node_count);
                for u in 0..node_count {
                    let s = extract_attribute_string_value(reader, domain_id, a, u)?;
                    if s.is_empty() {
                        b.append_null();
                    } else {
                        b.append_value(s.parse::<f64>().unwrap_or(0.0));
                    }
                }
                columns.push(Arc::new(b.finish()));
            }
            _ => {
                let mut b = StringBuilder::with_capacity(node_count, node_count * 16);
                for u in 0..node_count {
                    let s = extract_attribute_string_value(reader, domain_id, a, u)?;
                    b.append_value(&s);
                }
                columns.push(Arc::new(b.finish()));
            }
        }
    }

    let batch = RecordBatch::try_new(schema.clone(), columns)?;
    let out_file = File::create(path)?;
    let props = WriterProperties::builder().build();
    let mut writer = ArrowWriter::try_new(out_file, schema, Some(props))?;
    writer.write(&batch)?;
    writer.close()?;
    Ok(())
}

fn export_relation_parquet(
    path: &Path,
    rel: &impulse_graph::RelationInfo,
    row_offsets: &[u32],
    col_indices: &[u32],
    reader: &SnapshotReader,
    rel_idx: usize,
) -> Result<(), Box<dyn Error>> {
    let edge_count = rel.edge_count as usize;
    let mut fields = vec![
        Field::new("src", DataType::UInt32, false),
        Field::new("tgt", DataType::UInt32, false),
    ];
    for a in &rel.attributes {
        let base_t = a.type_code & IMPULSE_TYPE_MASK;
        let is_nullable = (a.type_code & 0x80) != 0;
        let dt = match base_t {
            0x01 => DataType::Int8,
            0x02 => DataType::Int16,
            0x03 => DataType::Int32,
            0x04 => DataType::Int64,
            0x06 => DataType::Float32,
            0x07 => DataType::Float64,
            _ => DataType::Float32,
        };
        fields.push(Field::new(&a.name, dt, is_nullable));
    }

    let schema = Arc::new(Schema::new(fields));
    let mut src_builder = UInt32Builder::with_capacity(edge_count);
    let mut tgt_builder = UInt32Builder::with_capacity(edge_count);

    for u in 0..rel.node_count as usize {
        if u + 1 >= row_offsets.len() {
            break;
        }
        let start = row_offsets[u] as usize;
        let end = (row_offsets[u + 1] as usize).min(col_indices.len());
        for &v in &col_indices[start..end] {
            src_builder.append_value(u as u32);
            tgt_builder.append_value(v);
        }
    }

    let mut columns: Vec<Arc<dyn Array>> = vec![
        Arc::new(src_builder.finish()),
        Arc::new(tgt_builder.finish()),
    ];

    for (a_idx, a) in rel.attributes.iter().enumerate() {
        let base_t = a.type_code & IMPULSE_TYPE_MASK;
        match base_t {
            0x01 => {
                let mut b = Int8Builder::with_capacity(edge_count);
                for e in 0..edge_count {
                    let s = extract_edge_attribute_string_value(reader, rel_idx, a_idx, e, a)?;
                    if s.is_empty() {
                        b.append_null();
                    } else {
                        b.append_value(s.parse::<i8>().unwrap_or(0));
                    }
                }
                columns.push(Arc::new(b.finish()));
            }
            0x03 => {
                let mut b = Int32Builder::with_capacity(edge_count);
                for e in 0..edge_count {
                    let s = extract_edge_attribute_string_value(reader, rel_idx, a_idx, e, a)?;
                    if s.is_empty() {
                        b.append_null();
                    } else {
                        b.append_value(s.parse::<i32>().unwrap_or(0));
                    }
                }
                columns.push(Arc::new(b.finish()));
            }
            0x04 => {
                let mut b = Int64Builder::with_capacity(edge_count);
                for e in 0..edge_count {
                    let s = extract_edge_attribute_string_value(reader, rel_idx, a_idx, e, a)?;
                    if s.is_empty() {
                        b.append_null();
                    } else {
                        b.append_value(s.parse::<i64>().unwrap_or(0));
                    }
                }
                columns.push(Arc::new(b.finish()));
            }
            0x06 => {
                let mut b = Float32Builder::with_capacity(edge_count);
                for e in 0..edge_count {
                    let s = extract_edge_attribute_string_value(reader, rel_idx, a_idx, e, a)?;
                    if s.is_empty() {
                        b.append_null();
                    } else {
                        b.append_value(s.parse::<f32>().unwrap_or(0.0));
                    }
                }
                columns.push(Arc::new(b.finish()));
            }
            _ => {
                let mut b = Float64Builder::with_capacity(edge_count);
                for e in 0..edge_count {
                    let s = extract_edge_attribute_string_value(reader, rel_idx, a_idx, e, a)?;
                    if s.is_empty() {
                        b.append_null();
                    } else {
                        b.append_value(s.parse::<f64>().unwrap_or(0.0));
                    }
                }
                columns.push(Arc::new(b.finish()));
            }
        }
    }

    let batch = RecordBatch::try_new(schema.clone(), columns)?;
    let out_file = File::create(path)?;
    let props = WriterProperties::builder().build();
    let mut writer = ArrowWriter::try_new(out_file, schema, Some(props))?;
    writer.write(&batch)?;
    writer.close()?;
    Ok(())
}
