use comfy_table::Table;
use impulse_graph::spec::{
    VamanaMetric, VamanaQuantType, IMPULSE_FEAT_4KB_PAGE_ALIGNED, IMPULSE_FEAT_CRYPTO_SIGNED,
    IMP_INDEX_VAMANA_ANN,
};
use impulse_graph::SnapshotReader;
use serde_json::json;
use std::error::Error;
use std::fs;
use std::path::Path;

#[derive(Debug, Clone, Default)]
pub struct InspectOptions<'a> {
    pub format: &'a str,
    pub verbose: bool,
    pub anonymize: bool,
    pub domain_filter: Option<&'a str>,
    pub relation_filter: Option<&'a str>,
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
        },
    )
}

pub fn run_with_options(file: &Path, opts: &InspectOptions<'_>) -> Result<(), Box<dyn Error>> {
    let reader = SnapshotReader::open(file)?;
    let metadata = fs::metadata(file)?;
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

    if opts.format == "json" {
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
            domains.push(json!({
                "domain_id": d.domain_id,
                "name": d_name,
                "key_type": format!("{:?}", d.key_type),
                "node_count": d.node_count,
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
                attrs.push(json!({
                    "name": a_name,
                    "type_code": a.type_code,
                    "dimension": a.dimension,
                    "data_offset": a.data_offset,
                    "data_bytes": a.data_bytes,
                }));
            }

            relations.push(json!({
                "relation_id": r.relation_id,
                "src_domain_id": r.src_domain_id,
                "tgt_domain_id": r.tgt_domain_id,
                "encoding_id": r.encoding_id,
                "name": r_name,
                "node_count": r.node_count,
                "edge_count": r.edge_count,
                "csr_row_off_offset": r.csr_row_off_offset,
                "csr_row_off_bytes": r.csr_row_off_bytes,
                "csr_col_idx_offset": r.csr_col_idx_offset,
                "csr_col_idx_bytes": r.csr_col_idx_bytes,
                "attributes": attrs,
            }));
        }

        let mut indices = Vec::new();
        for idx in reader.index_entries() {
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
                "name": idx.name,
                "data_offset": idx.data_offset,
                "data_bytes": idx.data_bytes,
                "vamana": vamana_json,
            }));
        }

        let custom_metadata = reader.get_metadata().unwrap_or_default();
        let index_count = header.index_count;

        let output = json!({
            "file": file.to_string_lossy(),
            "size_bytes": metadata.len(),
            "header": {
                "magic": format!("0x{:08X}", header.magic()),
                "version": format!("0.9.0 (0x{:04X})", header.version()),
                "required_features": format!("0x{:016X}", header.required_features()),
                "domain_count": header.domain_count(),
                "relation_count": header.relation_count(),
                "index_count": index_count,
            },
            "domains": domains,
            "relations": relations,
            "indices": indices,
            "metadata": custom_metadata,
        });

        println!("{}", serde_json::to_string_pretty(&output)?);
        return Ok(());
    }

    println!("=========================================================================");
    println!("               IMPULSE GRAPH ENGINE SNAPSHOT INSPECTOR                   ");
    println!("=========================================================================");
    println!("File:              {}", file.display());
    println!("File Size:         {} bytes", metadata.len());
    println!("Magic:             0x{:08X} (IMPS)", header.magic());
    let version_str = if header.version() == 9 {
        "0.9.0 (0x0009)".to_string()
    } else {
        format!("0.9.0 (0x{:04X})", header.version())
    };
    println!("Version:           {}", version_str);
    println!(
        "Required Features: {}",
        format_required_features(header.required_features())
    );
    println!("Domains:           {}", header.domain_count());
    println!("Relations:         {}", header.relation_count());
    println!();

    // Domain Table
    let mut dom_table = Table::new();
    dom_table.set_header(vec!["ID", "Domain Name", "Key Type", "Node Count"]);
    for (d_idx, d) in reader.domains().iter().enumerate() {
        let d_name = if opts.anonymize {
            format!("Domain_{}", d_idx + 1)
        } else {
            d.name.clone()
        };
        if !domain_matches(d_idx, d, &d_name) {
            continue;
        }
        dom_table.add_row(vec![
            d.domain_id.to_string(),
            d_name,
            format!("{:?}", d.key_type),
            d.node_count.to_string(),
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
        for idx in reader.index_entries() {
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
            idx_table.add_row(vec![
                idx.index_id.to_string(),
                idx.domain_id.to_string(),
                rel_str,
                idx.attribute_index.to_string(),
                format!("0x{:02X}", idx.index_type),
                idx.name.clone(),
                idx.data_bytes.to_string(),
            ]);
        }
        println!("{}", idx_table);

        let vamana_entries: Vec<_> = reader
            .index_entries()
            .iter()
            .filter(|e| e.index_type == IMP_INDEX_VAMANA_ANN)
            .filter(|idx| {
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
            for entry in vamana_entries {
                if let Ok(vh) = reader.parse_vamana_header(entry) {
                    let metric_str = VamanaMetric::from_u8(vh.metric)
                        .map(|m| m.name())
                        .unwrap_or("?");
                    let quant_str = VamanaQuantType::from_u8(vh.quant_type)
                        .map(|q| q.name())
                        .unwrap_or("?");
                    v_table.add_row(vec![
                        entry.name.clone(),
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
                "  Relation #{}: Name='{}' | CSR Row Offsets Offset=0x{:X} ({} B), Column Indices Offset=0x{:X} ({} B)",
                r.relation_id, r_name, r.csr_row_off_offset, r.csr_row_off_bytes, r.csr_col_idx_offset, r.csr_col_idx_bytes
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
                println!(
                    "    Attribute '{}': type_code=0x{:02X} {}, base_type=0x{:02X}, dim={}, data_offset=0x{:X} ({} B)",
                    a_name, a.type_code, nullability, base_type, a.dimension, a.data_offset, a.data_bytes
                );
            }
        }

        let custom_meta = reader.get_metadata().unwrap_or_default();
        if !custom_meta.is_empty() {
            println!();
            println!("--- CUSTOM METADATA ---");
            for (k, v) in custom_meta {
                println!("  {} = {}", k, v);
            }
        }
    }

    Ok(())
}

fn format_required_features(flags: u64) -> String {
    let mut names = Vec::new();
    if flags & IMPULSE_FEAT_4KB_PAGE_ALIGNED != 0 {
        names.push("4KB_PAGE_ALIGNED");
    }
    if flags & IMPULSE_FEAT_CRYPTO_SIGNED != 0 {
        names.push("CRYPTO_SIGNED");
    }
    if flags & 0x40 != 0 {
        names.push("INDEX_DIRECTORY");
    }
    format!("0x{:016X} [{}]", flags, names.join(", "))
}
