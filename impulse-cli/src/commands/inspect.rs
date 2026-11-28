use comfy_table::Table;
use impulse_graph::spec::{
    VamanaMetric, VamanaQuantType, IMPULSE_FEAT_4KB_PAGE_ALIGNED, IMPULSE_FEAT_CRYPTO_SIGNED,
    IMP_INDEX_VAMANA_ANN,
};
use impulse_graph::SnapshotReader;
use serde_json::json;
use std::error::Error;
use std::path::Path;

#[derive(Debug, Clone, Default)]
pub struct InspectOptions<'a> {
    pub format: &'a str,
    pub verbose: bool,
    pub anonymize: bool,
    pub domain_filter: Option<&'a str>,
    pub relation_filter: Option<&'a str>,
    pub page_map: bool,
}

pub fn run(file: &Path, format: &str, verbose: bool) -> Result<(), Box<dyn Error>> {
    run_with_options(
        file,
        &InspectOptions {
            format,
            verbose,
            anonymize: false,
            domain_filter: None,
            relation_filter: None,
            page_map: false,
        },
    )
}

pub fn run_with_options(file: &Path, opts: &InspectOptions<'_>) -> Result<(), Box<dyn Error>> {
    let reader = SnapshotReader::open(file)?;
    let file_size = reader.file_size_bytes();
    let header = reader.header();

    if opts.page_map {
        return render_page_map(&reader, file_size, opts.format, file, opts.anonymize);
    }

    if opts.format.eq_ignore_ascii_case("json") {
        let output = build_inspect_json(&reader, file, opts);
        println!("{}", serde_json::to_string_pretty(&output)?);
        return Ok(());
    }

    let domain_matches = |d_idx: usize, d: &impulse_graph::DomainInfo, anon_name: &str| -> bool {
        if let Some(f) = opts.domain_filter {
            d.name.eq_ignore_ascii_case(f)
                || d.domain_id.to_string() == f
                || anon_name.eq_ignore_ascii_case(f)
                || format!("Domain_{}", d_idx + 1).eq_ignore_ascii_case(f)
        } else {
            true
        }
    };

    let relation_matches =
        |r_idx: usize, r: &impulse_graph::RelationInfo, anon_name: &str| -> bool {
            if let Some(rf) = opts.relation_filter {
                if !r.name.eq_ignore_ascii_case(rf)
                    && r.relation_id.to_string() != rf
                    && !anon_name.eq_ignore_ascii_case(rf)
                    && !format!("Relation_{}", r_idx + 1).eq_ignore_ascii_case(rf)
                {
                    return false;
                }
            }
            if let Some(df) = opts.domain_filter {
                let matches_src_tgt = reader.domains().iter().enumerate().any(|(d_idx, d)| {
                    (d.domain_id == r.src_domain_id || d.domain_id == r.tgt_domain_id)
                        && (d.name.eq_ignore_ascii_case(df)
                            || d.domain_id.to_string() == df
                            || format!("Domain_{}", d_idx + 1).eq_ignore_ascii_case(df))
                });
                if !matches_src_tgt {
                    return false;
                }
            }
            true
        };

    println!("=========================================================================");
    println!("               IMPULSE GRAPH ENGINE SNAPSHOT INSPECTOR                   ");
    println!("=========================================================================");
    println!("File:              {}", file.display());
    println!("File Size:         {} bytes", file_size);
    println!("SHA-256:           {}", reader.sha256_hex());
    println!("Magic:             0x{:08X} (IMPS)", header.magic());
    let version_str = match header.version() {
        11 => "0.11.0 (0x000B)".to_string(),
        10 => "0.10.0 (0x000A)".to_string(),
        9 => "0.9.0 (0x0009)".to_string(),
        v => format!("0.{}.0 (0x{:04X})", v, v),
    };
    println!("Version:           {}", version_str);
    println!("Timestamp (ms):    {}", header.timestamp_ms());
    println!("UUID:              {}", header.snapshot_uuid_hex());
    println!(
        "Required Features: {}",
        format_required_features(header.required_features())
    );
    println!("Domains:           {}", header.domain_count());
    println!("Relations:         {}", header.relation_count());
    println!("Indices:           {}", reader.index_count());
    println!();

    // Domain Table
    let mut dom_table = Table::new();
    dom_table.set_header(vec![
        "ID",
        "Domain Name",
        "Key Type",
        "Dense Width",
        "Node Count",
        "Attributes",
        "Key Attr",
    ]);
    for (d_idx, d) in reader.domains().iter().enumerate() {
        let d_name = if opts.anonymize {
            format!("Domain_{}", d_idx + 1)
        } else {
            d.name.clone()
        };
        if !domain_matches(d_idx, d, &d_name) {
            continue;
        }
        let key_attr_str = if d.key_attr_index == 0xFFFF {
            "-".to_string()
        } else if let Some(ka) = d.attributes.get(d.key_attr_index as usize) {
            if opts.anonymize {
                format!("#{}", d.key_attr_index)
            } else {
                format!("{} (#{})", ka.name, d.key_attr_index)
            }
        } else {
            format!("#{}", d.key_attr_index)
        };
        dom_table.add_row(vec![
            d.domain_id.to_string(),
            d_name,
            format!("{:?}", d.key_type),
            d.dense_id_width.to_string(),
            d.node_count.to_string(),
            d.attributes.len().to_string(),
            key_attr_str,
        ]);
    }
    println!("--- DOMAIN CATALOG ---");
    println!("{}", dom_table);
    println!();

    // Relation Table
    let mut rel_table = Table::new();
    rel_table.set_header(vec![
        "ID",
        "Relation Name",
        "Src Domain",
        "Tgt Domain",
        "Encoding ID",
        "Nodes",
        "Edges",
        "Attributes",
        "Avg Degree",
    ]);
    for (r_idx, r) in reader.relations().iter().enumerate() {
        let r_name = if opts.anonymize {
            format!("Relation_{}", r_idx + 1)
        } else if r.name.is_empty() {
            format!("rel_{}_{}", r.src_domain_id, r.tgt_domain_id)
        } else {
            r.name.clone()
        };
        if !relation_matches(r_idx, r, &r_name) {
            continue;
        }
        let avg_deg = if r.node_count > 0 {
            r.edge_count as f64 / r.node_count as f64
        } else {
            0.0
        };
        rel_table.add_row(vec![
            r.relation_id.to_string(),
            r_name,
            r.src_domain_id.to_string(),
            r.tgt_domain_id.to_string(),
            format!("0x{:02X}", r.encoding_id),
            r.node_count.to_string(),
            r.edge_count.to_string(),
            r.attributes.len().to_string(),
            format!("{:.2}", avg_deg),
        ]);
    }
    println!("--- RELATION CATALOG & TOPOLOGY ---");
    println!("{}", rel_table);

    // Section 2.6 Secondary Index Directory Table
    if !reader.index_entries().is_empty() {
        println!();
        println!("--- SECTION 2.6 INDEX DIRECTORY TABLE ---");
        let mut idx_table = Table::new();
        idx_table.set_header(vec![
            "Index ID",
            "Domain ID",
            "Rel ID",
            "Attr Index",
            "Type Code",
            "Index Name",
            "Data Bytes",
        ]);
        for (i_idx, idx) in reader.index_entries().iter().enumerate() {
            if let Some(df) = opts.domain_filter {
                let matches_dom = reader.domains().iter().enumerate().any(|(d_idx, d)| {
                    d.domain_id == idx.domain_id
                        && (d.name.eq_ignore_ascii_case(df)
                            || d.domain_id.to_string() == df
                            || format!("Domain_{}", d_idx + 1).eq_ignore_ascii_case(df))
                });
                if !matches_dom {
                    continue;
                }
            }
            if let Some(rf) = opts.relation_filter {
                if idx.relation_id != 0xFFFF {
                    let matches_rel = reader.relations().iter().enumerate().any(|(r_idx, r)| {
                        r.relation_id == idx.relation_id
                            && (r.name.eq_ignore_ascii_case(rf)
                                || r.relation_id.to_string() == rf
                                || format!("Relation_{}", r_idx + 1).eq_ignore_ascii_case(rf))
                    });
                    if !matches_rel {
                        continue;
                    }
                }
            }
            let rel_str = if idx.relation_id == 0xFFFF {
                format!("NODE (Dom #{})", idx.domain_id)
            } else {
                let rel_info = reader
                    .relations()
                    .iter()
                    .find(|r| r.relation_id == idx.relation_id);
                if let Some(r) = rel_info {
                    format!(
                        "EDGE (Rel #{}: Dom #{}->#{})",
                        idx.relation_id, r.src_domain_id, r.tgt_domain_id
                    )
                } else {
                    format!("EDGE (Rel #{})", idx.relation_id)
                }
            };
            let i_name = if opts.anonymize {
                format!("Index_{}", i_idx + 1)
            } else {
                idx.name.clone()
            };
            idx_table.add_row(vec![
                idx.index_id.to_string(),
                idx.domain_id.to_string(),
                rel_str,
                idx.attribute_index.to_string(),
                format!("0x{:02X}", idx.index_type),
                i_name,
                idx.data_bytes.to_string(),
            ]);
        }
        println!("{}", idx_table);

        let vamana_entries: Vec<(usize, _)> = reader
            .index_entries()
            .iter()
            .enumerate()
            .filter(|(_, e)| e.index_type == IMP_INDEX_VAMANA_ANN)
            .filter(|(_, idx)| {
                if let Some(df) = opts.domain_filter {
                    let matches_dom = reader.domains().iter().enumerate().any(|(d_idx, d)| {
                        d.domain_id == idx.domain_id
                            && (d.name.eq_ignore_ascii_case(df)
                                || d.domain_id.to_string() == df
                                || format!("Domain_{}", d_idx + 1).eq_ignore_ascii_case(df))
                    });
                    if !matches_dom {
                        return false;
                    }
                }
                if let Some(rf) = opts.relation_filter {
                    if idx.relation_id != 0xFFFF {
                        let matches_rel =
                            reader.relations().iter().enumerate().any(|(r_idx, r)| {
                                r.relation_id == idx.relation_id
                                    && (r.name.eq_ignore_ascii_case(rf)
                                        || r.relation_id.to_string() == rf
                                        || format!("Relation_{}", r_idx + 1)
                                            .eq_ignore_ascii_case(rf))
                            });
                        if !matches_rel {
                            return false;
                        }
                    }
                }
                true
            })
            .collect();
        if !vamana_entries.is_empty() {
            println!();
            println!("--- VAMANA ANN VECTOR INDICES (IMP_INDEX_VAMANA_ANN = 0x08) ---");
            let mut v_table = Table::new();
            v_table.set_header(vec![
                "Index Name",
                "Dom",
                "Attr",
                "Dim",
                "Nodes",
                "R",
                "Medoid",
                "Alpha",
                "EF",
                "Metric",
                "Quant",
                "Adj Off",
                "Quant Off",
            ]);
            for (i_idx, entry) in vamana_entries {
                if let Ok(vh) = reader.parse_vamana_header(entry) {
                    let metric_str = VamanaMetric::from_u8(vh.metric)
                        .map(|m| m.name())
                        .unwrap_or("?");
                    let quant_str = VamanaQuantType::from_u8(vh.quant_type)
                        .map(|q| q.name())
                        .unwrap_or("?");
                    let v_name = if opts.anonymize {
                        format!("Index_{}", i_idx + 1)
                    } else {
                        entry.name.clone()
                    };
                    v_table.add_row(vec![
                        v_name,
                        vh.domain_id.to_string(),
                        vh.vec_attr_id.to_string(),
                        vh.dimension.to_string(),
                        vh.node_count.to_string(),
                        vh.max_degree_r.to_string(),
                        vh.medoid_node_id.to_string(),
                        format!("{:.2}", vh.alpha),
                        vh.ef_construction.to_string(),
                        metric_str.to_string(),
                        quant_str.to_string(),
                        format!("0x{:X}", vh.adj_slab_offset),
                        format!("0x{:X}", vh.quant_code_offset),
                    ]);
                }
            }
            println!("{}", v_table);
        }
    }

    if opts.verbose {
        println!();
        println!("--- VERBOSE SECTION OFFSETS & ATTRIBUTE DESCRIPTORS ---");
        for (d_idx, d) in reader.domains().iter().enumerate() {
            let d_name = if opts.anonymize {
                format!("Domain_{}", d_idx + 1)
            } else {
                d.name.clone()
            };
            if !domain_matches(d_idx, d, &d_name) || d.attributes.is_empty() {
                continue;
            }
            println!(
                "  Domain #{}: Name='{}' | Nodes={}, KeyAttrIndex=0x{:04X}",
                d.domain_id, d_name, d.node_count, d.key_attr_index
            );
            for (a_idx, a) in d.attributes.iter().enumerate() {
                let a_name = if opts.anonymize {
                    format!("DomAttrib_{}", a_idx + 1)
                } else {
                    a.name.clone()
                };
                let nullability = if (a.type_code & 0x80) != 0 {
                    "[Nullable]"
                } else {
                    "[Non-Null]"
                };
                let base_type = a.type_code & 0x7F;
                let type_label = format_attr_type_label(base_type, a.precision, a.scale);
                if base_type == 0x15 {
                    println!(
                        "    Node Attribute '{}': type={} (type_code=0x{:02X} {}, base_type=0x{:02X}), dim={}, data_offset=0x{:X} ({} B)",
                        a_name, type_label, a.type_code, nullability, base_type, a.dimension, a.data_offset, a.data_bytes
                    );
                } else {
                    println!(
                        "    Node Attribute '{}': type_code=0x{:02X} {}, base_type=0x{:02X}, dim={}, data_offset=0x{:X} ({} B)",
                        a_name, a.type_code, nullability, base_type, a.dimension, a.data_offset, a.data_bytes
                    );
                }
            }
        }
        for (r_idx, r) in reader.relations().iter().enumerate() {
            let r_name = if opts.anonymize {
                format!("Relation_{}", r_idx + 1)
            } else {
                r.name.clone()
            };
            if !relation_matches(r_idx, r, &r_name) {
                continue;
            }
            println!(
                "  Relation #{}: Name='{}' | features=0x{:04X}, src_w={}B, tgt_w={}B, edge_w={}B, CSR=0x{:X}({}B)/0x{:X}({}B), CSC=0x{:X}({}B)/0x{:X}({}B), COO=0x{:X}({}B)/0x{:X}({}B)/0x{:X}({}B) ord={}, vcomps={:?}",
                r.relation_id,
                r_name,
                r.section_features,
                reader.relation_src_node_id_width(r),
                reader.relation_tgt_node_id_width(r),
                r.edge_index_width,
                r.csr_row_off_offset,
                r.csr_row_off_bytes,
                r.csr_col_idx_offset,
                r.csr_col_idx_bytes,
                r.csc_row_off_offset,
                r.csc_row_off_bytes,
                r.csc_col_idx_offset,
                r.csc_col_idx_bytes,
                r.coo_src_offset,
                r.coo_src_bytes,
                r.coo_tgt_offset,
                r.coo_tgt_bytes,
                r.coo_edge_id_offset,
                r.coo_edge_id_bytes,
                r.coo_order,
                r.virtual_component_ids
            );
            for (a_idx, a) in r.attributes.iter().enumerate() {
                let a_name = if opts.anonymize {
                    format!("Attrib_{}", a_idx + 1)
                } else {
                    a.name.clone()
                };
                let nullability = if (a.type_code & 0x80) != 0 {
                    "[Nullable]"
                } else {
                    "[Non-Null]"
                };
                let base_type = a.type_code & 0x7F;
                let type_label = format_attr_type_label(base_type, a.precision, a.scale);
                if base_type == 0x15 {
                    println!(
                        "    Attribute '{}': type={} (type_code=0x{:02X} {}, base_type=0x{:02X}), dim={}, data_offset=0x{:X} ({} B)",
                        a_name, type_label, a.type_code, nullability, base_type, a.dimension, a.data_offset, a.data_bytes
                    );
                } else {
                    println!(
                        "    Attribute '{}': type_code=0x{:02X} {}, base_type=0x{:02X}, dim={}, data_offset=0x{:X} ({} B)",
                        a_name, a.type_code, nullability, base_type, a.dimension, a.data_offset, a.data_bytes
                    );
                }
            }
        }

        let custom_meta = reader.get_metadata().unwrap_or_default();
        if !custom_meta.is_empty() {
            println!();
            println!("--- CUSTOM METADATA ---");
            if opts.anonymize {
                let mut keys: Vec<String> = custom_meta.into_keys().collect();
                keys.sort();
                for (i, _) in keys.into_iter().enumerate() {
                    println!("  MetaKey_{} = <REDACTED>", i + 1);
                }
            } else {
                for (k, v) in custom_meta {
                    println!("  {} = {}", k, v);
                }
            }
        }
    }

    Ok(())
}

pub fn format_attr_type_label(base_type: u8, precision: u16, scale: u8) -> String {
    match base_type {
        0x15 => format!("DECIMAL64({}, {})", precision, scale),
        0x0F => "BOOL".to_string(),
        _ => format!("0x{:02X}", base_type),
    }
}

fn format_required_features(flags: u64) -> String {
    let mut names = Vec::new();
    if flags & IMPULSE_FEAT_4KB_PAGE_ALIGNED != 0 {
        names.push("4KB_PAGE_ALIGNED");
    }
    if flags & IMPULSE_FEAT_CRYPTO_SIGNED != 0 {
        names.push("CRYPTO_SIGNED");
    }
    if flags & impulse_graph::spec::IMPULSE_FEAT_FOOTER_CATALOG != 0 {
        names.push("FOOTER_CATALOG");
    }
    format!("0x{:016X} [{}]", flags, names.join(", "))
}

pub fn build_inspect_json(
    reader: &SnapshotReader,
    file: &Path,
    opts: &InspectOptions<'_>,
) -> serde_json::Value {
    let header = reader.header();
    let domain_matches = |d_idx: usize, d: &impulse_graph::DomainInfo, anon_name: &str| -> bool {
        if let Some(f) = opts.domain_filter {
            d.name.eq_ignore_ascii_case(f)
                || d.domain_id.to_string() == f
                || anon_name.eq_ignore_ascii_case(f)
                || format!("Domain_{}", d_idx + 1).eq_ignore_ascii_case(f)
        } else {
            true
        }
    };
    let relation_matches =
        |r_idx: usize, r: &impulse_graph::RelationInfo, anon_name: &str| -> bool {
            if let Some(f) = opts.relation_filter {
                let name_hit = r.name.eq_ignore_ascii_case(f)
                    || r.relation_id.to_string() == f
                    || anon_name.eq_ignore_ascii_case(f)
                    || format!("Relation_{}", r_idx + 1).eq_ignore_ascii_case(f);
                if !name_hit {
                    return false;
                }
            }
            if let Some(df) = opts.domain_filter {
                let matches_src_tgt = reader.domains().iter().enumerate().any(|(d_idx, d)| {
                    (d.domain_id == r.src_domain_id || d.domain_id == r.tgt_domain_id)
                        && (d.name.eq_ignore_ascii_case(df)
                            || d.domain_id.to_string() == df
                            || format!("Domain_{}", d_idx + 1).eq_ignore_ascii_case(df))
                });
                if !matches_src_tgt {
                    return false;
                }
            }
            true
        };

    let mut domains = Vec::new();
    for (d_idx, d) in reader.domains().iter().enumerate() {
        let d_name = if opts.anonymize {
            format!("Domain_{}", d_idx + 1)
        } else {
            d.name.clone()
        };
        if !domain_matches(d_idx, d, &d_name) {
            continue;
        }
        let mut dom_attrs = Vec::new();
        for (a_idx, a) in d.attributes.iter().enumerate() {
            let a_name = if opts.anonymize {
                format!("DomAttrib_{}", a_idx + 1)
            } else {
                a.name.clone()
            };
            let base_type = a.type_code & 0x7F;
            let mut attr_json = json!({
                "name": a_name,
                "type_code": a.type_code,
                "base_type_code": base_type,
                "is_nullable": a.is_nullable(),
                "dimension": a.dimension,
                "data_offset": a.data_offset,
                "data_bytes": a.data_bytes,
                "offsets_offset": a.offsets_offset,
                "offsets_bytes": a.offsets_bytes,
            });
            if base_type == 0x15 {
                attr_json["type_name"] =
                    json!(format_attr_type_label(base_type, a.precision, a.scale));
                attr_json["precision"] = json!(a.precision);
                attr_json["scale"] = json!(a.scale);
            }
            dom_attrs.push(attr_json);
        }
        domains.push(json!({
            "domain_id": d.domain_id,
            "name": d_name,
            "key_type": format!("{:?}", d.key_type),
            "dense_id_width": d.dense_id_width,
            "node_count": d.node_count,
            "key_attr_index": d.key_attr_index,
            "attr_count": d.attributes.len(),
            "attributes": dom_attrs,
        }));
    }

    let mut relations = Vec::new();
    for (r_idx, r) in reader.relations().iter().enumerate() {
        let r_name = if opts.anonymize {
            format!("Relation_{}", r_idx + 1)
        } else if r.name.is_empty() {
            format!("rel_{}_{}", r.src_domain_id, r.tgt_domain_id)
        } else {
            r.name.clone()
        };
        if !relation_matches(r_idx, r, &r_name) {
            continue;
        }
        let mut attrs = Vec::new();
        for (a_idx, a) in r.attributes.iter().enumerate() {
            let a_name = if opts.anonymize {
                format!("Attrib_{}", a_idx + 1)
            } else {
                a.name.clone()
            };
            let base_type = a.type_code & 0x7F;
            let mut attr_json = json!({
                "name": a_name,
                "type_code": a.type_code,
                "base_type_code": base_type,
                "is_nullable": a.is_nullable(),
                "dimension": a.dimension,
                "data_offset": a.data_offset,
                "data_bytes": a.data_bytes,
                "offsets_offset": a.offsets_offset,
                "offsets_bytes": a.offsets_bytes,
            });
            if base_type == 0x15 {
                attr_json["type_name"] =
                    json!(format_attr_type_label(base_type, a.precision, a.scale));
                attr_json["precision"] = json!(a.precision);
                attr_json["scale"] = json!(a.scale);
            }
            attrs.push(attr_json);
        }
        relations.push(json!({
            "relation_id": r.relation_id,
            "src_domain_id": r.src_domain_id,
            "tgt_domain_id": r.tgt_domain_id,
            "encoding_id": r.encoding_id,
            "src_node_id_width": reader.relation_src_node_id_width(r),
            "tgt_node_id_width": reader.relation_tgt_node_id_width(r),
            "node_id_width": r.node_id_width,
            "edge_index_width": r.edge_index_width,
            "section_features": r.section_features,
            "has_csr": r.has_csr(),
            "has_csc": r.has_csc(),
            "has_coo": r.has_coo(),
            "is_flat_forward": r.is_flat_forward(),
            "is_flat_reverse": r.is_flat_reverse(),
            "coo_order": r.coo_order,
            "coo_src_offset": r.coo_src_offset,
            "coo_src_bytes": r.coo_src_bytes,
            "coo_tgt_offset": r.coo_tgt_offset,
            "coo_tgt_bytes": r.coo_tgt_bytes,
            "coo_edge_id_offset": r.coo_edge_id_offset,
            "coo_edge_id_bytes": r.coo_edge_id_bytes,
            "is_virtual": r.is_virtual(),
            "virtual_component_ids": r.virtual_component_ids,
            "name": r_name,
            "node_count": r.node_count,
            "edge_count": r.edge_count,
            "csr_row_off_offset": r.csr_row_off_offset,
            "csr_row_off_bytes": r.csr_row_off_bytes,
            "csr_col_idx_offset": r.csr_col_idx_offset,
            "csr_col_idx_bytes": r.csr_col_idx_bytes,
            "csc_row_off_offset": r.csc_row_off_offset,
            "csc_row_off_bytes": r.csc_row_off_bytes,
            "csc_col_idx_offset": r.csc_col_idx_offset,
            "csc_col_idx_bytes": r.csc_col_idx_bytes,
            "attributes": attrs,
        }));
    }

    let mut indices = Vec::new();
    for (i_idx, idx) in reader.index_entries().iter().enumerate() {
        if let Some(df) = opts.domain_filter {
            let matches_dom = reader.domains().iter().enumerate().any(|(d_idx, d)| {
                d.domain_id == idx.domain_id
                    && (d.name.eq_ignore_ascii_case(df)
                        || d.domain_id.to_string() == df
                        || format!("Domain_{}", d_idx + 1).eq_ignore_ascii_case(df))
            });
            if !matches_dom {
                continue;
            }
        }
        if let Some(rf) = opts.relation_filter {
            if idx.relation_id != 0xFFFF {
                let matches_rel = reader.relations().iter().enumerate().any(|(r_idx, r)| {
                    r.relation_id == idx.relation_id
                        && (r.name.eq_ignore_ascii_case(rf)
                            || r.relation_id.to_string() == rf
                            || format!("Relation_{}", r_idx + 1).eq_ignore_ascii_case(rf))
                });
                if !matches_rel {
                    continue;
                }
            }
        }
        let i_name = if opts.anonymize {
            format!("Index_{}", i_idx + 1)
        } else {
            idx.name.clone()
        };
        let vamana_json = if idx.index_type == IMP_INDEX_VAMANA_ANN {
            reader.parse_vamana_header(idx).ok().map(|vh| {
                let magic = vh.magic;
                let version = vh.version;
                let domain_id = vh.domain_id;
                let vec_attr_id = vh.vec_attr_id;
                let dimension = vh.dimension;
                let node_count = vh.node_count;
                let max_degree_r = vh.max_degree_r;
                let medoid_node_id = vh.medoid_node_id;
                let alpha = vh.alpha;
                let ef_construction = vh.ef_construction;
                let adj_slab_offset = vh.adj_slab_offset;
                let adj_slab_bytes = vh.adj_slab_bytes;
                let quant_code_offset = vh.quant_code_offset;
                let metric_str = VamanaMetric::from_u8(vh.metric)
                    .map(|m| m.name())
                    .unwrap_or("UNKNOWN");
                let quant_str = VamanaQuantType::from_u8(vh.quant_type)
                    .map(|q| q.name())
                    .unwrap_or("UNKNOWN");
                json!({
                    "magic": format!("0x{:08X}", magic),
                    "version": version,
                    "domain_id": domain_id,
                    "vec_attr_id": vec_attr_id,
                    "dimension": dimension,
                    "node_count": node_count,
                    "max_degree_r": max_degree_r,
                    "medoid_node_id": medoid_node_id,
                    "alpha": alpha,
                    "ef_construction": ef_construction,
                    "metric": metric_str,
                    "quant_type": quant_str,
                    "adj_slab_offset": adj_slab_offset,
                    "adj_slab_bytes": adj_slab_bytes,
                    "quant_code_offset": quant_code_offset,
                })
            })
        } else {
            None
        };
        indices.push(json!({
            "index_id": idx.index_id,
            "domain_id": idx.domain_id,
            "relation_id": idx.relation_id,
            "attribute_index": idx.attribute_index,
            "index_type": idx.index_type,
            "name": i_name,
            "data_offset": idx.data_offset,
            "data_bytes": idx.data_bytes,
            "payload_feature_mask": idx.payload_feature_mask,
            "vamana": vamana_json,
        }));
    }

    let custom_metadata = if opts.anonymize {
        let raw_meta = reader.get_metadata().unwrap_or_default();
        let mut keys: Vec<String> = raw_meta.into_keys().collect();
        keys.sort();
        let mut redacted = std::collections::BTreeMap::new();
        for (i, _) in keys.into_iter().enumerate() {
            redacted.insert(format!("MetaKey_{}", i + 1), "<REDACTED>".to_string());
        }
        json!(redacted)
    } else {
        json!(reader.get_metadata().unwrap_or_default())
    };
    let index_count = reader.index_count();

    let ver_str_json = match header.version() {
        11 => "0.11.0 (0x000B)".to_string(),
        10 => "0.10.0 (0x000A)".to_string(),
        9 => "0.9.0 (0x0009)".to_string(),
        v => format!("0.{}.0 (0x{:04X})", v, v),
    };
    json!({
        "file": file.to_string_lossy(),
        "size_bytes": reader.file_size_bytes(),
        "sha256": reader.sha256_hex(),
        "header": {
            "magic": format!("0x{:08X}", header.magic()),
            "version": ver_str_json,
            "data_offset": header.data_offset(),
            "required_features": format!("0x{:016X}", header.required_features()),
            "domain_count": header.domain_count(),
            "relation_count": header.relation_count(),
            "index_count": index_count,
            "timestamp_ms": header.timestamp_ms(),
            "footer_directory_offset": header.footer_directory_offset(),
            "footer_directory_bytes": header.footer_directory_bytes(),
            "header_checksum": format!("0x{:04X}", header.header_checksum()),
            "snapshot_uuid": header.snapshot_uuid_hex(),
        },
        "domains": domains,
        "relations": relations,
        "indices": indices,
        "metadata": custom_metadata,
    })
}

#[derive(Debug, Clone)]
struct PageRegion {
    start: u64,
    bytes: u64,
    alignment: u64,
    name: String,
}

fn render_page_map(
    reader: &SnapshotReader,
    file_size: u64,
    format: &str,
    file_path: &Path,
    anonymize: bool,
) -> Result<(), Box<dyn Error>> {
    let mut regions = Vec::new();

    // 1. Page 0 Header
    regions.push(PageRegion {
        start: 0,
        bytes: 4096,
        alignment: 4096,
        name: "Page 0 Header (Active Baseline + Padding)".to_string(),
    });

    let slice = reader.as_bytes();
    let header = reader.header();
    let ver = header.version();
    let is_modern = matches!(ver, 9..=11);
    let dir_offset = if is_modern && header.footer_directory_offset() > 0 {
        header.footer_directory_offset() as usize
    } else {
        header.data_offset() as usize
    };

    if dir_offset < slice.len() && is_modern {
        let mut cur = dir_offset;
        // String Table
        if cur + 4 <= slice.len() {
            let str_bytes = u32::from_le_bytes(slice[cur..cur + 4].try_into().unwrap()) as usize;
            regions.push(PageRegion {
                start: cur as u64,
                bytes: (4 + str_bytes) as u64,
                alignment: 128,
                name: format!("Section 2.1 Shared String Table ({} bytes)", str_bytes),
            });
            cur += 4 + str_bytes;
        }
        let rem = cur % 128;
        if rem != 0 {
            cur += 128 - rem;
        }

        // Domain Catalog
        let dom_count = header.domain_count() as usize;
        let dom_bytes = if matches!(ver, 10 | 11) {
            reader
                .domains()
                .iter()
                .map(|d| {
                    std::mem::size_of::<impulse_graph::spec::DomainCatalogEntry>()
                        + d.attributes.len()
                            * std::mem::size_of::<impulse_graph::spec::AttributeDescriptor>()
                })
                .sum()
        } else {
            dom_count * std::mem::size_of::<impulse_graph::spec::DomainCatalogEntryV09>()
        };
        if dom_bytes > 0 && cur + dom_bytes <= slice.len() {
            regions.push(PageRegion {
                start: cur as u64,
                bytes: dom_bytes as u64,
                alignment: 128,
                name: format!("Section 2.2 Domain Catalog ({} entries)", dom_count),
            });
            cur += dom_bytes;
        }
        let rem = cur % 128;
        if rem != 0 {
            cur += 128 - rem;
        }

        // Relation Directory Table
        let rel_count = header.relation_count() as usize;
        let rel_start = cur;
        for r in reader.relations() {
            cur += if ver == 11 {
                192
            } else {
                std::mem::size_of::<impulse_graph::spec::RelationDirectoryEntry>()
            };
            cur += r.attributes.len()
                * std::mem::size_of::<impulse_graph::spec::AttributeDescriptor>();
        }
        if cur > rel_start && cur <= slice.len() {
            regions.push(PageRegion {
                start: rel_start as u64,
                bytes: (cur - rel_start) as u64,
                alignment: 128,
                name: format!("Section 2.3 Relation Directory ({} relations)", rel_count),
            });
        }
        let rem = cur % 128;
        if rem != 0 {
            cur += 128 - rem;
        }

        // Index Catalog Table
        let idx_count = reader.index_count();
        let idx_bytes = idx_count * std::mem::size_of::<impulse_graph::spec::IndexDirectoryEntry>();
        if idx_bytes > 0 && cur + idx_bytes <= slice.len() {
            regions.push(PageRegion {
                start: cur as u64,
                bytes: idx_bytes as u64,
                alignment: 128,
                name: format!("Section 2.4 Index Catalog ({} entries)", idx_count),
            });
        }
    }

    // Domain Node Attributes (Spec v0.10.0+)
    for (d_idx, d) in reader.domains().iter().enumerate() {
        let d_name = if anonymize {
            format!("Domain_{}", d_idx + 1)
        } else {
            d.name.clone()
        };
        for (a_idx, a) in d.attributes.iter().enumerate() {
            let a_name = if anonymize {
                format!("DomAttrib_{}", a_idx + 1)
            } else {
                a.name.clone()
            };
            if a.offsets_bytes > 0 {
                regions.push(PageRegion {
                    start: a.offsets_offset,
                    bytes: a.offsets_bytes,
                    alignment: 128,
                    name: format!("DomAttr '{}.{}' Var Offsets", d_name, a_name),
                });
            }
            if a.data_bytes > 0 {
                regions.push(PageRegion {
                    start: a.data_offset,
                    bytes: a.data_bytes,
                    alignment: 128,
                    name: format!("DomAttr '{}.{}' Payload", d_name, a_name),
                });
            }
        }
    }

    // Relation Topology & Attributes
    for (r_idx, r) in reader.relations().iter().enumerate() {
        let r_name = if anonymize {
            format!("Relation_{}", r_idx + 1)
        } else {
            r.name.clone()
        };
        if r.csr_row_off_bytes > 0 {
            regions.push(PageRegion {
                start: r.csr_row_off_offset,
                bytes: r.csr_row_off_bytes,
                alignment: 128,
                name: format!("Rel '{}' CSR Row Offsets", r_name),
            });
        }
        if r.csr_col_idx_bytes > 0 {
            regions.push(PageRegion {
                start: r.csr_col_idx_offset,
                bytes: r.csr_col_idx_bytes,
                alignment: 128,
                name: format!("Rel '{}' CSR Col Indices", r_name),
            });
        }
        if r.csc_row_off_bytes > 0 {
            regions.push(PageRegion {
                start: r.csc_row_off_offset,
                bytes: r.csc_row_off_bytes,
                alignment: 128,
                name: format!("Rel '{}' CSC Row Offsets", r_name),
            });
        }
        if r.csc_col_idx_bytes > 0 {
            regions.push(PageRegion {
                start: r.csc_col_idx_offset,
                bytes: r.csc_col_idx_bytes,
                alignment: 128,
                name: format!("Rel '{}' CSC Col Indices", r_name),
            });
        }
        if r.coo_src_bytes > 0 {
            regions.push(PageRegion {
                start: r.coo_src_offset,
                bytes: r.coo_src_bytes,
                alignment: 128,
                name: format!("Rel '{}' COO Src", r_name),
            });
        }
        if r.coo_tgt_bytes > 0 {
            regions.push(PageRegion {
                start: r.coo_tgt_offset,
                bytes: r.coo_tgt_bytes,
                alignment: 128,
                name: format!("Rel '{}' COO Tgt", r_name),
            });
        }
        if r.coo_edge_id_bytes > 0 {
            regions.push(PageRegion {
                start: r.coo_edge_id_offset,
                bytes: r.coo_edge_id_bytes,
                alignment: 128,
                name: format!("Rel '{}' COO Edge IDs", r_name),
            });
        }
        for (a_idx, a) in r.attributes.iter().enumerate() {
            let a_name = if anonymize {
                format!("Attrib_{}", a_idx + 1)
            } else {
                a.name.clone()
            };
            if a.offsets_bytes > 0 {
                regions.push(PageRegion {
                    start: a.offsets_offset,
                    bytes: a.offsets_bytes,
                    alignment: 128,
                    name: format!("Attr '{}.{}' Var Offsets", r_name, a_name),
                });
            }
            if a.data_bytes > 0 {
                regions.push(PageRegion {
                    start: a.data_offset,
                    bytes: a.data_bytes,
                    alignment: 128,
                    name: format!("Attr '{}.{}' Payload", r_name, a_name),
                });
            }
        }
    }

    // Index Data Blobs
    for (i_idx, entry) in reader.index_entries().iter().enumerate() {
        let i_name = if anonymize {
            format!("Index_{}", i_idx + 1)
        } else {
            entry.name.clone()
        };
        if entry.data_bytes > 0 {
            regions.push(PageRegion {
                start: entry.data_offset,
                bytes: entry.data_bytes,
                alignment: 128,
                name: format!("Index '{}' (type 0x{:02X}) Data", i_name, entry.index_type),
            });
        }
    }

    // Metadata
    if reader.metadata_bytes() > 0 {
        regions.push(PageRegion {
            start: reader.metadata_offset(),
            bytes: reader.metadata_bytes(),
            alignment: 16,
            name: "Custom JSON Metadata Block".to_string(),
        });
    }

    // Trailer
    if file_size >= 16 {
        regions.push(PageRegion {
            start: file_size - 16,
            bytes: 16,
            alignment: 16,
            name: "Footer Trailer (Magic + Length)".to_string(),
        });
    }

    // Sort by start offset
    regions.sort_by_key(|r| r.start);

    // Compute slacks and totals
    let mut total_payload_bytes: u64 = 0;
    let mut total_slack_bytes: u64 = 0;
    let mut items_json = Vec::new();

    for i in 0..regions.len() {
        let r = &regions[i];
        total_payload_bytes += r.bytes;
        let next_start = if i + 1 < regions.len() {
            regions[i + 1].start
        } else {
            file_size
        };
        let end = r.start + r.bytes;
        let slack = next_start.saturating_sub(end);
        total_slack_bytes += slack;

        let is_aligned = if r.alignment > 0 {
            r.start % r.alignment == 0
        } else {
            true
        };

        if format.eq_ignore_ascii_case("json") {
            items_json.push(json!({
                "name": r.name,
                "start_offset": r.start,
                "end_offset": end,
                "bytes": r.bytes,
                "alignment": r.alignment,
                "aligned": is_aligned,
                "slack_bytes": slack,
            }));
        }
    }

    if format.eq_ignore_ascii_case("json") {
        let output = json!({
            "file": file_path.to_string_lossy(),
            "file_size": file_size,
            "total_payload_bytes": total_payload_bytes,
            "total_slack_bytes": total_slack_bytes,
            "padding_ratio_pct": if file_size > 0 { (total_slack_bytes as f64 / file_size as f64) * 100.0 } else { 0.0 },
            "page_map": items_json,
        });
        println!("{}", serde_json::to_string_pretty(&output)?);
        return Ok(());
    }

    println!("=========================================================================");
    println!("             IMPULSE GRAPH ENGINE SNAPSHOT PAGE MAP                      ");
    println!("=========================================================================");
    println!("File:      {}", file_path.display());
    println!(
        "File Size: {} bytes ({:.2} KB)",
        file_size,
        file_size as f64 / 1024.0
    );
    println!();

    let mut table = Table::new();
    table.set_header(vec![
        "Offset Range (Hex)",
        "Start (Dec)",
        "Size",
        "Align",
        "Slack",
        "Aligned?",
        "Section / Component",
    ]);

    for i in 0..regions.len() {
        let r = &regions[i];
        let next_start = if i + 1 < regions.len() {
            regions[i + 1].start
        } else {
            file_size
        };
        let end = r.start + r.bytes;
        let slack = next_start.saturating_sub(end);
        let is_aligned = if r.alignment > 0 {
            r.start % r.alignment == 0
        } else {
            true
        };

        table.add_row(vec![
            format!("0x{:08X}..0x{:08X}", r.start, end),
            r.start.to_string(),
            format!("{} B", r.bytes),
            format!("{} B", r.alignment),
            format!("{} B", slack),
            if is_aligned {
                "YES".to_string()
            } else {
                "NO".to_string()
            },
            r.name.clone(),
        ]);
    }

    println!("{}", table);
    println!();
    println!("Summary:");
    println!(
        "  Total Payload:  {} bytes ({:.2}%)",
        total_payload_bytes,
        if file_size > 0 {
            (total_payload_bytes as f64 / file_size as f64) * 100.0
        } else {
            0.0
        }
    );
    println!(
        "  Total Slack/Pad: {} bytes ({:.2}%)",
        total_slack_bytes,
        if file_size > 0 {
            (total_slack_bytes as f64 / file_size as f64) * 100.0
        } else {
            0.0
        }
    );

    Ok(())
}

#[cfg(test)]
mod inspect_metadata_tests {
    use super::*;
    use impulse_graph::spec::{BaseDataType, KeyType};
    use impulse_graph::writer::SnapshotWriter;

    #[test]
    fn test_build_inspect_json_full_metadata_and_anonymize() {
        let tmp = std::env::temp_dir().join("cli_inspect_metadata_test.imps");
        let tmp_str = tmp.to_str().unwrap();
        let mut writer = SnapshotWriter::new(tmp_str);
        writer.add_domain(0, KeyType::Int32, "Account");
        writer.set_domain_dense_id_width(0, 4);
        writer.add_attribute_to_domain(
            0,
            "tier",
            BaseDataType::Int64 as u8,
            1,
            vec![0u8; 24],
            None,
        );
        writer.add_relation(0, 0, 3, 2, vec![0, 1, 2, 2], vec![1, 2]);
        writer.set_relation_name(0, "transfer");
        writer.set_relation_include_csc(0, true);
        writer.set_relation_coo_v11(0, 1);
        let mut wbytes = Vec::new();
        wbytes.extend_from_slice(&1.5f32.to_le_bytes());
        wbytes.extend_from_slice(&2.5f32.to_le_bytes());
        writer.add_attribute_to_relation(0, "weight", BaseDataType::Float32 as u8, 1, wbytes, None);
        writer.add_index_with_mask(0, 0xFFFF, 0, 1, "idx_secret", vec![0u8; 64], 0x55);
        let mut meta = std::collections::HashMap::new();
        meta.insert("secret_owner".to_string(), "team-alpha".to_string());
        writer.set_metadata(meta);
        writer.finalize().expect("finalize");

        let reader = SnapshotReader::open(&tmp).expect("open");
        let opts = InspectOptions {
            format: "json",
            verbose: false,
            anonymize: false,
            domain_filter: None,
            relation_filter: None,
            page_map: false,
        };
        let j = build_inspect_json(&reader, &tmp, &opts);
        assert_eq!(j["size_bytes"].as_u64(), Some(reader.file_size_bytes()));
        assert_eq!(j["sha256"].as_str(), Some(reader.sha256_hex().as_str()));
        assert_eq!(j["header"]["domain_count"].as_u64(), Some(1));
        assert_eq!(j["header"]["relation_count"].as_u64(), Some(1));
        assert_eq!(j["header"]["index_count"].as_u64(), Some(1));
        assert!(j["header"]["data_offset"].as_u64().unwrap() >= 4096);
        assert_eq!(j["header"]["snapshot_uuid"].as_str().unwrap().len(), 32);
        assert_eq!(j["domains"][0]["dense_id_width"].as_u64(), Some(4));
        assert_eq!(j["domains"][0]["attr_count"].as_u64(), Some(1));
        assert_eq!(
            j["domains"][0]["attributes"][0]["name"].as_str(),
            Some("tier")
        );
        assert_eq!(j["relations"][0]["src_node_id_width"].as_u64(), Some(4));
        assert_eq!(j["relations"][0]["tgt_node_id_width"].as_u64(), Some(4));
        assert_eq!(j["relations"][0]["has_csr"].as_bool(), Some(true));
        assert_eq!(j["relations"][0]["has_csc"].as_bool(), Some(true));
        assert_eq!(j["relations"][0]["has_coo"].as_bool(), Some(true));
        assert_eq!(j["relations"][0]["is_flat_forward"].as_bool(), Some(false));
        assert_eq!(
            j["relations"][0]["attributes"][0]["base_type_code"].as_u64(),
            Some(0x06)
        );
        assert_eq!(
            j["relations"][0]["attributes"][0]["is_nullable"].as_bool(),
            Some(false)
        );
        assert_eq!(j["indices"][0]["name"].as_str(), Some("idx_secret"));
        assert_eq!(j["indices"][0]["payload_feature_mask"].as_u64(), Some(0x55));
        assert_eq!(j["metadata"]["secret_owner"].as_str(), Some("team-alpha"));
        assert!(
            format_required_features(impulse_graph::spec::IMPULSE_FEAT_FOOTER_CATALOG)
                .contains("FOOTER_CATALOG")
        );

        let anon_opts = InspectOptions {
            anonymize: true,
            ..opts
        };
        let aj = build_inspect_json(&reader, &tmp, &anon_opts);
        assert_eq!(aj["domains"][0]["name"].as_str(), Some("Domain_1"));
        assert_eq!(
            aj["domains"][0]["attributes"][0]["name"].as_str(),
            Some("DomAttrib_1")
        );
        assert_eq!(aj["relations"][0]["name"].as_str(), Some("Relation_1"));
        assert_eq!(
            aj["relations"][0]["attributes"][0]["name"].as_str(),
            Some("Attrib_1")
        );
        assert_eq!(aj["indices"][0]["name"].as_str(), Some("Index_1"));
        assert!(aj["metadata"].get("secret_owner").is_none());
        assert_eq!(aj["metadata"]["MetaKey_1"].as_str(), Some("<REDACTED>"));

        let filtered_opts = InspectOptions {
            domain_filter: Some("MissingDomain"),
            ..opts
        };
        let fj = build_inspect_json(&reader, &tmp, &filtered_opts);
        assert_eq!(fj["domains"].as_array().unwrap().len(), 0);
        assert_eq!(fj["relations"].as_array().unwrap().len(), 0);

        // Verify render_page_map runs cleanly on v0.11 with COO + anonymize
        assert!(render_page_map(&reader, reader.file_size_bytes(), "json", &tmp, true).is_ok());

        let _ = std::fs::remove_file(&tmp);
    }
}
