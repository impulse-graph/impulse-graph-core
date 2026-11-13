use clap::Args;
use impulse_graph::SnapshotReader;
use impulse_graph::SnapshotWriter;
use std::collections::{HashMap, HashSet};
use std::error::Error;
use std::path::PathBuf;

#[derive(Args, Debug, Clone)]
pub struct TransmuteArgs {
    /// Path to input .imps binary snapshot file
    pub input: PathBuf,

    /// Path to output .imps binary snapshot file
    #[arg(short, long)]
    pub output: PathBuf,

    /// Automatically downcast node ID integer widths to u16/u32 where safely possible
    #[arg(long)]
    pub compact_widths: bool,

    /// Add CSC reverse index transposition to relation(s) ("all" or comma-separated relation names)
    #[arg(long, value_delimiter = ',')]
    pub add_csc: Option<Vec<String>>,

    /// Drop CSC reverse index transposition from relation(s) ("all" or comma-separated relation names)
    #[arg(long, value_delimiter = ',')]
    pub drop_csc: Option<Vec<String>>,

    /// Infer relation multiplicity (1:1, 1:M, M:1, M:N) and store in snapshot metadata
    #[arg(long)]
    pub infer_multiplicity: bool,

    /// Synthesize virtual joined relation (e.g. "CO_ACTED=ACTED_IN,ACTED_IN_REV")
    #[arg(long, value_delimiter = ',')]
    pub add_virtual_relation: Option<Vec<String>>,

    /// Rename domain (format: OldName=NewName)
    #[arg(long, value_delimiter = ',')]
    pub rename_domain: Option<Vec<String>>,

    /// Rename relation (format: OldName=NewName)
    #[arg(long, value_delimiter = ',')]
    pub rename_relation: Option<Vec<String>>,

    /// Rename attribute (format: OldName=NewName)
    #[arg(long, value_delimiter = ',')]
    pub rename_attr: Option<Vec<String>>,

    /// Drop domain by name
    #[arg(long, value_delimiter = ',')]
    pub drop_domain: Option<Vec<String>>,

    /// Drop relation by name
    #[arg(long, value_delimiter = ',')]
    pub drop_relation: Option<Vec<String>>,

    /// Drop attribute by name
    #[arg(long, value_delimiter = ',')]
    pub drop_attr: Option<Vec<String>>,

    /// Set metadata key-value pair (format: Key=Value)
    #[arg(long, value_delimiter = ',')]
    pub set_meta: Option<Vec<String>>,

    /// Delete metadata key
    #[arg(long, value_delimiter = ',')]
    pub delete_meta: Option<Vec<String>>,

    /// Clear all metadata
    #[arg(long)]
    pub clear_meta: bool,
}

pub fn run(args: &TransmuteArgs) -> Result<(), Box<dyn Error>> {
    println!(
        "Transmuting snapshot: {} -> {}",
        args.input.display(),
        args.output.display()
    );

    let reader = SnapshotReader::open(&args.input)?;
    let mut writer = SnapshotWriter::new(args.output.to_str().unwrap());

    // 1. Parse renames and drops
    let mut domain_renames: HashMap<String, String> = HashMap::new();
    if let Some(list) = &args.rename_domain {
        for item in list {
            if let Some((k, v)) = item.split_once('=') {
                domain_renames.insert(k.trim().to_string(), v.trim().to_string());
            }
        }
    }

    let mut rel_renames: HashMap<String, String> = HashMap::new();
    if let Some(list) = &args.rename_relation {
        for item in list {
            if let Some((k, v)) = item.split_once('=') {
                rel_renames.insert(k.trim().to_string(), v.trim().to_string());
            }
        }
    }

    let mut attr_renames: HashMap<String, String> = HashMap::new();
    if let Some(list) = &args.rename_attr {
        for item in list {
            if let Some((k, v)) = item.split_once('=') {
                attr_renames.insert(k.trim().to_string(), v.trim().to_string());
            }
        }
    }

    let drop_domains: HashSet<String> = args
        .drop_domain
        .as_deref()
        .unwrap_or_default()
        .iter()
        .map(|s| s.trim().to_string())
        .collect();

    let drop_relations: HashSet<String> = args
        .drop_relation
        .as_deref()
        .unwrap_or_default()
        .iter()
        .map(|s| s.trim().to_string())
        .collect();

    let drop_attrs: HashSet<String> = args
        .drop_attr
        .as_deref()
        .unwrap_or_default()
        .iter()
        .map(|s| s.trim().to_string())
        .collect();

    let add_csc_set: HashSet<String> = args
        .add_csc
        .as_deref()
        .unwrap_or_default()
        .iter()
        .map(|s| s.trim().to_string())
        .collect();

    let drop_csc_set: HashSet<String> = args
        .drop_csc
        .as_deref()
        .unwrap_or_default()
        .iter()
        .map(|s| s.trim().to_string())
        .collect();

    let add_all_csc = add_csc_set.contains("all");
    let drop_all_csc = drop_csc_set.contains("all");

    // 2. Transfer Domains & Inline Node Attributes (Spec v0.10.0)
    let mut domain_id_map: HashMap<u16, u16> = HashMap::new();
    let mut next_new_domain_id: u16 = 0;

    for (d_idx, d) in reader.domains().iter().enumerate() {
        if drop_domains.contains(&d.name) {
            println!("  [DROP] Domain '{}' (ID={})", d.name, d.domain_id);
            continue;
        }
        let final_name = domain_renames
            .get(&d.name)
            .cloned()
            .unwrap_or_else(|| d.name.clone());
        let new_id = next_new_domain_id;
        next_new_domain_id += 1;
        domain_id_map.insert(d.domain_id, new_id);

        writer.add_domain(new_id, d.key_type, &final_name);
        writer.set_domain_node_count(new_id, d.node_count);

        let mut new_key_attr_idx: Option<u16> = None;
        let mut kept_attr_idx: u16 = 0;
        for (a_idx, a) in d.attributes.iter().enumerate() {
            if drop_attrs.contains(&a.name) {
                println!("  [DROP] Domain Attribute '{}.{}'", final_name, a.name);
                continue;
            }
            let final_attr_name = attr_renames
                .get(&a.name)
                .cloned()
                .unwrap_or_else(|| a.name.clone());
            let data = reader.get_domain_attribute_by_index(d_idx, a_idx)?.to_vec();
            let offsets = reader
                .get_domain_attribute_offsets_by_index(d_idx, a_idx)?
                .map(|s| s.to_vec());
            writer.add_attribute_to_domain(
                new_id,
                &final_attr_name,
                a.type_code,
                a.dimension,
                data,
                offsets,
            );
            if d.key_attr_index == a_idx as u16 {
                new_key_attr_idx = Some(kept_attr_idx);
            }
            kept_attr_idx += 1;
        }
        if let Some(ka) = new_key_attr_idx {
            writer.set_domain_key_attr(new_id, ka);
        }

        println!(
            "  [KEEP] Domain '{}' -> '{}' (ID: {} -> {}, Nodes: {})",
            d.name, final_name, d.domain_id, new_id, d.node_count
        );
    }

    // 3. Metadata processing
    let mut metadata = if args.clear_meta {
        HashMap::new()
    } else {
        reader.get_metadata().unwrap_or_default()
    };

    if let Some(dels) = &args.delete_meta {
        for k in dels {
            metadata.remove(k.trim());
        }
    }

    if let Some(sets) = &args.set_meta {
        for item in sets {
            if let Some((k, v)) = item.split_once('=') {
                metadata.insert(k.trim().to_string(), v.trim().to_string());
            }
        }
    }

    // 4. Transfer Relations
    let mut kept_relations: Vec<(usize, &impulse_graph::RelationInfo)> = Vec::new();
    for (r_idx, r) in reader.relations().iter().enumerate() {
        if drop_relations.contains(&r.name) {
            println!("  [DROP] Relation '{}'", r.name);
            continue;
        }
        if !domain_id_map.contains_key(&r.src_domain_id)
            || !domain_id_map.contains_key(&r.tgt_domain_id)
        {
            println!("  [DROP] Relation '{}' (references dropped domain)", r.name);
            continue;
        }
        kept_relations.push((r_idx, r));
    }

    let mut old_to_new_rel_id: HashMap<u16, u16> = HashMap::new();
    let mut next_new_rel_idx = 0;
    for (r_idx, r) in &kept_relations {
        let new_src = domain_id_map[&r.src_domain_id];
        let new_tgt = domain_id_map[&r.tgt_domain_id];
        let final_name = rel_renames
            .get(&r.name)
            .cloned()
            .unwrap_or_else(|| r.name.clone());

        if r.is_virtual() {
            let remapped_comps: Vec<u16> = r
                .virtual_component_ids
                .iter()
                .filter_map(|cid| old_to_new_rel_id.get(cid).copied())
                .collect();
            if !remapped_comps.is_empty() {
                writer.add_virtual_relation(
                    new_src,
                    new_tgt,
                    r.node_count,
                    &final_name,
                    remapped_comps,
                );
                old_to_new_rel_id.insert(r.relation_id, next_new_rel_idx as u16);
                next_new_rel_idx += 1;
            }
            continue;
        }

        let (row_offsets, col_indices, coo_srcs) =
            crate::commands::index::extract_relation_csr_and_coo(&reader, *r_idx, r)?;

        // Determine CSC inclusion
        let mut include_csc = r.has_csc();
        if add_all_csc || add_csc_set.contains(&r.name) {
            include_csc = true;
        }
        if drop_all_csc || drop_csc_set.contains(&r.name) {
            include_csc = false;
        }

        let mut card_bits = r.section_features & impulse_graph::spec::IMP_REL_CARDINALITY_MASK;

        // Multiplicity inference
        if args.infer_multiplicity && r.edge_count > 0 {
            let mut max_out = 0usize;
            for i in 0..row_offsets.len().saturating_sub(1) {
                let deg = (row_offsets[i + 1] - row_offsets[i]) as usize;
                if deg > max_out {
                    max_out = deg;
                }
            }
            let mut in_degrees: HashMap<u32, usize> = HashMap::new();
            for &c in &col_indices {
                *in_degrees.entry(c).or_insert(0) += 1;
            }
            let max_in = in_degrees.values().copied().max().unwrap_or(0);
            let (mult, inferred_bits) = match (max_out <= 1, max_in <= 1) {
                (true, true) => ("1:1", impulse_graph::spec::IMP_REL_CARDINALITY_1_1),
                (false, true) => ("1:M", impulse_graph::spec::IMP_REL_CARDINALITY_1_M),
                (true, false) => ("M:1", impulse_graph::spec::IMP_REL_CARDINALITY_M_1),
                (false, false) => ("M:N", impulse_graph::spec::IMP_REL_CARDINALITY_M_N),
            };
            card_bits = inferred_bits;
            metadata.insert(format!("multiplicity.{}", final_name), mult.to_string());
            println!(
                "  [MULTIPLICITY] Relation '{}': {} (max_out={}, max_in={})",
                final_name, mult, max_out, max_in
            );
        }

        // Add relation
        writer.add_relation(
            new_src,
            new_tgt,
            r.node_count,
            r.edge_count,
            row_offsets,
            col_indices.clone(),
        );
        writer.set_relation_name(next_new_rel_idx, &final_name);
        writer.set_relation_include_csc(next_new_rel_idx, include_csc);
        writer.set_relation_encoding(next_new_rel_idx, r.encoding_id);
        writer.set_relation_section_features(next_new_rel_idx, card_bits);
        if let Some(srcs) = coo_srcs {
            writer.set_relation_coo_sources(next_new_rel_idx, srcs);
        }

        let tgt_node_count = reader
            .domains()
            .iter()
            .find(|d| d.domain_id == r.tgt_domain_id)
            .map(|d| d.node_count)
            .unwrap_or(0);

        // Compact widths if requested (leave 0xFFFF free as sentinel for flat arrays)
        if args.compact_widths {
            let max_col = col_indices.iter().copied().max().unwrap_or(0);
            if max_col < 65535 && tgt_node_count <= 65535 && r.node_count <= 65535 {
                writer.set_relation_widths(next_new_rel_idx, 2, 4);
                println!(
                    "  [COMPACT] Relation '{}': downcasted node_id_width -> u16",
                    final_name
                );
            } else {
                writer.set_relation_widths(next_new_rel_idx, 4, 4);
            }
        } else {
            writer.set_relation_widths(next_new_rel_idx, r.node_id_width, r.edge_index_width);
        }

        // Copy / filter attributes
        for (a_idx, a) in r.attributes.iter().enumerate() {
            if drop_attrs.contains(&a.name) {
                println!("  [DROP] Attribute '{}.{}'", final_name, a.name);
                continue;
            }
            let final_attr_name = attr_renames
                .get(&a.name)
                .cloned()
                .unwrap_or_else(|| a.name.clone());
            let data = reader.get_attribute_data(*r_idx, a_idx)?.to_vec();
            let offsets = reader
                .get_attribute_offsets(*r_idx, a_idx)?
                .map(|s| s.to_vec());

            writer.add_attribute_to_relation(
                next_new_rel_idx,
                &final_attr_name,
                a.type_code,
                a.dimension,
                data,
                offsets,
            );
        }

        old_to_new_rel_id.insert(r.relation_id, next_new_rel_idx as u16);
        println!(
            "  [KEEP] Relation '{}' -> '{}' (E={}, CSC={})",
            r.name, final_name, r.edge_count, include_csc
        );
        next_new_rel_idx += 1;
    }

    // 5. Synthesize Virtual Joined Relations (A o B)
    if let Some(v_rels) = &args.add_virtual_relation {
        for v_spec in v_rels {
            if let Some((v_name, comp_spec)) = v_spec.split_once('=') {
                if let Some((rel_a_name, rel_b_name)) = comp_spec.split_once(',') {
                    let v_name = v_name.trim();
                    let rel_a_name = rel_a_name.trim();
                    let rel_b_name = rel_b_name.trim();

                    let find_a = kept_relations.iter().find(|(_, r)| {
                        rel_renames
                            .get(&r.name)
                            .map(|s| s.as_str())
                            .unwrap_or(&r.name)
                            == rel_a_name
                    });
                    let find_b = kept_relations.iter().find(|(_, r)| {
                        rel_renames
                            .get(&r.name)
                            .map(|s| s.as_str())
                            .unwrap_or(&r.name)
                            == rel_b_name
                    });

                    if let (Some((idx_a, rel_a)), Some((idx_b, rel_b))) = (find_a, find_b) {
                        let rows_a = reader.get_row_offsets(*idx_a)?;
                        let cols_a = reader.get_col_indices_as_u32_vec(*idx_a)?;
                        let rows_b = reader.get_row_offsets(*idx_b)?;
                        let cols_b = reader.get_col_indices_as_u32_vec(*idx_b)?;

                        let src_nodes = rel_a.node_count as usize;
                        let mut new_row_offsets = Vec::with_capacity(src_nodes + 1);
                        let mut new_col_indices = Vec::new();
                        new_row_offsets.push(0u32);

                        for u in 0..src_nodes {
                            let mut neighbors = HashSet::new();
                            let start_a = rows_a[u] as usize;
                            let end_a = rows_a[u + 1] as usize;
                            for &v in &cols_a[start_a..end_a] {
                                let v_idx = v as usize;
                                if v_idx + 1 < rows_b.len() {
                                    let start_b = rows_b[v_idx] as usize;
                                    let end_b = rows_b[v_idx + 1] as usize;
                                    for &w in &cols_b[start_b..end_b] {
                                        neighbors.insert(w);
                                    }
                                }
                            }
                            let mut sorted_nb: Vec<u32> = neighbors.into_iter().collect();
                            sorted_nb.sort_unstable();
                            new_col_indices.extend(sorted_nb);
                            new_row_offsets.push(new_col_indices.len() as u32);
                        }

                        let total_edges = new_col_indices.len() as u64;
                        let new_src = domain_id_map[&rel_a.src_domain_id];
                        let new_tgt = domain_id_map[&rel_b.tgt_domain_id];

                        writer.add_relation(
                            new_src,
                            new_tgt,
                            rel_a.node_count,
                            total_edges,
                            new_row_offsets,
                            new_col_indices,
                        );
                        writer.set_relation_name(next_new_rel_idx, v_name);
                        writer.set_relation_widths(next_new_rel_idx, 4, 4);
                        println!(
                            "  [VIRTUAL] Synthesized relation '{}' = '{}' o '{}' (E={})",
                            v_name, rel_a_name, rel_b_name, total_edges
                        );
                        next_new_rel_idx += 1;
                    } else {
                        eprintln!(
                            "  [WARN] Could not find relations '{}' or '{}' for virtual join",
                            rel_a_name, rel_b_name
                        );
                    }
                }
            }
        }
    }

    // 6. Set Final Metadata & Finalize Snapshot
    writer.set_metadata(metadata);
    writer.finalize()?;

    println!("Transmutation complete: {}", args.output.display());
    Ok(())
}
