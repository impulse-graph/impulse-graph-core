use comfy_table::Table;
use impulse_graph::spec::IMPULSE_TYPE_MASK;
use impulse_graph::SnapshotReader;
use serde_json::{json, Value};
use std::error::Error;
use std::path::Path;

pub struct SampleOptions<'a> {
    pub domain: Option<&'a str>,
    pub relation: Option<&'a str>,
    pub limit: usize,
    pub offset: usize,
    pub format: &'a str,
}

pub fn run(
    file: &Path,
    domain_filter: Option<&str>,
    relation_filter: Option<&str>,
    limit: usize,
    offset: usize,
    format: &str,
) -> Result<(), Box<dyn Error>> {
    let opts = SampleOptions {
        domain: domain_filter,
        relation: relation_filter,
        limit,
        offset,
        format,
    };
    run_with_options(file, &opts)
}

pub fn run_with_options(file: &Path, opts: &SampleOptions<'_>) -> Result<(), Box<dyn Error>> {
    let reader = SnapshotReader::open(file)?;
    let is_json = opts.format.eq_ignore_ascii_case("json");

    // Case 1: Sample specified domain, or if neither domain nor relation specified and domains exist
    let should_sample_domain =
        opts.domain.is_some() || (opts.relation.is_none() && reader.domain_count() > 0);
    // Case 2: Sample specified relation, or if neither domain nor relation specified and relations exist
    let should_sample_relation =
        opts.relation.is_some() || (opts.domain.is_none() && reader.relation_count() > 0);

    let mut json_results = json!({});

    if should_sample_domain {
        let domain_target = opts.domain.unwrap_or_else(|| {
            reader
                .domains()
                .first()
                .map(|d| d.name.as_str())
                .unwrap_or("")
        });

        let dom_opt = reader.domains().iter().enumerate().find(|(_, d)| {
            d.name.eq_ignore_ascii_case(domain_target) || d.domain_id.to_string() == domain_target
        });

        if let Some((d_idx, d)) = dom_opt {
            let n = d.node_count as usize;
            let start = opts.offset.min(n);
            let end = (start + opts.limit).min(n);

            // Fetch string keys if available
            let mut keys = Vec::new();
            if let Ok(data) = reader.get_domain_attribute_data(d.domain_id, "_key") {
                if let Ok(Some(offsets)) = reader.get_domain_attribute_offsets(d.domain_id, "_key")
                {
                    for i in start..end {
                        if i + 1 < offsets.len() {
                            let s = offsets[i] as usize;
                            let e = offsets[i + 1] as usize;
                            if s <= e && e <= data.len() {
                                keys.push(
                                    std::str::from_utf8(&data[s..e]).unwrap_or("").to_string(),
                                );
                            } else {
                                keys.push(i.to_string());
                            }
                        } else {
                            keys.push(i.to_string());
                        }
                    }
                }
            }
            if keys.len() != (end - start) {
                keys = (start..end).map(|i| i.to_string()).collect();
            }

            // Collect node attributes (Spec v0.10.0 inline domain attributes first, then v0.9.0 carrier fallback)
            let mut attr_cols = Vec::new();
            for (a_idx, a) in d.attributes.iter().enumerate() {
                if a.name == "_key" {
                    continue;
                }
                if let Ok(data) = reader.get_domain_attribute_by_index(d_idx, a_idx) {
                    let offsets = reader
                        .get_domain_attribute_offsets_by_index(d_idx, a_idx)
                        .ok()
                        .flatten();
                    attr_cols.push((a.clone(), data, offsets));
                }
            }
            if attr_cols.is_empty() {
                let expected_carrier_name = format!("{}_nodes", d.name);
                for r in reader.relations() {
                    let is_carrier = (r.name == expected_carrier_name
                        || r.name == format!("Domain_{}_nodes", d.domain_id)
                        || (r.name.ends_with("_nodes") && r.edge_count == 0))
                        && r.src_domain_id == d.domain_id
                        && r.tgt_domain_id == d.domain_id;
                    if is_carrier {
                        for (a_idx, a) in r.attributes.iter().enumerate() {
                            if a.name == "_key" {
                                continue;
                            }
                            if let Ok(data) =
                                reader.get_attribute_data(r.relation_id as usize, a_idx)
                            {
                                let offsets = reader
                                    .get_attribute_offsets(r.relation_id as usize, a_idx)
                                    .ok()
                                    .flatten();
                                attr_cols.push((a.clone(), data, offsets));
                            }
                        }
                    }
                }
            }

            if is_json {
                let mut rows = Vec::new();
                for (row_idx, dense_id) in (start..end).enumerate() {
                    let mut row_obj = serde_json::Map::new();
                    row_obj.insert("dense_id".to_string(), json!(dense_id));
                    row_obj.insert("key".to_string(), json!(keys[row_idx]));

                    for (attr, data, offsets) in &attr_cols {
                        let val = format_attribute_val(attr, data, *offsets, dense_id, n);
                        row_obj.insert(attr.name.clone(), val);
                    }
                    rows.push(Value::Object(row_obj));
                }
                json_results["domain"] = json!({
                    "domain_id": d.domain_id,
                    "name": d.name,
                    "total_nodes": n,
                    "offset": start,
                    "limit": opts.limit,
                    "sample_count": end - start,
                    "rows": rows,
                });
            } else {
                println!(
                    "=== DOMAIN SAMPLE: '{}' (ID: {}, Total Nodes: {}) ===",
                    d.name, d.domain_id, n
                );
                println!(
                    "Displaying dense IDs {}..{} (Limit: {}, Offset: {})",
                    start, end, opts.limit, opts.offset
                );

                let mut table = Table::new();
                let mut headers = vec!["dense_id".to_string(), "key".to_string()];
                for (attr, _, _) in &attr_cols {
                    headers.push(attr.name.clone());
                }
                table.set_header(headers);

                for (row_idx, dense_id) in (start..end).enumerate() {
                    let mut row = vec![dense_id.to_string(), keys[row_idx].clone()];
                    for (attr, data, offsets) in &attr_cols {
                        let val = format_attribute_val(attr, data, *offsets, dense_id, n);
                        row.push(val_to_string(&val));
                    }
                    table.add_row(row);
                }
                println!("{}", table);
                println!();
            }
        } else if opts.domain.is_some() {
            return Err(format!("Domain '{}' not found in snapshot", domain_target).into());
        }
    }

    if should_sample_relation {
        let rel_target = opts.relation.unwrap_or_else(|| {
            reader
                .relations()
                .first()
                .map(|r| r.name.as_str())
                .unwrap_or("")
        });

        let rel_opt = reader.relations().iter().enumerate().find(|(r_idx, r)| {
            r.name.eq_ignore_ascii_case(rel_target)
                || r.relation_id.to_string() == rel_target
                || r_idx.to_string() == rel_target
        });

        if let Some((r_idx, r)) = rel_opt {
            let (row_offsets_vec, col_indices_vec, _) = if r.is_virtual() {
                (vec![0u32; r.node_count as usize + 1], Vec::new(), None)
            } else {
                crate::commands::index::extract_relation_csr_and_coo(&reader, r_idx, r)?
            };
            let row_offsets = row_offsets_vec.as_slice();
            let col_indices = col_indices_vec.as_slice();
            let m = col_indices.len();
            let start = opts.offset.min(m);
            let end = (start + opts.limit).min(m);

            // Source domain name & keys
            let src_dom = reader
                .domains()
                .iter()
                .find(|d| d.domain_id == r.src_domain_id);
            let tgt_dom = reader
                .domains()
                .iter()
                .find(|d| d.domain_id == r.tgt_domain_id);
            let src_dom_name = src_dom.map(|d| d.name.as_str()).unwrap_or("?");
            let tgt_dom_name = tgt_dom.map(|d| d.name.as_str()).unwrap_or("?");

            // Collect edge attributes
            let mut attr_cols = Vec::new();
            for (a_idx, a) in r.attributes.iter().enumerate() {
                if let Ok(data) = reader.get_attribute_data(r_idx, a_idx) {
                    let offsets = reader.get_attribute_offsets(r_idx, a_idx).ok().flatten();
                    attr_cols.push((a.clone(), data, offsets));
                }
            }

            if is_json {
                let mut rows = Vec::new();
                for (edge_idx, &tgt_id) in col_indices.iter().enumerate().take(end).skip(start) {
                    let src_id = find_source_node(edge_idx, row_offsets);

                    let mut row_obj = serde_json::Map::new();
                    row_obj.insert("edge_idx".to_string(), json!(edge_idx));
                    row_obj.insert("src_id".to_string(), json!(src_id));
                    row_obj.insert("tgt_id".to_string(), json!(tgt_id));

                    for (attr, data, offsets) in &attr_cols {
                        let val = format_attribute_val(attr, data, *offsets, edge_idx, m);
                        row_obj.insert(attr.name.clone(), val);
                    }
                    rows.push(Value::Object(row_obj));
                }
                json_results["relation"] = json!({
                    "relation_id": r.relation_id,
                    "name": r.name,
                    "src_domain": src_dom_name,
                    "tgt_domain": tgt_dom_name,
                    "total_edges": m,
                    "offset": start,
                    "limit": opts.limit,
                    "sample_count": end - start,
                    "rows": rows,
                });
            } else {
                println!(
                    "=== RELATION SAMPLE: '{}' (ID: {}, {} -> {}, Total Edges: {}) ===",
                    r.name, r.relation_id, src_dom_name, tgt_dom_name, m
                );
                println!(
                    "Displaying edge indices {}..{} (Limit: {}, Offset: {})",
                    start, end, opts.limit, opts.offset
                );

                let mut table = Table::new();
                let mut headers = vec![
                    "edge_idx".to_string(),
                    "src_id".to_string(),
                    "tgt_id".to_string(),
                ];
                for (attr, _, _) in &attr_cols {
                    headers.push(attr.name.clone());
                }
                table.set_header(headers);

                for (edge_idx, &tgt_id) in col_indices.iter().enumerate().take(end).skip(start) {
                    let src_id = find_source_node(edge_idx, row_offsets);
                    let mut row =
                        vec![edge_idx.to_string(), src_id.to_string(), tgt_id.to_string()];
                    for (attr, data, offsets) in &attr_cols {
                        let val = format_attribute_val(attr, data, *offsets, edge_idx, m);
                        row.push(val_to_string(&val));
                    }
                    table.add_row(row);
                }
                println!("{}", table);
                println!();
            }
        } else if opts.relation.is_some() {
            return Err(format!("Relation '{}' not found in snapshot", rel_target).into());
        }
    }

    if is_json {
        println!("{}", serde_json::to_string_pretty(&json_results)?);
    }

    Ok(())
}

fn find_source_node(edge_idx: usize, row_offsets: &[u32]) -> u32 {
    let edge_u32 = edge_idx as u32;
    match row_offsets.binary_search(&edge_u32) {
        Ok(idx) => {
            // idx is where row_offsets[idx] == edge_u32
            // If multiple rows have the same offset (empty rows), find the right non-empty one
            let mut u = idx;
            while u + 1 < row_offsets.len() && row_offsets[u + 1] <= edge_u32 {
                u += 1;
            }
            u as u32
        }
        Err(idx) => {
            // idx is insertion point, so row_offsets[idx-1] < edge_u32 < row_offsets[idx]
            idx.saturating_sub(1) as u32
        }
    }
}

fn format_attribute_val(
    attr: &impulse_graph::AttributeInfo,
    raw_data: &[u8],
    offsets: Option<&[u32]>,
    row_idx: usize,
    total_rows: usize,
) -> Value {
    let base_t = attr.type_code & IMPULSE_TYPE_MASK;
    let is_nullable = (attr.type_code & 0x80) != 0;

    let data = if is_nullable {
        let word_count = total_rows.div_ceil(64);
        let bitmap_bytes = word_count * 8;
        let padded_bm = if bitmap_bytes == 0 {
            0
        } else {
            (bitmap_bytes + 127) & !127
        };
        let byte_pos = row_idx / 8;
        let bit_pos = row_idx % 8;
        if byte_pos < bitmap_bytes
            && byte_pos < raw_data.len()
            && (raw_data[byte_pos] & (1 << bit_pos)) == 0
        {
            return Value::Null;
        }
        if padded_bm <= raw_data.len() {
            &raw_data[padded_bm..]
        } else {
            raw_data
        }
    } else {
        raw_data
    };

    if base_t == 0x0B {
        // Var string
        if let Some(offs) = offsets {
            if row_idx + 1 < offs.len() {
                let s = offs[row_idx] as usize;
                let e = offs[row_idx + 1] as usize;
                if s <= e && e <= data.len() {
                    let str_val = std::str::from_utf8(&data[s..e]).unwrap_or("");
                    return json!(str_val);
                }
            }
        }
        return json!("");
    }

    if attr.dimension > 1 && base_t == 0x06 {
        // Vector embedding preview
        let dim = attr.dimension as usize;
        let start = row_idx * dim * 4;
        let end = start + dim * 4;
        if end <= data.len() {
            let mut vec_vals = Vec::with_capacity(dim.min(5));
            for i in 0..dim.min(5) {
                let byte_off = start + i * 4;
                let f =
                    f32::from_le_bytes(data[byte_off..byte_off + 4].try_into().unwrap_or([0; 4]));
                vec_vals.push(f);
            }
            if dim > 5 {
                return json!(format!(
                    "[{:.3}, {:.3}, ... (dim={})]",
                    vec_vals[0], vec_vals[1], dim
                ));
            } else {
                return json!(vec_vals);
            }
        }
        return json!([]);
    }

    match base_t {
        0x01 => {
            if row_idx < data.len() {
                json!(data[row_idx] as i8)
            } else {
                if is_nullable {
                    Value::Null
                } else {
                    json!(0)
                }
            }
        }
        0x02 => {
            let start = row_idx * 2;
            if start + 2 <= data.len() {
                let v = i16::from_le_bytes(data[start..start + 2].try_into().unwrap_or([0; 2]));
                json!(v)
            } else {
                if is_nullable {
                    Value::Null
                } else {
                    json!(0)
                }
            }
        }
        0x03 => {
            let start = row_idx * 4;
            if start + 4 <= data.len() {
                let v = i32::from_le_bytes(data[start..start + 4].try_into().unwrap_or([0; 4]));
                json!(v)
            } else {
                if is_nullable {
                    Value::Null
                } else {
                    json!(0)
                }
            }
        }
        0x04 | 0x08 | 0x09 => {
            let start = row_idx * 8;
            if start + 8 <= data.len() {
                let v = i64::from_le_bytes(data[start..start + 8].try_into().unwrap_or([0; 8]));
                json!(v)
            } else {
                if is_nullable {
                    Value::Null
                } else {
                    json!(0)
                }
            }
        }
        0x06 => {
            let start = row_idx * 4;
            if start + 4 <= data.len() {
                let v = f32::from_le_bytes(data[start..start + 4].try_into().unwrap_or([0; 4]));
                json!(v)
            } else {
                if is_nullable {
                    Value::Null
                } else {
                    json!(0.0)
                }
            }
        }
        0x07 => {
            let start = row_idx * 8;
            if start + 8 <= data.len() {
                let v = f64::from_le_bytes(data[start..start + 8].try_into().unwrap_or([0; 8]));
                json!(v)
            } else {
                if is_nullable {
                    Value::Null
                } else {
                    json!(0.0)
                }
            }
        }
        _ => json!(""),
    }
}

fn val_to_string(v: &Value) -> String {
    match v {
        Value::String(s) => s.clone(),
        Value::Number(n) => n.to_string(),
        Value::Bool(b) => b.to_string(),
        Value::Null => "NULL".to_string(),
        Value::Array(_) | Value::Object(_) => v.to_string(),
    }
}
