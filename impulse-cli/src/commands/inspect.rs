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
    let metadata = fs::metadata(file)?;
    let header = reader.header();

    if opts.page_map {
        return render_page_map(&reader, metadata.len(), opts.format, file);
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
            let mut dom_attrs = Vec::new();
            for (a_idx, a) in d.attributes.iter().enumerate() {
                let a_name = if opts.anonymize {
                    format!("DomAttrib_{}", a_idx + 1)
                } else {
                    a.name.clone()
                };
                dom_attrs.push(json!({
                    "name": a_name,
                    "type_code": a.type_code,
                    "dimension": a.dimension,
                    "data_offset": a.data_offset,
                    "data_bytes": a.data_bytes,
                }));
            }
            domains.push(json!({
                "domain_id": d.domain_id,
                "name": d_name,
                "key_type": format!("{:?}", d.key_type),
                "node_count": d.node_count,
                "key_attr_index": d.key_attr_index,
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
                "node_id_width": r.node_id_width,
                "section_features": r.section_features,
                "has_csc": r.has_csc(),
                "is_virtual": r.is_virtual(),
                "virtual_component_ids": r.virtual_component_ids,
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

        let ver_str_json = match header.version() {
            10 => "0.10.0 (0x000A)".to_string(),
            9 => "0.9.0 (0x0009)".to_string(),
            v => format!("0.{}.0 (0x{:04X})", v, v),
        };
        let output = json!({
            "file": file.to_string_lossy(),
            "size_bytes": metadata.len(),
            "header": {
                "magic": format!("0x{:08X}", header.magic()),
                "version": ver_str_json,
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
    let version_str = match header.version() {
        10 => "0.10.0 (0x000A)".to_string(),
        9 => "0.9.0 (0x0009)".to_string(),
        v => format!("0.{}.0 (0x{:04X})", v, v),
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
    dom_table.set_header(vec![
        "ID",
        "Domain Name",
        "Key Type",
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
                println!(
                    "    Node Attribute '{}': type_code=0x{:02X} {}, base_type=0x{:02X}, dim={}, data_offset=0x{:X} ({} B)",
                    a_name, a.type_code, nullability, base_type, a.dimension, a.data_offset, a.data_bytes
                );
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
                "  Relation #{}: Name='{}' | features=0x{:04X}, width=u{}, CSR Row Offsets Offset=0x{:X} ({} B), Column Indices Offset=0x{:X} ({} B)",
                r.relation_id, r_name, r.section_features, r.node_id_width as u32 * 8, r.csr_row_off_offset, r.csr_row_off_bytes, r.csr_col_idx_offset, r.csr_col_idx_bytes
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
    let dir_offset = if (ver == 9 || ver == 10) && header.footer_directory_offset() > 0 {
        header.footer_directory_offset() as usize
    } else {
        header.data_offset() as usize
    };

    if dir_offset < slice.len() && (ver == 9 || ver == 10) {
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
        let dom_bytes = if ver == 10 {
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
            cur += std::mem::size_of::<impulse_graph::spec::RelationDirectoryEntry>();
            cur += r.attributes.len() * std::mem::size_of::<impulse_graph::spec::AttributeDescriptor>();
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
        let idx_count = header.index_count as usize;
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

    // Domain Node Attributes (Spec v0.10.0)
    for d in reader.domains() {
        for a in &d.attributes {
            if a.offsets_bytes > 0 {
                regions.push(PageRegion {
                    start: a.offsets_offset,
                    bytes: a.offsets_bytes,
                    alignment: 128,
                    name: format!("DomAttr '{}.{}' Var Offsets", d.name, a.name),
                });
            }
            if a.data_bytes > 0 {
                regions.push(PageRegion {
                    start: a.data_offset,
                    bytes: a.data_bytes,
                    alignment: 128,
                    name: format!("DomAttr '{}.{}' Payload", d.name, a.name),
                });
            }
        }
    }

    // Relation Topology & Attributes
    for r in reader.relations() {
        if r.csr_row_off_bytes > 0 {
            regions.push(PageRegion {
                start: r.csr_row_off_offset,
                bytes: r.csr_row_off_bytes,
                alignment: 128,
                name: format!("Rel '{}' CSR Row Offsets", r.name),
            });
        }
        if r.csr_col_idx_bytes > 0 {
            regions.push(PageRegion {
                start: r.csr_col_idx_offset,
                bytes: r.csr_col_idx_bytes,
                alignment: 128,
                name: format!("Rel '{}' CSR Col Indices", r.name),
            });
        }
        if r.csc_row_off_bytes > 0 {
            regions.push(PageRegion {
                start: r.csc_row_off_offset,
                bytes: r.csc_row_off_bytes,
                alignment: 128,
                name: format!("Rel '{}' CSC Row Offsets", r.name),
            });
        }
        if r.csc_col_idx_bytes > 0 {
            regions.push(PageRegion {
                start: r.csc_col_idx_offset,
                bytes: r.csc_col_idx_bytes,
                alignment: 128,
                name: format!("Rel '{}' CSC Col Indices", r.name),
            });
        }
        for a in &r.attributes {
            if a.offsets_bytes > 0 {
                regions.push(PageRegion {
                    start: a.offsets_offset,
                    bytes: a.offsets_bytes,
                    alignment: 128,
                    name: format!("Attr '{}.{}' Var Offsets", r.name, a.name),
                });
            }
            if a.data_bytes > 0 {
                regions.push(PageRegion {
                    start: a.data_offset,
                    bytes: a.data_bytes,
                    alignment: 128,
                    name: format!("Attr '{}.{}' Payload", r.name, a.name),
                });
            }
        }
    }

    // Index Data Blobs
    for entry in reader.index_entries() {
        if entry.data_bytes > 0 {
            regions.push(PageRegion {
                start: entry.data_offset,
                bytes: entry.data_bytes,
                alignment: 128,
                name: format!("Index '{}' (type 0x{:02X}) Data", entry.name, entry.index_type),
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
    println!("File Size: {} bytes ({:.2} KB)", file_size, file_size as f64 / 1024.0);
    println!();

    let mut table = Table::new();
    table.set_header(vec![
        "Offset Range (Hex)", "Start (Dec)", "Size", "Align", "Slack", "Aligned?", "Section / Component",
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
            if is_aligned { "YES".to_string() } else { "NO".to_string() },
            r.name.clone(),
        ]);
    }

    println!("{}", table);
    println!();
    println!("Summary:");
    println!("  Total Payload:  {} bytes ({:.2}%)",
        total_payload_bytes,
        if file_size > 0 { (total_payload_bytes as f64 / file_size as f64) * 100.0 } else { 0.0 }
    );
    println!("  Total Slack/Pad: {} bytes ({:.2}%)",
        total_slack_bytes,
        if file_size > 0 { (total_slack_bytes as f64 / file_size as f64) * 100.0 } else { 0.0 }
    );

    Ok(())
}
