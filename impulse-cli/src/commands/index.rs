use comfy_table::Table;
use impulse_graph::ffi::*;
use impulse_graph::spec::*;
use impulse_graph::vamana::{build_vamana_index, VamanaBuildConfig, VamanaMetric, VamanaQuantType};
use impulse_graph::{SnapshotReader, SnapshotWriter};
use serde_json::json;
use std::error::Error;
use std::ffi::CString;
use std::path::Path;

pub fn list(snapshot_path: &Path, format: &str) -> Result<(), Box<dyn Error>> {
    let reader = SnapshotReader::open(snapshot_path)?;
    let entries = reader.index_entries();

    let is_json = format.eq_ignore_ascii_case("json");
    if is_json {
        let mut list_arr = Vec::new();
        for (i, entry) in entries.iter().enumerate() {
            list_arr.push(json!({
                "index_id": i,
                "name": entry.name,
                "type_code": entry.index_type,
                "type_name": index_type_to_string(entry.index_type),
                "domain_id": entry.domain_id,
                "relation_id": entry.relation_id,
                "attribute_index": entry.attribute_index,
                "data_offset": entry.data_offset,
                "data_bytes": entry.data_bytes,
            }));
        }
        println!("{}", serde_json::Value::Array(list_arr));
        return Ok(());
    }

    println!("Secondary & Vector Indexes in: {}", snapshot_path.display());
    if entries.is_empty() {
        println!("  (No secondary indexes found in snapshot)");
        return Ok(());
    }

    let mut table = Table::new();
    table.set_header(vec![
        "ID",
        "Name",
        "Type",
        "Domain",
        "Rel/Attr",
        "Data Offset",
        "Data Bytes",
    ]);

    for (i, entry) in entries.iter().enumerate() {
        let type_name = index_type_to_string(entry.index_type);
        let rel_attr_str = if entry.relation_id == 0xFFFF {
            format!("NodeAttr #{}", entry.attribute_index)
        } else {
            format!(
                "Rel #{} / Attr #{}",
                entry.relation_id, entry.attribute_index
            )
        };

        table.add_row(vec![
            i.to_string(),
            entry.name.clone(),
            type_name.to_string(),
            entry.domain_id.to_string(),
            rel_attr_str,
            format!("0x{:08X}", entry.data_offset),
            format!("{} B", entry.data_bytes),
        ]);
    }

    println!("{}", table);
    Ok(())
}

pub fn add(input_path: &Path, output_path: &Path, specs: &[String]) -> Result<(), Box<dyn Error>> {
    println!(
        "Adding {} index(es) to snapshot: {} -> {}",
        specs.len(),
        input_path.display(),
        output_path.display()
    );

    let reader = SnapshotReader::open(input_path)?;
    let output_str = output_path
        .to_str()
        .ok_or("Output path is not valid UTF-8")?;
    let mut writer = SnapshotWriter::new(output_str);

    // 1..3. Replay metadata, domains (with v0.10.0 inline attributes & key_attr_index), and relations
    replay_snapshot_into_writer(&reader, &mut writer)?;

    // 4. Replay existing indexes
    let snapshot_bytes = reader.as_bytes();
    for entry in reader.index_entries() {
        let start = entry.data_offset as usize;
        let len = entry.data_bytes as usize;
        if start + len <= snapshot_bytes.len() {
            let mut blob = snapshot_bytes[start..start + len].to_vec();
            relativize_vamana_blob_offsets(entry.index_type, entry.data_offset, &mut blob);
            writer.add_index(
                entry.domain_id,
                entry.relation_id,
                entry.attribute_index,
                entry.index_type,
                &entry.name,
                blob,
            );
        }
    }

    // 5. Build and add new indexes
    for spec_str in specs {
        build_and_add_index(&reader, &mut writer, spec_str)?;
    }

    writer.finalize()?;
    println!(
        "SUCCESS: Added indexes and generated {}",
        output_path.display()
    );
    Ok(())
}

pub fn drop(
    input_path: &Path,
    output_path: &Path,
    target_name: Option<&str>,
    target_type: Option<&str>,
    drop_all: bool,
) -> Result<(), Box<dyn Error>> {
    println!(
        "Dropping index(es) from snapshot: {} -> {}",
        input_path.display(),
        output_path.display()
    );

    let reader = SnapshotReader::open(input_path)?;
    let output_str = output_path
        .to_str()
        .ok_or("Output path is not valid UTF-8")?;
    let mut writer = SnapshotWriter::new(output_str);

    // 1..3. Replay metadata, domains, and relations
    replay_snapshot_into_writer(&reader, &mut writer)?;

    // 4. Replay indexes filtering out drops
    let snapshot_bytes = reader.as_bytes();
    let mut kept_count = 0;
    let mut dropped_count = 0;

    for entry in reader.index_entries() {
        let mut should_drop = drop_all;
        if let Some(name) = target_name {
            if entry.name.eq_ignore_ascii_case(name) {
                should_drop = true;
            }
        }
        if let Some(t_str) = target_type {
            if matches_index_type(entry.index_type, t_str) {
                should_drop = true;
            }
        }

        if should_drop {
            dropped_count += 1;
            println!(
                "  [DROPPED] Index '{}' (type 0x{:02X})",
                entry.name, entry.index_type
            );
        } else {
            kept_count += 1;
            let start = entry.data_offset as usize;
            let len = entry.data_bytes as usize;
            if start + len <= snapshot_bytes.len() {
                let mut blob = snapshot_bytes[start..start + len].to_vec();
                relativize_vamana_blob_offsets(entry.index_type, entry.data_offset, &mut blob);
                writer.add_index(
                    entry.domain_id,
                    entry.relation_id,
                    entry.attribute_index,
                    entry.index_type,
                    &entry.name,
                    blob,
                );
            }
        }
    }

    writer.finalize()?;
    println!(
        "SUCCESS: Dropped {} index(es), kept {} index(es) -> {}",
        dropped_count,
        kept_count,
        output_path.display()
    );
    Ok(())
}

fn replay_snapshot_into_writer(
    reader: &SnapshotReader,
    writer: &mut SnapshotWriter,
) -> Result<(), Box<dyn Error>> {
    if let Ok(meta) = reader.get_metadata() {
        if !meta.is_empty() {
            writer.set_metadata(meta);
        }
    }

    for (d_idx, d) in reader.domains().iter().enumerate() {
        writer.add_domain(d.domain_id, d.key_type, &d.name);
        writer.set_domain_node_count(d.domain_id, d.node_count);
        for (a_idx, a) in d.attributes.iter().enumerate() {
            let data = reader.get_domain_attribute_by_index(d_idx, a_idx)?.to_vec();
            let offs = reader
                .get_domain_attribute_offsets_by_index(d_idx, a_idx)?
                .map(|o| o.to_vec());
            writer.add_attribute_to_domain(
                d.domain_id,
                &a.name,
                a.type_code,
                a.dimension,
                data,
                offs,
            );
        }
        if d.key_attr_index != 0xFFFF {
            writer.set_domain_key_attr(d.domain_id, d.key_attr_index);
        }
    }

    for (r_idx, r) in reader.relations().iter().enumerate() {
        if r.is_virtual() {
            writer.add_virtual_relation(
                r.src_domain_id,
                r.tgt_domain_id,
                r.node_count,
                &r.name,
                r.virtual_component_ids.clone(),
            );
            continue;
        }

        let (row_offsets, col_indices, coo_srcs) = extract_relation_csr_and_coo(reader, r_idx, r)?;
        writer.add_relation(
            r.src_domain_id,
            r.tgt_domain_id,
            r.node_count,
            r.edge_count,
            row_offsets,
            col_indices,
        );
        writer.set_relation_name(r_idx, &r.name);
        writer.set_relation_widths(r_idx, r.node_id_width, r.edge_index_width);
        writer.set_relation_encoding(r_idx, r.encoding_id);
        writer.set_relation_section_features(r_idx, r.section_features & IMP_REL_CARDINALITY_MASK);
        if let Some(srcs) = coo_srcs {
            writer.set_relation_coo_sources(r_idx, srcs);
        }
        if r.has_csc() {
            writer.set_relation_include_csc(r_idx, true);
        }

        for (a_idx, a) in r.attributes.iter().enumerate() {
            let data = reader.get_attribute_data(r_idx, a_idx)?.to_vec();
            let offsets = reader
                .get_attribute_offsets(r_idx, a_idx)?
                .map(|o| o.to_vec());
            writer.add_attribute_to_relation(
                r_idx,
                &a.name,
                a.type_code,
                a.dimension,
                data,
                offsets,
            );
        }
    }

    Ok(())
}

pub(crate) type ExtractedCsrCoo = (Vec<u32>, Vec<u32>, Option<Vec<u32>>);

pub(crate) fn extract_relation_csr_and_coo(
    reader: &SnapshotReader,
    r_idx: usize,
    r: &impulse_graph::RelationInfo,
) -> Result<ExtractedCsrCoo, Box<dyn Error>> {
    let n = r.node_count as usize;
    if r.is_flat_forward() {
        let flat = reader.get_col_indices_as_u32_vec(r_idx)?;
        let sentinel = if r.node_id_width == 2 {
            0xFFFFu32
        } else {
            u32::MAX
        };
        let mut row_offsets = Vec::with_capacity(n + 1);
        let mut col_indices = Vec::with_capacity(r.edge_count as usize);
        row_offsets.push(0u32);
        for &v in flat.iter().take(n) {
            if v != sentinel {
                col_indices.push(v);
            }
            row_offsets.push(col_indices.len() as u32);
        }
        while row_offsets.len() < n + 1 {
            row_offsets.push(col_indices.len() as u32);
        }
        Ok((row_offsets, col_indices, None))
    } else {
        let row_offsets = reader.get_row_offsets(r_idx)?.to_vec();
        let col_indices = reader.get_col_indices_as_u32_vec(r_idx)?;
        Ok((row_offsets, col_indices, None))
    }
}

fn strip_nullable_bitmap(type_code: u8, row_count: usize, raw_data: &[u8]) -> &[u8] {
    if (type_code & 0x80) != 0 {
        let word_count = row_count.div_ceil(64);
        let raw_bm = word_count * 8;
        let padded_bm = if raw_bm == 0 {
            0
        } else {
            (raw_bm + 127) & !127
        };
        if padded_bm <= raw_data.len() {
            return &raw_data[padded_bm..];
        }
    }
    raw_data
}

fn build_and_add_index(
    reader: &SnapshotReader,
    writer: &mut SnapshotWriter,
    spec_str: &str,
) -> Result<(), Box<dyn Error>> {
    // Format: "Target.attr:type[:param=val...]"
    // e.g., "User.age:permutation"
    // e.g., "User.embedding:vamana:R=32:L=64:metric=cosine:quant=sq8:name=User_emb_sq8"
    let parts: Vec<&str> = spec_str.split(':').collect();
    if parts.len() < 2 {
        return Err(format!(
            "Invalid index spec '{}'. Must be 'Target.attr:type[:params]'",
            spec_str
        )
        .into());
    }

    let target_pair: Vec<&str> = parts[0].split('.').collect();
    if target_pair.len() != 2 {
        return Err(format!("Invalid target '{}'. Must be 'Entity.attribute'", parts[0]).into());
    }
    let entity_name = target_pair[0];
    let attr_name = target_pair[1];
    let index_type_str = parts[1].to_lowercase();

    let mut custom_idx_name: Option<String> = None;
    for p in &parts[2..] {
        if let Some((k, v)) = p.split_once('=') {
            if k.eq_ignore_ascii_case("name") && !v.trim().is_empty() {
                custom_idx_name = Some(v.trim().to_string());
            }
        }
    }

    // Check if entity is a Domain or a Relation
    let domain_opt = reader
        .domains()
        .iter()
        .enumerate()
        .find(|(_, d)| d.name == entity_name || d.domain_id.to_string() == entity_name);
    let relation_opt = reader
        .relations()
        .iter()
        .enumerate()
        .find(|(_, r)| r.name == entity_name || r.relation_id.to_string() == entity_name);

    if domain_opt.is_none() && relation_opt.is_none() {
        return Err(format!("Entity '{}' not found in domains or relations", entity_name).into());
    }

    let (domain_id, relation_id, attr_idx, attr_info, attr_payload, attr_offsets, elem_row_count) =
        if let Some((dom_idx, dom)) = domain_opt {
            // 1. Spec v0.10.0 inline DomainCatalogEntry attributes
            let mut found = None;
            for (a_idx, a) in dom.attributes.iter().enumerate() {
                if a.name == attr_name {
                    let raw = reader.get_domain_attribute_by_index(dom_idx, a_idx)?;
                    let offs = reader.get_domain_attribute_offsets_by_index(dom_idx, a_idx)?;
                    let payload = strip_nullable_bitmap(a.type_code, dom.node_count as usize, raw);
                    found = Some((
                        dom.domain_id,
                        0xFFFFu16,
                        a_idx as u16,
                        a.clone(),
                        payload,
                        offs,
                        dom.node_count as usize,
                    ));
                    break;
                }
            }
            // 2. Fallback to v0.9.0 carrier relations
            if found.is_none() {
                for (r_idx, r) in reader.relations().iter().enumerate() {
                    if r.src_domain_id == dom.domain_id {
                        for (a_idx, a) in r.attributes.iter().enumerate() {
                            if a.name == attr_name {
                                let raw = reader.get_attribute_data(r_idx, a_idx)?;
                                let offs = reader.get_attribute_offsets(r_idx, a_idx)?;
                                let payload = strip_nullable_bitmap(
                                    a.type_code,
                                    dom.node_count as usize,
                                    raw,
                                );
                                found = Some((
                                    dom.domain_id,
                                    0xFFFFu16,
                                    a_idx as u16,
                                    a.clone(),
                                    payload,
                                    offs,
                                    dom.node_count as usize,
                                ));
                                break;
                            }
                        }
                    }
                    if found.is_some() {
                        break;
                    }
                }
            }
            found.ok_or_else(|| {
                format!("Domain attribute '{}.{}' not found", entity_name, attr_name)
            })?
        } else {
            let (r_idx, rel) = relation_opt.unwrap();
            let a_idx = rel
                .attributes
                .iter()
                .position(|a| a.name == attr_name)
                .ok_or_else(|| {
                    format!(
                        "Relation attribute '{}.{}' not found",
                        entity_name, attr_name
                    )
                })?;
            let a = rel.attributes[a_idx].clone();
            let raw = reader.get_attribute_data(r_idx, a_idx)?;
            let offs = reader.get_attribute_offsets(r_idx, a_idx)?;
            let payload = strip_nullable_bitmap(a.type_code, rel.edge_count as usize, raw);
            (
                rel.src_domain_id,
                rel.relation_id,
                a_idx as u16,
                a,
                payload,
                offs,
                rel.edge_count as usize,
            )
        };

    let idx_name = custom_idx_name
        .unwrap_or_else(|| format!("{}_{}_{}", entity_name, attr_name, index_type_str));
    println!(
        "  Building {} index '{}' for {}.{}...",
        index_type_str, idx_name, entity_name, attr_name
    );

    match index_type_str.as_str() {
        "permutation" | "0x01" | "1" => {
            let elem_count = elem_row_count.min(get_element_count(&attr_info, attr_payload.len()));
            let mut out_ptr: *mut std::ffi::c_void = std::ptr::null_mut();
            let mut out_size: usize = 0;
            let status = unsafe {
                impulse_index_build_permutation(
                    attr_payload.as_ptr() as *const std::ffi::c_void,
                    elem_count,
                    attr_info.type_code,
                    &mut out_ptr,
                    &mut out_size,
                )
            };
            if status != 0 || out_ptr.is_null() {
                return Err(format!("Failed to build permutation index, status code {}", status).into());
            }
            let blob = unsafe { std::slice::from_raw_parts(out_ptr as *const u8, out_size) }.to_vec();
            unsafe { impulse_index_free(out_ptr); }
            writer.add_index(domain_id, relation_id, attr_idx, IMP_INDEX_PERMUTATION, &idx_name, blob);
        }
        "zonemap" | "0x02" | "2" => {
            let elem_count = elem_row_count.min(get_element_count(&attr_info, attr_payload.len()));
            let mut out_ptr: *mut std::ffi::c_void = std::ptr::null_mut();
            let mut out_size: usize = 0;
            let status = unsafe {
                impulse_index_build_zone_map(
                    attr_payload.as_ptr() as *const std::ffi::c_void,
                    elem_count,
                    attr_info.type_code,
                    &mut out_ptr,
                    &mut out_size,
                )
            };
            if status != 0 || out_ptr.is_null() {
                return Err(format!("Failed to build zonemap index, status code {}", status).into());
            }
            let blob = unsafe { std::slice::from_raw_parts(out_ptr as *const u8, out_size) }.to_vec();
            unsafe { impulse_index_free(out_ptr); }
            writer.add_index(domain_id, relation_id, attr_idx, IMP_INDEX_ZONE_MAP, &idx_name, blob);
        }
        "inverted_bitset" | "0x03" | "3" => {
            let c_strings = decode_var_string_c_strings(attr_payload, attr_offsets)?;
            let c_ptrs: Vec<*const std::os::raw::c_char> = c_strings.iter().map(|cs| cs.as_ptr()).collect();
            let mut out_ptr: *mut std::ffi::c_void = std::ptr::null_mut();
            let mut out_size: usize = 0;
            let status = unsafe {
                impulse_index_build_inverted_bitset(
                    c_ptrs.as_ptr(),
                    c_ptrs.len(),
                    &mut out_ptr,
                    &mut out_size,
                )
            };
            if status != 0 || out_ptr.is_null() {
                return Err(format!("Failed to build inverted bitset index, status code {}", status).into());
            }
            let blob = unsafe { std::slice::from_raw_parts(out_ptr as *const u8, out_size) }.to_vec();
            unsafe { impulse_index_free(out_ptr); }
            writer.add_index(domain_id, relation_id, attr_idx, IMP_INDEX_INVERTED_BITSET, &idx_name, blob);
        }
        "mphf" | "0x04" | "4" => {
            let c_strings = decode_var_string_c_strings(attr_payload, attr_offsets)?;
            let c_ptrs: Vec<*const std::os::raw::c_char> = c_strings.iter().map(|cs| cs.as_ptr()).collect();
            let mut out_ptr: *mut std::ffi::c_void = std::ptr::null_mut();
            let mut out_size: usize = 0;
            let status = unsafe {
                impulse_index_build_minimal_perfect_hash(
                    c_ptrs.as_ptr(),
                    c_ptrs.len(),
                    &mut out_ptr,
                    &mut out_size,
                )
            };
            if status != 0 || out_ptr.is_null() {
                return Err(format!("Failed to build MPHF index, status code {}", status).into());
            }
            let blob = unsafe { std::slice::from_raw_parts(out_ptr as *const u8, out_size) }.to_vec();
            unsafe { impulse_index_free(out_ptr); }
            writer.add_index(domain_id, relation_id, attr_idx, IMP_INDEX_MINIMAL_PERFECT_HASH, &idx_name, blob);
        }
        "trigram" | "0x05" | "5" => {
            let c_strings = decode_var_string_c_strings(attr_payload, attr_offsets)?;
            let c_ptrs: Vec<*const std::os::raw::c_char> = c_strings.iter().map(|cs| cs.as_ptr()).collect();
            let mut out_ptr: *mut std::ffi::c_void = std::ptr::null_mut();
            let mut out_size: usize = 0;
            let status = unsafe {
                impulse_index_build_trigram(
                    c_ptrs.as_ptr(),
                    c_ptrs.len(),
                    &mut out_ptr,
                    &mut out_size,
                )
            };
            if status != 0 || out_ptr.is_null() {
                return Err(format!("Failed to build trigram index, status code {}", status).into());
            }
            let blob = unsafe { std::slice::from_raw_parts(out_ptr as *const u8, out_size) }.to_vec();
            unsafe { impulse_index_free(out_ptr); }
            writer.add_index(domain_id, relation_id, attr_idx, IMP_INDEX_TRIGRAM_3GRAM, &idx_name, blob);
        }
        "domain_split" | "0x06" | "6" => {
            let c_strings = decode_var_string_c_strings(attr_payload, attr_offsets)?;
            let c_ptrs: Vec<*const std::os::raw::c_char> = c_strings.iter().map(|cs| cs.as_ptr()).collect();
            let mut out_ptr: *mut std::ffi::c_void = std::ptr::null_mut();
            let mut out_size: usize = 0;
            let status = unsafe {
                impulse_index_build_domain_split(
                    c_ptrs.as_ptr(),
                    c_ptrs.len(),
                    &mut out_ptr,
                    &mut out_size,
                )
            };
            if status != 0 || out_ptr.is_null() {
                return Err(format!("Failed to build domain split index, status code {}", status).into());
            }
            let blob = unsafe { std::slice::from_raw_parts(out_ptr as *const u8, out_size) }.to_vec();
            unsafe { impulse_index_free(out_ptr); }
            writer.add_index(domain_id, relation_id, attr_idx, IMP_INDEX_DOMAIN_SPLIT_BITSET, &idx_name, blob);
        }
        "temporal_interval" | "0x07" | "7" => {
            let elem_count = elem_row_count.min(get_element_count(&attr_info, attr_payload.len()));
            let mut out_ptr: *mut std::ffi::c_void = std::ptr::null_mut();
            let mut out_size: usize = 0;
            let status = unsafe {
                impulse_index_build_temporal_interval(
                    attr_payload.as_ptr() as *const std::ffi::c_void,
                    elem_count,
                    attr_info.type_code,
                    &mut out_ptr,
                    &mut out_size,
                )
            };
            if status != 0 || out_ptr.is_null() {
                return Err(format!("Failed to build temporal interval index, status code {}", status).into());
            }
            let blob = unsafe { std::slice::from_raw_parts(out_ptr as *const u8, out_size) }.to_vec();
            unsafe { impulse_index_free(out_ptr); }
            writer.add_index(domain_id, relation_id, attr_idx, IMP_INDEX_TEMPORAL_INTERVAL, &idx_name, blob);
        }
        "vamana" | "0x08" | "8" => {
            let mut r = 32u32;
            let mut l = 64u32;
            let mut alpha = 1.2f32;
            let mut metric = VamanaMetric::Cosine;
            let mut quant = VamanaQuantType::Sq8;

            for p in &parts[2..] {
                let kv: Vec<&str> = p.split('=').collect();
                if kv.len() == 2 {
                    match kv[0].to_lowercase().as_str() {
                        "r" => r = kv[1].parse().unwrap_or(32),
                        "l" | "ef_construction" => l = kv[1].parse().unwrap_or(64),
                        "alpha" => alpha = kv[1].parse().unwrap_or(1.2),
                        "metric" => metric = match kv[1].to_lowercase().as_str() {
                            "l2" | "euclidean" => VamanaMetric::L2,
                            "dot" | "ip" => VamanaMetric::Dot,
                            "hamming" => VamanaMetric::Hamming,
                            _ => VamanaMetric::Cosine,
                        },
                        "quant" => quant = match kv[1].to_lowercase().as_str() {
                            "none" | "f32" => VamanaQuantType::None,
                            "bq1" | "binary" => VamanaQuantType::Bq1,
                            "fp16" | "half" => VamanaQuantType::Fp16,
                            _ => VamanaQuantType::Sq8,
                        },
                        _ => {}
                    }
                }
            }

            let cfg = VamanaBuildConfig {
                max_degree_r: r,
                ef_construction: l,
                alpha,
                metric,
                quant_type: quant,
                seed: 42,
            };

            let dim = attr_info.dimension as usize;
            if dim == 0 {
                return Err("Vamana index requires vector dimension > 0".into());
            }
            let total_floats = attr_payload.len() / 4;
            let n = total_floats / dim;
            let mut vectors = Vec::with_capacity(total_floats);
            for chunk in attr_payload.as_chunks::<4>().0 {
                vectors.push(f32::from_le_bytes(*chunk));
            }

            let built = build_vamana_index(domain_id, attr_idx, &vectors, n, dim, &cfg)?;
            writer.add_built_vamana_index_with_relation(&idx_name, relation_id, built);
        }
        _ => return Err(format!("Unsupported index type '{}'. Supported: permutation, zonemap, inverted_bitset, mphf, trigram, domain_split, temporal_interval, vamana", index_type_str).into()),
    }

    Ok(())
}

fn relativize_vamana_blob_offsets(index_type: u8, base_offset: u64, blob: &mut [u8]) {
    if index_type == IMP_INDEX_VAMANA_ANN && blob.len() >= 64 {
        let abs_adj = u64::from_le_bytes(blob[40..48].try_into().unwrap());
        if abs_adj >= base_offset {
            blob[40..48].copy_from_slice(&(abs_adj - base_offset).to_le_bytes());
        }
        let abs_quant = u64::from_le_bytes(blob[56..64].try_into().unwrap());
        if abs_quant >= base_offset && abs_quant > 0 {
            blob[56..64].copy_from_slice(&(abs_quant - base_offset).to_le_bytes());
        }
    }
}

fn get_element_count(attr: &impulse_graph::AttributeInfo, data_bytes: usize) -> usize {
    let base_t = attr.type_code & IMPULSE_TYPE_MASK;
    let elem_size = match base_t {
        0x01 => 1,
        0x02 | 0x05 => 2,
        0x03 | 0x06 => 4,
        0x04 | 0x07 | 0x08 | 0x09 => 8,
        0x0D => 8,
        0x0E => 16,
        _ => 4,
    };
    let dim = attr.dimension.max(1) as usize;
    data_bytes / (elem_size * dim)
}

fn decode_var_string_c_strings(
    data: &[u8],
    offsets: Option<&[u32]>,
) -> Result<Vec<CString>, Box<dyn Error>> {
    let mut result = Vec::new();
    if let Some(offs) = offsets {
        for i in 0..offs.len().saturating_sub(1) {
            let s = offs[i] as usize;
            let e = offs[i + 1] as usize;
            if s <= e && e <= data.len() {
                let slice = &data[s..e];
                let cs = CString::new(slice).unwrap_or_else(|_| CString::new("").unwrap());
                result.push(cs);
            } else {
                result.push(CString::new("").unwrap());
            }
        }
    } else {
        // Fallback: single null-terminated or empty
        result.push(CString::new("").unwrap());
    }
    Ok(result)
}

fn matches_index_type(type_code: u8, filter: &str) -> bool {
    let clean = filter.trim().to_lowercase();
    if let Ok(num) = clean.parse::<u8>() {
        return type_code == num;
    }
    if clean.starts_with("0x") {
        if let Ok(num) = u8::from_str_radix(clean.trim_start_matches("0x"), 16) {
            return type_code == num;
        }
    }
    match clean.as_str() {
        "permutation" => type_code == IMP_INDEX_PERMUTATION,
        "zonemap" | "zone_map" => type_code == IMP_INDEX_ZONE_MAP,
        "inverted_bitset" | "bitset" => type_code == IMP_INDEX_INVERTED_BITSET,
        "mphf" | "minimal_perfect_hash" => type_code == IMP_INDEX_MINIMAL_PERFECT_HASH,
        "trigram" | "3gram" => type_code == IMP_INDEX_TRIGRAM_3GRAM,
        "domain_split" => type_code == IMP_INDEX_DOMAIN_SPLIT_BITSET,
        "temporal_interval" | "temporal" => type_code == IMP_INDEX_TEMPORAL_INTERVAL,
        "vamana" | "vamana_ann" | "ann" | "vector" => type_code == IMP_INDEX_VAMANA_ANN,
        _ => index_type_to_string(type_code)
            .to_lowercase()
            .contains(&clean),
    }
}

fn index_type_to_string(type_code: u8) -> &'static str {
    match type_code {
        IMP_INDEX_PERMUTATION => "Permutation (0x01)",
        IMP_INDEX_ZONE_MAP => "ZoneMap (0x02)",
        IMP_INDEX_INVERTED_BITSET => "InvertedBitSet (0x03)",
        IMP_INDEX_MINIMAL_PERFECT_HASH => "MPHF (0x04)",
        IMP_INDEX_TRIGRAM_3GRAM => "Trigram (0x05)",
        IMP_INDEX_DOMAIN_SPLIT_BITSET => "DomainSplit (0x06)",
        IMP_INDEX_TEMPORAL_INTERVAL => "TemporalInterval (0x07)",
        IMP_INDEX_VAMANA_ANN => "VamanaANN (0x08)",
        _ => "Unknown",
    }
}
