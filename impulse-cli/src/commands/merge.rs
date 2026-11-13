use impulse_graph::{SnapshotReader, SnapshotWriter};
use std::collections::{HashMap, HashSet};
use std::error::Error;
use std::path::Path;

pub fn run(base: &Path, deltas: &[impl AsRef<Path>], output: &Path) -> Result<(), Box<dyn Error>> {
    println!(
        "Merging {} overlay/delta snapshots into base snapshot {:?} -> destination {:?}",
        deltas.len(),
        base,
        output
    );

    let base_reader = SnapshotReader::open(base)?;
    let output_str = output
        .to_str()
        .ok_or("Output path is not valid UTF-8")?;
    let mut writer = SnapshotWriter::new(output_str);

    // 1. Copy base domains & Spec v0.10.0 inline attributes
    for (d_idx, d) in base_reader.domains().iter().enumerate() {
        writer.add_domain(d.domain_id, d.key_type, &d.name);
        writer.set_domain_node_count(d.domain_id, d.node_count);
        for (a_idx, a) in d.attributes.iter().enumerate() {
            let data = base_reader
                .get_domain_attribute_by_index(d_idx, a_idx)?
                .to_vec();
            let offs = base_reader
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

    // 2. Map domain names to IDs and track node counts across base + deltas
    let mut domain_name_to_id: HashMap<String, u16> = base_reader
        .domains()
        .iter()
        .map(|d| (d.name.clone(), d.domain_id))
        .collect();
    let mut domain_node_counts: HashMap<u16, usize> = base_reader
        .domains()
        .iter()
        .map(|d| (d.domain_id, d.node_count as usize))
        .collect();

    // 3. Integrate new domains from deltas
    let mut next_domain_id = base_reader.domain_count();
    for delta_path in deltas {
        let delta_reader = SnapshotReader::open(delta_path.as_ref())?;
        for (d_idx, d) in delta_reader.domains().iter().enumerate() {
            if !domain_name_to_id.contains_key(&d.name) {
                let id = next_domain_id;
                next_domain_id += 1;
                writer.add_domain(id, d.key_type, &d.name);
                writer.set_domain_node_count(id, d.node_count);
                domain_node_counts.insert(id, d.node_count as usize);
                for (a_idx, a) in d.attributes.iter().enumerate() {
                    let data = delta_reader
                        .get_domain_attribute_by_index(d_idx, a_idx)?
                        .to_vec();
                    let offs = delta_reader
                        .get_domain_attribute_offsets_by_index(d_idx, a_idx)?
                        .map(|o| o.to_vec());
                    writer.add_attribute_to_domain(
                        id,
                        &a.name,
                        a.type_code,
                        a.dimension,
                        data,
                        offs,
                    );
                }
                if d.key_attr_index != 0xFFFF {
                    writer.set_domain_key_attr(id, d.key_attr_index);
                }
                domain_name_to_id.insert(d.name.clone(), id);
            }
        }
    }

    // 4. Collect and merge relations
    // Map relation_name -> (src_domain_id, tgt_domain_id, edge_set, include_csc)
    type MergedRelationEntry = (u16, u16, Vec<(u32, u32)>, bool);
    let mut merged_relations: HashMap<String, MergedRelationEntry> = HashMap::new();

    // Load base relations
    for (r_idx, r) in base_reader.relations().iter().enumerate() {
        if r.is_virtual() {
            continue;
        }
        let r_name = if r.name.is_empty() {
            format!("rel_{}_{}", r.src_domain_id, r.tgt_domain_id)
        } else {
            r.name.clone()
        };
        let (row_offsets, col_indices, _) =
            crate::commands::index::extract_relation_csr_and_coo(&base_reader, r_idx, r)?;
        let mut edges = Vec::with_capacity(r.edge_count as usize);

        for u in 0..r.node_count as usize {
            if u + 1 < row_offsets.len() {
                let start = row_offsets[u] as usize;
                let end = (row_offsets[u + 1] as usize).min(col_indices.len());
                for &v in &col_indices[start..end] {
                    edges.push((u as u32, v));
                }
            }
        }
        merged_relations.insert(
            r_name,
            (r.src_domain_id, r.tgt_domain_id, edges, r.has_csc()),
        );
    }

    // Overlay delta relations
    for delta_path in deltas {
        let delta_reader = SnapshotReader::open(delta_path.as_ref())?;
        for (r_idx, r) in delta_reader.relations().iter().enumerate() {
            if r.is_virtual() {
                continue;
            }
            let r_name = if r.name.is_empty() {
                format!("rel_{}_{}", r.src_domain_id, r.tgt_domain_id)
            } else {
                r.name.clone()
            };

            let src_name = delta_reader
                .domains()
                .iter()
                .find(|d| d.domain_id == r.src_domain_id)
                .map(|d| &d.name);
            let tgt_name = delta_reader
                .domains()
                .iter()
                .find(|d| d.domain_id == r.tgt_domain_id)
                .map(|d| &d.name);

            let effective_src_id = src_name
                .and_then(|n| domain_name_to_id.get(n).copied())
                .unwrap_or(r.src_domain_id);
            let effective_tgt_id = tgt_name
                .and_then(|n| domain_name_to_id.get(n).copied())
                .unwrap_or(r.tgt_domain_id);

            let (row_offsets, col_indices, _) =
                crate::commands::index::extract_relation_csr_and_coo(&delta_reader, r_idx, r)?;
            let mut delta_edges = Vec::with_capacity(r.edge_count as usize);

            for u in 0..r.node_count as usize {
                if u + 1 < row_offsets.len() {
                    let start = row_offsets[u] as usize;
                    let end = (row_offsets[u + 1] as usize).min(col_indices.len());
                    for &v in &col_indices[start..end] {
                        delta_edges.push((u as u32, v));
                    }
                }
            }

            if let Some((_s, _t, existing_edges, csc_flag)) = merged_relations.get_mut(&r_name) {
                let mut edge_set: HashSet<(u32, u32)> = existing_edges.iter().copied().collect();
                for e in delta_edges {
                    if edge_set.insert(e) {
                        existing_edges.push(e);
                    }
                }
                if r.has_csc() {
                    *csc_flag = true;
                }
            } else {
                merged_relations.insert(
                    r_name,
                    (effective_src_id, effective_tgt_id, delta_edges, r.has_csc()),
                );
            }
        }
    }

    // Write merged relations to snapshot
    for (rel_idx, (name, (src_dom, tgt_dom, mut edges, include_csc))) in
        merged_relations.into_iter().enumerate()
    {
        edges.sort_unstable_by(|a, b| a.0.cmp(&b.0).then_with(|| a.1.cmp(&b.1)));

        let max_u = edges.iter().map(|e| e.0).max().map(|u| u + 1).unwrap_or(0) as usize;
        let dom_n = domain_node_counts.get(&src_dom).copied().unwrap_or(max_u);
        let n = dom_n.max(max_u);

        let mut row_offsets = vec![0u32; n + 1];
        let mut col_indices = Vec::with_capacity(edges.len());

        for &(u, v) in &edges {
            row_offsets[(u as usize) + 1] += 1;
            col_indices.push(v);
        }
        for i in 0..n {
            row_offsets[i + 1] += row_offsets[i];
        }

        writer.add_relation(
            src_dom,
            tgt_dom,
            n as u64,
            col_indices.len() as u64,
            row_offsets,
            col_indices,
        );
        writer.set_relation_name(rel_idx, &name);
        if include_csc {
            writer.set_relation_include_csc(rel_idx, true);
        }
    }

    // 5. Merge metadata
    let mut meta = base_reader.get_metadata().unwrap_or_default();
    for delta_path in deltas {
        if let Ok(delta_reader) = SnapshotReader::open(delta_path.as_ref()) {
            if let Ok(d_meta) = delta_reader.get_metadata() {
                for (k, v) in d_meta {
                    meta.insert(k, v);
                }
            }
        }
    }
    if !meta.is_empty() {
        writer.set_metadata(meta);
    }

    writer.finalize()?;
    println!(
        "SUCCESS: Successfully merged snapshots into {}",
        output.display()
    );
    Ok(())
}
