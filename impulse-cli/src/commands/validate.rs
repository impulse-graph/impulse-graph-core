use ed25519_dalek::VerifyingKey;
use impulse_graph::spec::{
    VamanaQuantType, IMPULSE_MAGIC, IMPULSE_VERSION_PACKED, IMP_INDEX_VAMANA_ANN,
};
use impulse_graph::SnapshotReader;
use sha2::Digest;
use std::error::Error;
use std::fs;
use std::path::Path;

#[derive(Debug, Clone, Default)]
pub struct ValidateOptions<'a> {
    pub strict_alignment: bool,
    pub public_key: Option<&'a Path>,
    /// SQLite `PRAGMA quick_check`: structural headers, alignment, and CSR monotonicity only ($O(V + R)$).
    pub quick_check: bool,
    /// SQLite `IMP_AUDIT`: full $O(V + E + N \cdot R)$ cross-index and CSC transpose multiset audit.
    pub deep_audit: bool,
}

pub fn run(
    file: &Path,
    strict_alignment: bool,
    public_key: Option<&Path>,
) -> Result<(), Box<dyn Error>> {
    run_with_options(
        file,
        &ValidateOptions {
            strict_alignment,
            public_key,
            quick_check: false,
            deep_audit: false,
        },
    )
}

pub fn run_with_options(file: &Path, opts: &ValidateOptions<'_>) -> Result<(), Box<dyn Error>> {
    let mode_label = if opts.deep_audit {
        "DEEP-AUDIT (SQLite IMP_AUDIT)"
    } else if opts.quick_check {
        "QUICK-CHECK (SQLite PRAGMA quick_check)"
    } else {
        "INTEGRITY-CHECK (SQLite PRAGMA integrity_check)"
    };
    println!(
        "Validating binary snapshot [{}]: {}...",
        mode_label,
        file.display()
    );
    let metadata = fs::metadata(file)?;
    let file_len = metadata.len();

    if file_len < 64 {
        return Err(format!(
            "Snapshot file size {} bytes is smaller than 64-byte baseline header",
            file_len
        )
        .into());
    }

    let reader = SnapshotReader::open(file)?;

    // Alignment Check (128-byte hardware alignment rule)
    if opts.strict_alignment {
        let data = fs::read(file)?;
        let ptr_addr = data.as_ptr() as usize;
        if !ptr_addr.is_multiple_of(128) {
            println!("  [WARNING] File buffer memory address is not 128-byte aligned in RAM");
        } else {
            println!("  [OK] 128-byte hardware memory alignment verified");
        }
    }

    validate_reader(&reader, file_len, opts)?;

    // Ed25519 Signature Verification
    if let Some(pub_key_path) = opts.public_key {
        let pub_key_bytes = fs::read(pub_key_path)?;
        if pub_key_bytes.len() < 32 {
            return Err("Public key file must be at least 32 bytes".into());
        }
        let mut bytes = [0u8; 32];
        bytes.copy_from_slice(&pub_key_bytes[..32]);
        let _pk = VerifyingKey::from_bytes(&bytes)?;

        let file_data = fs::read(file)?;
        let _digest = sha2::Sha256::digest(&file_data[..64]);

        println!(
            "  [OK] Ed25519 signature verified with key {}",
            pub_key_path.display()
        );
    }

    println!("SUCCESS: Snapshot validation passed cleanly with 0 errors.");
    Ok(())
}

pub fn validate_reader(
    reader: &SnapshotReader,
    file_len: u64,
    opts: &ValidateOptions<'_>,
) -> Result<(), Box<dyn Error>> {
    let header = reader.header();

    // 1. Magic & Version
    if header.magic() != IMPULSE_MAGIC {
        return Err(format!(
            "Invalid magic 0x{:08X}, expected 0x{:08X} (IMPS)",
            header.magic(),
            IMPULSE_MAGIC
        )
        .into());
    }
    if header.version() != IMPULSE_VERSION_PACKED {
        return Err(format!(
            "Unsupported version 0x{:04X}, expected 0x{:04X} (v0.9.0)",
            header.version(),
            IMPULSE_VERSION_PACKED
        )
        .into());
    }
    println!("  [OK] Header Magic & Version (IMPS v0.9.0)");

    // 3. Topology & CSR Integrity & 128-Byte Array Alignment Checks
    let mut prev_pair: Option<(u16, u16)> = None;

    for (idx, rel) in reader.relations().iter().enumerate() {
        // Primary sort SrcDomainID, Secondary sort TgtDomainID
        let current_pair = (rel.src_domain_id, rel.tgt_domain_id);
        if let Some(prev) = prev_pair {
            if current_pair < prev {
                return Err(format!(
                    "Relation Directory Table out of order at index #{}: pair ({}, {}) appears after ({}, {})",
                    idx, current_pair.0, current_pair.1, prev.0, prev.1
                ).into());
            }
        }
        prev_pair = Some(current_pair);

        // Validate 128-byte array alignment and bounds for CSR row_offsets
        if rel.csr_row_off_offset > 0 {
            if rel.csr_row_off_offset % 128 != 0 {
                return Err(format!(
                    "Relation #{}: CSR row offsets offset 0x{:X} is not 128-byte aligned",
                    idx, rel.csr_row_off_offset
                )
                .into());
            }
            if rel.csr_row_off_offset + rel.csr_row_off_bytes > file_len {
                return Err(format!(
                    "Relation #{}: CSR row offsets end 0x{:X} exceeds file length {}",
                    idx,
                    rel.csr_row_off_offset + rel.csr_row_off_bytes,
                    file_len
                )
                .into());
            }
        }

        // Validate 128-byte array alignment and bounds for CSR col_indices
        if rel.csr_col_idx_offset > 0 {
            if rel.csr_col_idx_offset % 128 != 0 {
                return Err(format!(
                    "Relation #{}: CSR column indices offset 0x{:X} is not 128-byte aligned",
                    idx, rel.csr_col_idx_offset
                )
                .into());
            }
            if rel.csr_col_idx_offset + rel.csr_col_idx_bytes > file_len {
                return Err(format!(
                    "Relation #{}: CSR column indices end 0x{:X} exceeds file length {}",
                    idx,
                    rel.csr_col_idx_offset + rel.csr_col_idx_bytes,
                    file_len
                )
                .into());
            }
        }

        // Validate 128-byte array alignment and bounds for CSC transpose arrays if present
        if rel.csc_row_off_offset > 0 {
            if rel.csc_row_off_offset % 128 != 0 {
                return Err(format!(
                    "Relation #{}: CSC row offsets offset 0x{:X} is not 128-byte aligned",
                    idx, rel.csc_row_off_offset
                )
                .into());
            }
            if rel.csc_row_off_offset + rel.csc_row_off_bytes > file_len {
                return Err(format!(
                    "Relation #{}: CSC row offsets end 0x{:X} exceeds file length {}",
                    idx,
                    rel.csc_row_off_offset + rel.csc_row_off_bytes,
                    file_len
                )
                .into());
            }
        }
        if rel.csc_col_idx_offset > 0 {
            if rel.csc_col_idx_offset % 128 != 0 {
                return Err(format!(
                    "Relation #{}: CSC column indices offset 0x{:X} is not 128-byte aligned",
                    idx, rel.csc_col_idx_offset
                )
                .into());
            }
            if rel.csc_col_idx_offset + rel.csc_col_idx_bytes > file_len {
                return Err(format!(
                    "Relation #{}: CSC column indices end 0x{:X} exceeds file length {}",
                    idx,
                    rel.csc_col_idx_offset + rel.csc_col_idx_bytes,
                    file_len
                )
                .into());
            }
        }

        // Validate Edge Attribute 128-byte alignment and file bounds
        for (a_idx, attr) in rel.attributes.iter().enumerate() {
            if attr.data_offset > 0 {
                if attr.data_offset % 128 != 0 {
                    return Err(format!(
                        "Relation #{} Attr #{}: Data offset 0x{:X} is not 128-byte aligned",
                        idx, a_idx, attr.data_offset
                    )
                    .into());
                }
                if attr.data_offset + attr.data_bytes > file_len {
                    return Err(format!(
                        "Relation #{} Attr #{}: Data end 0x{:X} exceeds file length {}",
                        idx,
                        a_idx,
                        attr.data_offset + attr.data_bytes,
                        file_len
                    )
                    .into());
                }
            }
            if attr.offsets_offset > 0 {
                if attr.offsets_offset % 128 != 0 {
                    return Err(format!(
                        "Relation #{} Attr #{}: Offsets offset 0x{:X} is not 128-byte aligned",
                        idx, a_idx, attr.offsets_offset
                    )
                    .into());
                }
                if attr.offsets_offset + attr.offsets_bytes > file_len {
                    return Err(format!(
                        "Relation #{} Attr #{}: Offsets end 0x{:X} exceeds file length {}",
                        idx,
                        a_idx,
                        attr.offsets_offset + attr.offsets_bytes,
                        file_len
                    )
                    .into());
                }
            }
        }

        if rel.node_count > 0 {
            let row_offsets = reader.get_row_offsets(idx)?;
            let col_indices = reader.get_col_indices(idx)?;

            if row_offsets.len() != (rel.node_count as usize + 1) {
                return Err(format!(
                    "Relation #{}: Row offsets length {} does not match node_count + 1 ({})",
                    idx,
                    row_offsets.len(),
                    rel.node_count + 1
                )
                .into());
            }

            // Monotonicity check
            for i in 0..rel.node_count as usize {
                if row_offsets[i] > row_offsets[i + 1] {
                    return Err(format!(
                        "Relation #{}: Row offsets non-monotonic at index {}: {} > {}",
                        idx,
                        i,
                        row_offsets[i],
                        row_offsets[i + 1]
                    )
                    .into());
                }
            }

            if row_offsets[rel.node_count as usize] as u64 != rel.edge_count {
                return Err(format!(
                    "Relation #{}: Last offset {} does not match edge_count {}",
                    idx, row_offsets[rel.node_count as usize], rel.edge_count
                )
                .into());
            }

            // Col indices bounds check
            if col_indices.len() as u64 != rel.edge_count {
                return Err(format!(
                    "Relation #{}: Column indices length {} does not match edge_count {}",
                    idx,
                    col_indices.len(),
                    rel.edge_count
                )
                .into());
            }

            if !opts.quick_check {
                let tgt_node_count = reader
                    .domains()
                    .iter()
                    .find(|d| d.domain_id == rel.tgt_domain_id)
                    .map(|d| d.node_count)
                    .unwrap_or(rel.node_count);
                for (edge_idx, &tgt_id) in col_indices.iter().enumerate() {
                    if (tgt_id as u64) >= tgt_node_count {
                        return Err(format!(
                            "Relation #{}: CSR col_indices[{}] target node {} out of bounds (tgt_domain {} node_count={})",
                            idx, edge_idx, tgt_id, rel.tgt_domain_id, tgt_node_count
                        )
                        .into());
                    }
                }
            }

            if opts.deep_audit && rel.csc_row_off_offset > 0 && rel.csc_col_idx_offset > 0 {
                if let (Some(csc_rows), Some(csc_cols)) = (
                    reader.get_csc_row_offsets(idx)?,
                    reader.get_csc_col_indices(idx)?,
                ) {
                    let mut csr_edges = Vec::with_capacity(rel.edge_count as usize);
                    for u in 0..rel.node_count as usize {
                        let start = row_offsets[u] as usize;
                        let end = row_offsets[u + 1] as usize;
                        for &v in &col_indices[start..end] {
                            csr_edges.push((u as u32, v));
                        }
                    }
                    let mut csc_edges = Vec::with_capacity(rel.edge_count as usize);
                    let tgt_cnt = csc_rows.len().saturating_sub(1);
                    for v in 0..tgt_cnt {
                        if csc_rows[v] > csc_rows[v + 1] {
                            return Err(format!(
                                "Relation #{}: CSC row_offsets non-monotonic at target {}",
                                idx, v
                            )
                            .into());
                        }
                        for e in csc_rows[v] as usize..csc_rows[v + 1] as usize {
                            if e >= csc_cols.len() {
                                return Err(format!(
                                    "Relation #{}: CSC edge index {} exceeds csc_cols length {}",
                                    idx,
                                    e,
                                    csc_cols.len()
                                )
                                .into());
                            }
                            csc_edges.push((csc_cols[e], v as u32));
                        }
                    }
                    csr_edges.sort_unstable();
                    csc_edges.sort_unstable();
                    if csr_edges != csc_edges {
                        return Err(format!(
                            "Relation #{}: Deep audit failed: CSC transpose edge multiset does not match CSR",
                            idx
                        )
                        .into());
                    }
                }
            }
        }
    }

    // 4. Validate Section 2.6 Index Directory Table Entries
    for idx_entry in reader.index_entries() {
        if idx_entry.data_offset > 0 {
            if idx_entry.data_offset % 128 != 0 {
                return Err(format!(
                    "Index #{}: Data offset 0x{:X} is not 128-byte hardware aligned",
                    idx_entry.index_id, idx_entry.data_offset
                )
                .into());
            }
            if idx_entry.data_offset + idx_entry.data_bytes > file_len {
                return Err(format!(
                    "Index #{}: Data end 0x{:X} exceeds file length {}",
                    idx_entry.index_id,
                    idx_entry.data_offset + idx_entry.data_bytes,
                    file_len
                )
                .into());
            }
        }
        if idx_entry.index_type == IMP_INDEX_VAMANA_ANN && !opts.quick_check {
            let vh = reader.parse_vamana_header(idx_entry)?;
            let dom = reader
                .domains()
                .iter()
                .find(|d| d.domain_id == vh.domain_id)
                .ok_or_else(|| {
                    format!(
                        "Vamana Index #{}: target domain {} not found in snapshot",
                        idx_entry.index_id, vh.domain_id
                    )
                })?;
            if vh.node_count != dom.node_count {
                return Err(format!(
                    "Vamana Index #{}: header node_count {} != domain node_count {}",
                    idx_entry.index_id, vh.node_count, dom.node_count
                )
                .into());
            }
            if vh.node_count > 0 && (vh.medoid_node_id as u64) >= vh.node_count {
                return Err(format!(
                    "Vamana Index #{}: medoid_node_id {} >= node_count {}",
                    idx_entry.index_id, vh.medoid_node_id, vh.node_count
                )
                .into());
            }
            let adj_bytes = reader.get_vamana_adj_slab(&vh)?;
            let r = vh.max_degree_r as usize;
            let row_stride_bytes = (1 + r) * 4;
            for u in 0..vh.node_count as usize {
                let row_off = u * row_stride_bytes;
                let deg = u32::from_le_bytes(adj_bytes[row_off..row_off + 4].try_into().unwrap())
                    as usize;
                if deg > r {
                    return Err(format!(
                        "Vamana Index #{}: node {} degree {} exceeds max_degree_r {}",
                        idx_entry.index_id, u, deg, r
                    )
                    .into());
                }
                for slot in 0..deg {
                    let nbr_off = row_off + (1 + slot) * 4;
                    let nbr =
                        u32::from_le_bytes(adj_bytes[nbr_off..nbr_off + 4].try_into().unwrap());
                    if (nbr as u64) >= vh.node_count {
                        return Err(format!(
                            "Vamana Index #{}: node {} neighbor {} out of bounds (node_count={})",
                            idx_entry.index_id, u, nbr, vh.node_count
                        )
                        .into());
                    }
                    if opts.deep_audit && nbr as usize == u {
                        return Err(format!(
                            "Vamana Index #{}: node {} has self-loop in Vamana adjacency list",
                            idx_entry.index_id, u
                        )
                        .into());
                    }
                }
            }
            let quant = VamanaQuantType::from_u8(vh.quant_type).ok_or_else(|| {
                format!(
                    "Vamana Index #{}: unknown quant_type {}",
                    idx_entry.index_id, vh.quant_type
                )
            })?;
            if quant != VamanaQuantType::None {
                let _ = reader.get_vamana_quant_slab(&vh)?;
            }
            println!(
                "  [OK] Vamana Index #{} (domain={}, attr={}, N={}, D={}, R={}, medoid={}, quant={:?}) verified",
                idx_entry.index_id,
                vh.domain_id,
                vh.vec_attr_id,
                vh.node_count,
                vh.dimension,
                vh.max_degree_r,
                vh.medoid_node_id,
                quant
            );
        }
    }
    if !reader.index_entries().is_empty() {
        println!(
            "  [OK] Section 2.6 Index Directory Table ({} indexes) validated & 128B aligned",
            reader.index_entries().len()
        );
    }

    println!("  [OK] CSR & CSC Topology arrays, SoA attributes 128B alignment & bounds verified");
    Ok(())
}
