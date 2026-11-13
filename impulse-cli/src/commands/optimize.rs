use crate::embeddings::{load_embeddings_from_path, EmbeddingAttachmentSpec, VectorIndexSpec};
use impulse_graph::spec::{
    BaseDataType, IMPULSE_TYPE_MASK, IMP_INDEX_MINIMAL_PERFECT_HASH, IMP_INDEX_VAMANA_ANN,
};
use impulse_graph::vamana::{
    compute_vamana_bfs_order, permute_vamana_index, VamanaBuildConfig, VamanaMetric,
    VamanaQuantType,
};
use impulse_graph::{SnapshotReader, SnapshotWriter};
use std::collections::{HashMap, VecDeque};
use std::error::Error;
use std::path::Path;

#[derive(Debug, Clone, Default)]
pub struct OptimizeOptions {
    pub rcm: bool,
    pub degree_sort: bool,
    pub vamana_locality: bool,
    pub csc: bool,
    pub encoding: Option<String>,
    pub strip_mappings: bool,
    pub strip_properties: bool,
    pub rebuild_vector_index: bool,
    pub embeddings: Vec<String>,
    pub vector_indices: Vec<String>,
}

#[allow(clippy::too_many_arguments)]
pub fn run(
    input_path: &Path,
    output_path: &Path,
    rcm: bool,
    degree_sort: bool,
    csc: bool,
    encoding: Option<&str>,
    strip_mappings: bool,
    strip_properties: bool,
) -> Result<(), Box<dyn Error>> {
    let opts = OptimizeOptions {
        rcm,
        degree_sort,
        vamana_locality: false,
        csc,
        encoding: encoding.map(|s| s.to_string()),
        strip_mappings,
        strip_properties,
        rebuild_vector_index: false,
        embeddings: Vec::new(),
        vector_indices: Vec::new(),
    };
    run_with_options(input_path, output_path, &opts)
}

pub fn run_with_options(
    input_path: &Path,
    output_path: &Path,
    opts: &OptimizeOptions,
) -> Result<(), Box<dyn Error>> {
    println!(
        "Optimizing snapshot: {} -> {}",
        input_path.display(),
        output_path.display()
    );
    let reader = SnapshotReader::open(input_path)?;
    let output_str = output_path
        .to_str()
        .ok_or("Output path is not valid UTF-8")?;
    let mut writer = SnapshotWriter::new(output_str);

    if let Ok(meta) = reader.get_metadata() {
        if !meta.is_empty() {
            writer.set_metadata(meta);
        }
    }

    // 1. Re-add domains and compute per-domain node permutations: (old_to_new, new_to_old)
    let mut domain_perms: HashMap<u16, (Vec<u32>, Vec<u32>)> = HashMap::new();

    for (d_idx, d) in reader.domains().iter().enumerate() {
        writer.add_domain(d.domain_id, d.key_type, &d.name);
        writer.set_domain_node_count(d.domain_id, d.node_count);
        let n = d.node_count as usize;

        // Check if vamana_locality is requested and a Vamana index exists for this domain
        let mut computed_perm: Option<(Vec<u32>, Vec<u32>)> = None;
        if opts.vamana_locality && n > 0 {
            for entry in reader.index_entries() {
                if entry.index_type == IMP_INDEX_VAMANA_ANN && entry.domain_id == d.domain_id {
                    if let Ok(hdr) = reader.parse_vamana_header(entry) {
                        if let Ok(adj_slab) = reader.get_vamana_adj_slab(&hdr) {
                            if hdr.node_count as usize == n {
                                computed_perm = Some(compute_vamana_bfs_order(&hdr, adj_slab));
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Otherwise check RCM or degree_sort on the primary self-relation of domain `d.domain_id`
        if computed_perm.is_none() && (opts.rcm || opts.degree_sort) && n > 0 {
            if let Some((rel_idx, rel)) = reader
                .relations()
                .iter()
                .enumerate()
                .find(|(_, r)| r.src_domain_id == d.domain_id && r.node_count as usize == n)
            {
                if let (Ok(row_offsets), Ok(col_indices)) = (
                    reader.get_row_offsets(rel_idx),
                    reader.get_col_indices(rel_idx),
                ) {
                    if rel.edge_count > 0 {
                        computed_perm = Some(if opts.rcm {
                            compute_rcm_permutation(n, row_offsets, col_indices)
                        } else {
                            compute_degree_sort_permutation(n, row_offsets)
                        });
                    }
                }
            }
        }

        let perm = computed_perm.unwrap_or_else(|| {
            let id: Vec<u32> = (0..n as u32).collect();
            (id.clone(), id)
        });

        // Permute & preserve Spec v0.10.0 inline domain attributes
        if !opts.strip_properties {
            let new_to_old = &perm.1;
            for (attr_idx, attr) in d.attributes.iter().enumerate() {
                let raw_data = reader.get_domain_attribute_by_index(d_idx, attr_idx)?;
                let raw_offsets = reader.get_domain_attribute_offsets_by_index(d_idx, attr_idx)?;
                let (new_data, new_offs) = permute_attribute_payload(
                    attr.type_code,
                    attr.dimension,
                    raw_data,
                    raw_offsets,
                    new_to_old,
                );
                writer.add_attribute_to_domain(
                    d.domain_id,
                    &attr.name,
                    attr.type_code,
                    attr.dimension,
                    new_data,
                    new_offs,
                );
            }
            if d.key_attr_index != 0xFFFF {
                writer.set_domain_key_attr(d.domain_id, d.key_attr_index);
            }
        }

        domain_perms.insert(d.domain_id, perm);
    }

    // 2. Re-add relations with CSR/CSC remapping and multi-dimensional attribute permutation
    for (idx, rel) in reader.relations().iter().enumerate() {
        if rel.is_virtual() {
            writer.add_virtual_relation(
                rel.src_domain_id,
                rel.tgt_domain_id,
                rel.node_count,
                &rel.name,
                rel.virtual_component_ids.clone(),
            );
            continue;
        }

        let (row_offsets_vec, col_indices_vec, _coo_opt) =
            crate::commands::index::extract_relation_csr_and_coo(&reader, idx, rel)?;
        let row_offsets = row_offsets_vec.as_slice();
        let col_indices = col_indices_vec.as_slice();
        let src_n = rel.node_count as usize;

        let default_src_id: Vec<u32> = (0..src_n as u32).collect();
        let (src_old_to_new, src_new_to_old) = domain_perms
            .get(&rel.src_domain_id)
            .filter(|(o2n, _)| o2n.len() == src_n)
            .map(|(o2n, n2o)| (o2n.as_slice(), n2o.as_slice()))
            .unwrap_or((default_src_id.as_slice(), default_src_id.as_slice()));

        let tgt_old_to_new = domain_perms
            .get(&rel.tgt_domain_id)
            .map(|(o2n, _)| o2n.as_slice());

        // Remap edges: (new_u, new_v, old_edge_idx)
        let mut remapped_edges: Vec<(u32, u32, usize)> = Vec::with_capacity(col_indices.len());
        for old_u in 0..src_n.min(row_offsets.len().saturating_sub(1)) {
            let start = row_offsets[old_u] as usize;
            let end = (row_offsets[old_u + 1] as usize).min(col_indices.len());
            let new_u = src_old_to_new[old_u];
            for (edge_idx, &old_v_raw) in col_indices[start..end].iter().enumerate() {
                let orig_e = start + edge_idx;
                let new_v = if let Some(t_o2n) = tgt_old_to_new {
                    if (old_v_raw as usize) < t_o2n.len() {
                        t_o2n[old_v_raw as usize]
                    } else {
                        old_v_raw
                    }
                } else {
                    old_v_raw
                };
                remapped_edges.push((new_u, new_v, orig_e));
            }
        }

        remapped_edges.sort_unstable_by(|a, b| a.0.cmp(&b.0).then_with(|| a.1.cmp(&b.1)));

        let mut new_offsets = vec![0u32; src_n + 1];
        let mut new_cols = Vec::with_capacity(remapped_edges.len());
        let mut edge_new_to_old = Vec::with_capacity(remapped_edges.len());

        for &(u, v, orig_e) in &remapped_edges {
            new_offsets[(u as usize) + 1] += 1;
            new_cols.push(v);
            edge_new_to_old.push(orig_e as u32);
        }
        for i in 0..src_n {
            new_offsets[i + 1] += new_offsets[i];
        }

        writer.add_relation(
            rel.src_domain_id,
            rel.tgt_domain_id,
            rel.node_count,
            new_cols.len() as u64,
            new_offsets,
            new_cols,
        );
        writer.set_relation_name(idx, &rel.name);
        writer.set_relation_widths(idx, rel.node_id_width, rel.edge_index_width);
        writer.set_relation_encoding(idx, rel.encoding_id);
        writer.set_relation_section_features(
            idx,
            rel.section_features & impulse_graph::spec::IMP_REL_CARDINALITY_MASK,
        );
        if opts.csc || rel.has_csc() {
            writer.set_relation_include_csc(idx, true);
        }

        // Permute and preserve node & edge attributes on this relation
        if !opts.strip_properties {
            for (attr_idx, attr) in rel.attributes.iter().enumerate() {
                let raw_data = reader.get_attribute_data(idx, attr_idx)?;
                let raw_offsets = reader.get_attribute_offsets(idx, attr_idx)?;

                let elem_stride = attribute_element_byte_stride(attr.type_code, attr.dimension);
                let is_nullable = (attr.type_code & 0x80) != 0;

                // Determine whether attribute is node-aligned (`src_n`) or edge-aligned (`rel.edge_count`)
                let is_node_attr = if let Some(offs) = raw_offsets {
                    offs.len() == src_n + 1 && src_n != rel.edge_count as usize
                } else if elem_stride > 0 && !is_nullable {
                    let count = raw_data.len() / elem_stride;
                    count == src_n && (src_n != rel.edge_count as usize || rel.edge_count == 0)
                } else {
                    rel.edge_count == 0
                };

                let perm_new_to_old: &[u32] = if is_node_attr {
                    src_new_to_old
                } else {
                    &edge_new_to_old
                };

                let (new_data, new_offs) = permute_attribute_payload(
                    attr.type_code,
                    attr.dimension,
                    raw_data,
                    raw_offsets,
                    perm_new_to_old,
                );

                writer.add_attribute_to_relation(
                    idx,
                    &attr.name,
                    attr.type_code,
                    attr.dimension,
                    new_data,
                    new_offs,
                );
            }
        }
    }

    // 3. Attach any new external embeddings requested via --embeddings
    for emb_spec_str in &opts.embeddings {
        let espec = EmbeddingAttachmentSpec::parse(emb_spec_str)?;
        let dom = reader
            .domains()
            .iter()
            .find(|d| d.name == espec.domain_name || d.domain_id.to_string() == espec.domain_name)
            .ok_or_else(|| {
                format!(
                    "Domain '{}' in --embeddings spec not found in snapshot",
                    espec.domain_name
                )
            })?;
        let loaded = load_embeddings_from_path(
            &espec.file_path,
            espec.selector.as_deref(),
            espec.normalize,
        )?;
        let n = dom.node_count as usize;
        let raw_vecs = loaded.align_to_domain(n, None)?;
        let dim = loaded.dimension;

        // Apply domain permutation `new_to_old` so the attached embeddings match the optimized node order
        let mut permuted_bytes = Vec::with_capacity(n * dim * 4);
        if let Some((_, new_to_old)) = domain_perms.get(&dom.domain_id) {
            for &old_u in new_to_old {
                let u = old_u as usize;
                for &v in &raw_vecs[u * dim..(u + 1) * dim] {
                    permuted_bytes.extend_from_slice(&v.to_le_bytes());
                }
            }
        } else {
            for &v in &raw_vecs {
                permuted_bytes.extend_from_slice(&v.to_le_bytes());
            }
        }

        writer.add_attribute_to_domain(
            dom.domain_id,
            &espec.attr_name,
            BaseDataType::Float32 as u8,
            dim as u32,
            permuted_bytes,
            None,
        );
    }

    // 4. Preserve & remap existing Section 2.6 indices (MPHF & Vamana ANN)
    let snapshot_bytes = reader.as_bytes();
    for entry in reader.index_entries() {
        if entry.index_type == IMP_INDEX_MINIMAL_PERFECT_HASH {
            if opts.strip_mappings {
                continue;
            }
            let start = entry.data_offset as usize;
            let len = entry.data_bytes as usize;
            if start + len <= snapshot_bytes.len() {
                let mut idx_blob = snapshot_bytes[start..start + len].to_vec();
                // Remap dense node IDs in MPHF table if full table is present
                if idx_blob.len() >= 32 {
                    let key_count = u64::from_le_bytes(idx_blob[0..8].try_into().unwrap()) as usize;
                    let str_bytes =
                        u32::from_le_bytes(idx_blob[16..20].try_into().unwrap()) as usize;
                    let table_start = 32 + str_bytes;
                    if let Some((old_to_new, _)) = domain_perms.get(&entry.domain_id) {
                        if table_start + key_count * 8 <= idx_blob.len() {
                            for slot in 0..key_count {
                                let off = table_start + slot * 8;
                                let k_off =
                                    u32::from_le_bytes(idx_blob[off..off + 4].try_into().unwrap());
                                if k_off != 0 {
                                    let old_nid = u32::from_le_bytes(
                                        idx_blob[off + 4..off + 8].try_into().unwrap(),
                                    ) as usize;
                                    if old_nid < old_to_new.len() {
                                        let new_nid = old_to_new[old_nid];
                                        idx_blob[off + 4..off + 8]
                                            .copy_from_slice(&new_nid.to_le_bytes());
                                    }
                                }
                            }
                        }
                    }
                }
                writer.add_index(
                    entry.domain_id,
                    entry.relation_id,
                    entry.attribute_index,
                    entry.index_type,
                    &entry.name,
                    idx_blob,
                );
            }
        } else if entry.index_type == IMP_INDEX_VAMANA_ANN {
            let hdr = reader.parse_vamana_header(entry)?;
            if opts.rebuild_vector_index {
                let metric = VamanaMetric::from_u8(hdr.metric).unwrap_or(VamanaMetric::Cosine);
                let quant =
                    VamanaQuantType::from_u8(hdr.quant_type).unwrap_or(VamanaQuantType::Sq8);
                let cfg = VamanaBuildConfig {
                    max_degree_r: hdr.max_degree_r,
                    ef_construction: hdr.ef_construction,
                    alpha: hdr.alpha,
                    metric,
                    quant_type: quant,
                    seed: 0x5641_4D41_2026_0009,
                };
                // Find attribute name by domain_id/relation_id and attribute_index
                let attr_name = if entry.relation_id == 0xFFFF {
                    reader
                        .domains()
                        .iter()
                        .find(|d| d.domain_id == hdr.domain_id)
                        .and_then(|d| d.attributes.get(entry.attribute_index as usize))
                        .map(|a| a.name.clone())
                } else {
                    reader
                        .relations()
                        .get(entry.relation_id as usize)
                        .and_then(|r| r.attributes.get(entry.attribute_index as usize))
                        .map(|a| a.name.clone())
                }
                .or_else(|| {
                    reader
                        .domains()
                        .iter()
                        .find(|d| d.domain_id == hdr.domain_id)
                        .and_then(|d| d.attributes.get(hdr.vec_attr_id as usize))
                        .map(|a| a.name.clone())
                })
                .or_else(|| {
                    reader
                        .relations()
                        .iter()
                        .find(|r| {
                            r.src_domain_id == hdr.domain_id
                                && (r.edge_count == 0
                                    || r.name.ends_with("_nodes")
                                    || r.attributes.iter().any(|a| a.dimension > 1))
                        })
                        .and_then(|r| r.attributes.get(hdr.vec_attr_id as usize))
                        .map(|a| a.name.clone())
                })
                .unwrap_or_else(|| "embedding".to_string());
                writer.add_vamana_index(hdr.domain_id, &attr_name, &entry.name, &cfg)?;
            } else {
                let adj_slab = reader.get_vamana_adj_slab(&hdr)?;
                let quant_slab = reader.get_vamana_quant_slab(&hdr)?;
                if let Some((old_to_new, new_to_old)) = domain_perms.get(&hdr.domain_id) {
                    let permuted =
                        permute_vamana_index(&hdr, adj_slab, quant_slab, old_to_new, new_to_old)?;
                    writer.add_built_vamana_index_with_relation(
                        &entry.name,
                        entry.relation_id,
                        permuted,
                    );
                } else {
                    let start = entry.data_offset as usize;
                    let len = entry.data_bytes as usize;
                    if start + len <= snapshot_bytes.len() {
                        let mut payload = snapshot_bytes[start..start + len].to_vec();
                        // Relativize absolute Vamana slab offsets before SnapshotWriter::finalize_to_writer
                        // adds the new idx.data_offset.
                        if payload.len() >= 64 {
                            let rel_adj = hdr.adj_slab_offset.saturating_sub(entry.data_offset);
                            let rel_quant = hdr.quant_code_offset.saturating_sub(entry.data_offset);
                            payload[0x28..0x30].copy_from_slice(&rel_adj.to_le_bytes());
                            payload[0x38..0x40].copy_from_slice(&rel_quant.to_le_bytes());
                        }
                        writer.add_index(
                            entry.domain_id,
                            entry.relation_id,
                            entry.attribute_index,
                            entry.index_type,
                            &entry.name,
                            payload,
                        );
                    }
                }
            }
        } else {
            let start = entry.data_offset as usize;
            let len = entry.data_bytes as usize;
            if start + len <= snapshot_bytes.len() {
                writer.add_index(
                    entry.domain_id,
                    entry.relation_id,
                    entry.attribute_index,
                    entry.index_type,
                    &entry.name,
                    snapshot_bytes[start..start + len].to_vec(),
                );
            }
        }
    }

    // 5. Build any additional Vamana indices requested via --vector-index
    for v_spec_str in &opts.vector_indices {
        let vspec = VectorIndexSpec::parse(v_spec_str)?;
        let dom = reader
            .domains()
            .iter()
            .find(|d| d.name == vspec.domain_name || d.domain_id.to_string() == vspec.domain_name)
            .ok_or_else(|| {
                format!(
                    "Domain '{}' in --vector-index spec not found in snapshot",
                    vspec.domain_name
                )
            })?;
        writer.add_vamana_index(
            dom.domain_id,
            &vspec.attr_name,
            &vspec.index_name,
            &vspec.config,
        )?;
    }

    writer.finalize()?;
    println!(
        "SUCCESS: Optimized snapshot saved to {}",
        output_path.display()
    );
    Ok(())
}

fn attribute_element_byte_stride(type_code: u8, dimension: u32) -> usize {
    let base_type = type_code & IMPULSE_TYPE_MASK;
    let base_bytes = match base_type {
        0x01 => 1,
        0x02 | 0x05 => 2,
        0x03 | 0x06 => 4,
        0x04 | 0x07 | 0x08 | 0x09 | 0x0D => 8,
        0x0E => 16,
        0x0A => 1,
        _ => 0,
    };
    base_bytes * (dimension.max(1) as usize)
}

fn permute_attribute_payload(
    type_code: u8,
    dimension: u32,
    raw_data: &[u8],
    raw_offsets: Option<&[u32]>,
    new_to_old: &[u32],
) -> (Vec<u8>, Option<Vec<u32>>) {
    let count = new_to_old.len();
    let is_nullable = (type_code & 0x80) != 0;
    let base_type = type_code & IMPULSE_TYPE_MASK;

    let (bitmap_bytes_padded, payload_slice) = if is_nullable {
        let word_count = count.div_ceil(64);
        let raw_bm = word_count * 8;
        let padded_bm = if raw_bm == 0 {
            0
        } else {
            (raw_bm + 127) & !127
        };
        if padded_bm <= raw_data.len() {
            (padded_bm, &raw_data[padded_bm..])
        } else {
            (0, raw_data)
        }
    } else {
        (0, raw_data)
    };

    let mut new_bitmap_buf = if bitmap_bytes_padded > 0 {
        let word_count = count.div_ceil(64);
        let mut new_words = vec![0u64; word_count];
        for (new_idx, &old_u) in new_to_old.iter().enumerate() {
            let old_idx = old_u as usize;
            let byte_pos = old_idx / 8;
            let bit_pos = old_idx % 8;
            if byte_pos < bitmap_bytes_padded && (raw_data[byte_pos] & (1u8 << bit_pos)) != 0 {
                new_words[new_idx / 64] |= 1u64 << (new_idx % 64);
            }
        }
        let mut bm = Vec::with_capacity(bitmap_bytes_padded);
        for w in new_words {
            bm.extend_from_slice(&w.to_le_bytes());
        }
        bm.resize(bitmap_bytes_padded, 0u8);
        bm
    } else {
        Vec::new()
    };

    if base_type == 0x0B || base_type == 0x0C {
        if let Some(offs) = raw_offsets {
            let mut new_offs = Vec::with_capacity(count + 1);
            let mut new_blob = Vec::with_capacity(payload_slice.len());
            new_offs.push(0u32);
            for &old_u in new_to_old {
                let u = old_u as usize;
                if u + 1 < offs.len() {
                    let s = offs[u] as usize;
                    let e = (offs[u + 1] as usize).min(payload_slice.len());
                    if s <= e {
                        new_blob.extend_from_slice(&payload_slice[s..e]);
                    }
                }
                new_offs.push(new_blob.len() as u32);
            }
            new_bitmap_buf.extend_from_slice(&new_blob);
            return (new_bitmap_buf, Some(new_offs));
        }
    }

    let stride = attribute_element_byte_stride(type_code, dimension);
    if stride == 0 || payload_slice.len() < count * stride {
        return (raw_data.to_vec(), raw_offsets.map(|o| o.to_vec()));
    }

    let mut new_payload = vec![0u8; count * stride];
    for (new_u, &old_u) in new_to_old.iter().enumerate() {
        let old_idx = old_u as usize;
        if (old_idx + 1) * stride <= payload_slice.len() {
            new_payload[new_u * stride..(new_u + 1) * stride]
                .copy_from_slice(&payload_slice[old_idx * stride..(old_idx + 1) * stride]);
        }
    }
    new_bitmap_buf.extend_from_slice(&new_payload);
    (new_bitmap_buf, None)
}

/// Computes Reverse Cuthill-McKee (RCM) node permutation `(old_to_new, new_to_old)`.
pub fn compute_rcm_permutation(
    node_count: usize,
    row_offsets: &[u32],
    col_indices: &[u32],
) -> (Vec<u32>, Vec<u32>) {
    if node_count == 0 || row_offsets.len() <= node_count {
        let id: Vec<u32> = (0..node_count as u32).collect();
        return (id.clone(), id);
    }
    let mut visited = vec![false; node_count];
    let mut order: Vec<u32> = Vec::with_capacity(node_count);

    let mut degrees: Vec<(usize, u32)> = (0..node_count)
        .map(|i| (i, row_offsets[i + 1].saturating_sub(row_offsets[i])))
        .collect();
    degrees.sort_by_key(|&(_, deg)| deg);

    for (start_node, _) in degrees {
        if visited[start_node] {
            continue;
        }

        let mut queue = VecDeque::new();
        queue.push_back(start_node);
        visited[start_node] = true;

        while let Some(u) = queue.pop_front() {
            order.push(u as u32);

            let start = (row_offsets[u] as usize).min(col_indices.len());
            let end = (row_offsets[u + 1] as usize).min(col_indices.len());
            let mut nbrs: Vec<usize> = if start <= end {
                (start..end)
                    .map(|k| col_indices[k] as usize)
                    .filter(|&v| v < node_count && !visited[v])
                    .collect()
            } else {
                Vec::new()
            };

            nbrs.sort_by_key(|&v| row_offsets[v + 1].saturating_sub(row_offsets[v]));

            for v in nbrs {
                if !visited[v] {
                    visited[v] = true;
                    queue.push_back(v);
                }
            }
        }
    }

    order.reverse();

    let mut old_to_new = vec![0u32; node_count];
    for (new_id, &old_id) in order.iter().enumerate() {
        old_to_new[old_id as usize] = new_id as u32;
    }
    (old_to_new, order)
}

/// Computes Degree-Sorted node permutation `(old_to_new, new_to_old)`.
pub fn compute_degree_sort_permutation(
    node_count: usize,
    row_offsets: &[u32],
) -> (Vec<u32>, Vec<u32>) {
    if node_count == 0 || row_offsets.len() <= node_count {
        let id: Vec<u32> = (0..node_count as u32).collect();
        return (id.clone(), id);
    }
    let mut degrees: Vec<(usize, u32)> = (0..node_count)
        .map(|i| (i, row_offsets[i + 1].saturating_sub(row_offsets[i])))
        .collect();

    degrees.sort_by_key(|&(_, deg)| std::cmp::Reverse(deg));

    let mut old_to_new = vec![0u32; node_count];
    let mut new_to_old = Vec::with_capacity(node_count);
    for (new_id, &(old_id, _)) in degrees.iter().enumerate() {
        old_to_new[old_id] = new_id as u32;
        new_to_old.push(old_id as u32);
    }
    (old_to_new, new_to_old)
}
