//! Vamana (DiskANN) Single-Layer Graph Index Builder, Permuter & Two-Stage Quantized Search
//!
//! Implements Section 2.6 `IMP_INDEX_VAMANA_ANN = 8` (`0x08`) in pure zero-dependency Rust,
//! maintaining 100% binary layout and numerical parity with OCaml `snapshot.ml` and C++ `ImpulseVM`.

use crate::assert as imp_assert;
pub use crate::spec::{VamanaHeader, VamanaMetric, VamanaQuantType, VAMANA_MAGIC, VAMANA_VERSION};
use std::cmp::Ordering;

#[derive(Debug, Clone)]
pub struct VamanaBuildConfig {
    pub max_degree_r: u32,
    pub ef_construction: u32,
    pub alpha: f32,
    pub metric: VamanaMetric,
    pub quant_type: VamanaQuantType,
    pub seed: u64,
}

impl Default for VamanaBuildConfig {
    fn default() -> Self {
        Self {
            max_degree_r: 32,
            ef_construction: 100,
            alpha: 1.2,
            metric: VamanaMetric::Cosine,
            quant_type: VamanaQuantType::Sq8,
            seed: 0x5641_4D41_2026_0009,
        }
    }
}

impl VamanaBuildConfig {
    /// Parses a comma-separated parameter string such as `"r=32,l=100,alpha=1.2,metric=cosine,quant=sq8"`.
    pub fn parse_params(params: &str) -> Result<Self, String> {
        let mut cfg = Self::default();
        if params.trim().is_empty() {
            return Ok(cfg);
        }
        for part in params.split([',', ':']) {
            let part = part.trim();
            if part.is_empty() {
                continue;
            }
            let (k, v) = part
                .split_once('=')
                .ok_or_else(|| format!("Invalid Vamana parameter '{}' (expected key=value)", part))?;
            let k = k.trim().to_ascii_lowercase();
            let v = v.trim().to_ascii_lowercase();
            match k.as_str() {
                "r" | "max_degree" | "max_degree_r" | "degree" => {
                    cfg.max_degree_r = v
                        .parse::<u32>()
                        .map_err(|_| format!("Invalid max_degree_r '{}'", v))?;
                    if cfg.max_degree_r == 0 {
                        return Err("Vamana max_degree_r must be > 0".to_string());
                    }
                }
                "l" | "ef" | "ef_construction" | "beam" => {
                    cfg.ef_construction = v
                        .parse::<u32>()
                        .map_err(|_| format!("Invalid ef_construction '{}'", v))?;
                    if cfg.ef_construction == 0 {
                        return Err("Vamana ef_construction must be > 0".to_string());
                    }
                }
                "alpha" => {
                    cfg.alpha = v
                        .parse::<f32>()
                        .map_err(|_| format!("Invalid alpha '{}'", v))?;
                    if cfg.alpha < 1.0 {
                        return Err("Vamana alpha must be >= 1.0".to_string());
                    }
                }
                "metric" | "dist" => {
                    cfg.metric = match v.as_str() {
                        "l2" | "euclidean" => VamanaMetric::L2,
                        "cosine" | "cos" => VamanaMetric::Cosine,
                        "dot" | "ip" | "inner_product" => VamanaMetric::Dot,
                        "hamming" | "ham" => VamanaMetric::Hamming,
                        _ => return Err(format!("Unsupported Vamana metric '{}'", v)),
                    };
                }
                "quant" | "quant_type" | "routing_quant" => {
                    cfg.quant_type = match v.as_str() {
                        "none" | "fp32" | "full" => VamanaQuantType::None,
                        "bq1" | "binary" | "1bit" => VamanaQuantType::Bq1,
                        "sq8" | "int8" | "uint8" | "8bit" => VamanaQuantType::Sq8,
                        "fp16" | "f16" | "half" => VamanaQuantType::Fp16,
                        _ => return Err(format!("Unsupported Vamana quant_type '{}'", v)),
                    };
                }
                "seed" => {
                    cfg.seed = v
                        .parse::<u64>()
                        .map_err(|_| format!("Invalid seed '{}'", v))?;
                }
                _ => {
                    return Err(format!("Unknown Vamana build parameter '{}'", k));
                }
            }
        }
        Ok(cfg)
    }
}

#[derive(Debug, Clone)]
pub struct BuiltVamanaIndex {
    pub header: VamanaHeader,
    pub adj_slab: Vec<u8>,
    pub quant_slab: Vec<u8>,
}

/// Decodes an IEEE-754 binary16 (FP16) 16-bit unsigned integer into `f32`.
#[inline]
pub fn f32_from_fp16(h: u16) -> f32 {
    let sign = ((h >> 15) & 1) as u32;
    let exp = ((h >> 10) & 0x1F) as i32;
    let frac = (h & 0x03FF) as u32;
    let bits: u32 = if exp == 0 {
        if frac == 0 {
            sign << 31
        } else {
            // Subnormal FP16 -> normalized FP32
            let mut m = frac;
            let mut e = -14i32;
            while (m & 0x0400) == 0 {
                m <<= 1;
                e -= 1;
            }
            m &= 0x03FF;
            let exp32 = (e + 127) as u32;
            (sign << 31) | (exp32 << 23) | (m << 13)
        }
    } else if exp == 0x1F {
        let frac32 = if frac == 0 { 0 } else { 0x0040_0000 | (frac << 13) };
        (sign << 31) | (0xFF << 23) | frac32
    } else {
        let exp32 = (exp - 15 + 127) as u32;
        (sign << 31) | (exp32 << 23) | (frac << 13)
    };
    f32::from_bits(bits)
}

/// Encodes an `f32` into an IEEE-754 binary16 (FP16) 16-bit unsigned integer.
#[inline]
pub fn fp16_from_f32(f: f32) -> u16 {
    let bits = f.to_bits();
    let sign = ((bits >> 31) & 1) as u16;
    let exp32 = ((bits >> 23) & 0xFF) as i32;
    let mant32 = bits & 0x007F_FFFF;
    if exp32 == 0xFF {
        let frac16 = if mant32 == 0 { 0 } else { 0x0200 };
        (sign << 15) | (0x1F << 10) | frac16
    } else {
        let unb_exp = exp32 - 127 + 15;
        if unb_exp >= 0x1F {
            (sign << 15) | (0x1F << 10)
        } else if unb_exp <= 0 {
            if unb_exp < -10 {
                sign << 15
            } else {
                let mant_full = 0x0080_0000 | mant32;
                let shift = (14 - unb_exp) as u32;
                let frac16 = ((mant_full >> shift) & 0x03FF) as u16;
                (sign << 15) | frac16
            }
        } else {
            let frac16 = ((mant32 >> 13) & 0x03FF) as u16;
            (sign << 15) | ((unb_exp as u16) << 10) | frac16
        }
    }
}

/// Computes `(score, distance)` between `q` and `v` where higher `score` is more similar
/// and lower `distance` is closer (matching OCaml `Snapshot.compute_vector_metric`).
#[inline]
pub fn compute_vector_metric(metric: VamanaMetric, q: &[f32], v: &[f32]) -> (f32, f32) {
    let dim = q.len().min(v.len());
    match metric {
        VamanaMetric::L2 => {
            let mut sum_sq = 0.0f32;
            for i in 0..dim {
                let d = q[i] - v[i];
                sum_sq += d * d;
            }
            (-sum_sq, sum_sq)
        }
        VamanaMetric::Dot => {
            let mut dot = 0.0f32;
            for i in 0..dim {
                dot += q[i] * v[i];
            }
            (dot, -dot)
        }
        VamanaMetric::Cosine => {
            let mut dot = 0.0f32;
            let mut nq = 0.0f32;
            let mut nv = 0.0f32;
            for i in 0..dim {
                dot += q[i] * v[i];
                nq += q[i] * q[i];
                nv += v[i] * v[i];
            }
            let denom = nq.sqrt() * nv.sqrt();
            let cos_sim = if denom <= 1e-12 { 0.0 } else { dot / denom };
            (cos_sim, 1.0 - cos_sim)
        }
        VamanaMetric::Hamming => {
            let mut ham = 0u32;
            for i in 0..dim {
                let bq = q[i] >= 0.0;
                let bv = v[i] >= 0.0;
                if bq != bv {
                    ham += 1;
                }
            }
            let d = ham as f32;
            (-d, d)
        }
    }
}

#[inline]
fn compare_cand(a: &(u32, f32, f32), b: &(u32, f32, f32)) -> Ordering {
    a.2.total_cmp(&b.2).then_with(|| a.0.cmp(&b.0))
}

/// Encodes quantized routing slab (`BQ1`, `SQ8`, `FP16`) for `vectors` (`node_count * dimension`).
pub fn encode_quant_slab(
    vectors: &[f32],
    node_count: usize,
    dimension: usize,
    quant_type: VamanaQuantType,
    metric: VamanaMetric,
) -> Vec<u8> {
    let row_bytes = quant_type.row_bytes(dimension);
    if row_bytes == 0 || node_count == 0 {
        return Vec::new();
    }
    let mut slab = vec![0u8; node_count * row_bytes];
    match quant_type {
        VamanaQuantType::None => {}
        VamanaQuantType::Bq1 => {
            for u in 0..node_count {
                let vec = &vectors[u * dimension..(u + 1) * dimension];
                let row_out = &mut slab[u * row_bytes..(u + 1) * row_bytes];
                for (d, &val) in vec.iter().enumerate() {
                    if val >= 0.0 {
                        row_out[d >> 3] |= 1u8 << (d & 7);
                    }
                }
            }
        }
        VamanaQuantType::Sq8 => {
            // OCaml snapshot.ml decodes SQ8 via `(code - 128.0) / 127.0` in [-1.0, 1.0].
            // For Cosine, scaling a vector to max_abs <= 1.0 preserves exact direction;
            // for L2/Dot, we clamp to [-1.0, 1.0] (or normalize if Cosine).
            for u in 0..node_count {
                let vec = &vectors[u * dimension..(u + 1) * dimension];
                let row_out = &mut slab[u * row_bytes..(u + 1) * row_bytes];
                let scale = if metric == VamanaMetric::Cosine {
                    let max_abs = vec.iter().fold(0.0f32, |m, &x| m.max(x.abs()));
                    if max_abs > 1.0 {
                        1.0 / max_abs
                    } else {
                        1.0
                    }
                } else {
                    1.0
                };
                for d in 0..dimension {
                    let v = (vec[d] * scale).clamp(-1.0, 1.0);
                    let code = (v * 127.0 + 128.0).round().clamp(0.0, 255.0) as u8;
                    row_out[d] = code;
                }
            }
        }
        VamanaQuantType::Fp16 => {
            for u in 0..node_count {
                let vec = &vectors[u * dimension..(u + 1) * dimension];
                let row_out = &mut slab[u * row_bytes..(u + 1) * row_bytes];
                for (d, &val) in vec.iter().enumerate() {
                    let h = fp16_from_f32(val);
                    row_out[d * 2..d * 2 + 2].copy_from_slice(&h.to_le_bytes());
                }
            }
        }
    }
    slab
}

/// Builds a Vamana (`IMP_INDEX_VAMANA_ANN = 8`) index over `vectors` (`[node_count, dimension]`).
pub fn build_vamana_index(
    domain_id: u16,
    vec_attr_id: u16,
    vectors: &[f32],
    node_count: usize,
    dimension: usize,
    cfg: &VamanaBuildConfig,
) -> Result<BuiltVamanaIndex, String> {
    if dimension == 0 {
        return Err("Vamana dimension must be > 0".to_string());
    }
    if vectors.len() != node_count.saturating_mul(dimension) {
        return Err(format!(
            "Vamana vector slice length mismatch: got {}, expected {} * {} = {}",
            vectors.len(),
            node_count,
            dimension,
            node_count.saturating_mul(dimension)
        ));
    }
    let r = cfg.max_degree_r.max(1) as usize;
    let l_build = cfg.ef_construction.max(cfg.max_degree_r).max(1) as usize;

    // 1. Compute dataset centroid & exact medoid node m*
    let mut centroid = vec![0.0f32; dimension];
    if node_count > 0 {
        for u in 0..node_count {
            let v = &vectors[u * dimension..(u + 1) * dimension];
            for d in 0..dimension {
                centroid[d] += v[d];
            }
        }
        let inv_n = 1.0 / (node_count as f32);
        for c in &mut centroid {
            *c *= inv_n;
        }
    }

    let mut medoid_node_id = 0u32;
    let mut best_medoid_dist = f32::INFINITY;
    for u in 0..node_count {
        let v = &vectors[u * dimension..(u + 1) * dimension];
        let (_score, dist) = compute_vector_metric(cfg.metric, &centroid, v);
        if dist < best_medoid_dist {
            best_medoid_dist = dist;
            medoid_node_id = u as u32;
        }
    }

    // 2. Initialize deterministic R-regular out-neighborhood graph
    let mut adj: Vec<Vec<u32>> = vec![Vec::with_capacity(r); node_count];
    if node_count > 1 {
        let init_deg = r.min(node_count - 1);
        for (u, nbrs) in adj.iter_mut().enumerate() {
            let mut state = cfg
                .seed
                .wrapping_add((u as u64).wrapping_mul(0x9E37_79B9_7F4A_7C15));
            let mut step = 1usize;
            while nbrs.len() < init_deg {
                state = state
                    .wrapping_mul(6364136223846793005)
                    .wrapping_add(1442695040888963407);
                let cand = if nbrs.len() < 2 {
                    // Always include deterministic ring/stride neighbors for guaranteed connectivity
                    (u + step) % node_count
                } else {
                    ((state >> 32) as usize) % node_count
                };
                step += 1;
                if cand != u && !nbrs.contains(&(cand as u32)) {
                    nbrs.push(cand as u32);
                } else if step > node_count * 2 {
                    let fallback = (u + step) % node_count;
                    if fallback != u && !nbrs.contains(&(fallback as u32)) {
                        nbrs.push(fallback as u32);
                    }
                }
            }
        }
    }

    // Helper: GreedySearch during build from `medoid_node_id` toward `query_vec`
    let greedy_search_build = |adj_graph: &[Vec<u32>],
                               query_vec: &[f32],
                               visited_list: &mut Vec<(u32, f32, f32)>,
                               visited_flags: &mut [u32],
                               visit_token: u32| {
        visited_list.clear();
        let mut beam: Vec<(u32, f32, f32)> = Vec::with_capacity(l_build + 1);
        let mut expanded: Vec<bool> = Vec::with_capacity(l_build + 1);

        let m_idx = medoid_node_id as usize;
        visited_flags[m_idx] = visit_token;
        let m_vec = &vectors[m_idx * dimension..(m_idx + 1) * dimension];
        let (m_score, m_dist) = compute_vector_metric(cfg.metric, query_vec, m_vec);
        beam.push((medoid_node_id, m_score, m_dist));
        expanded.push(false);
        visited_list.push((medoid_node_id, m_score, m_dist));

        loop {
            // Find closest unexpanded candidate in `beam`
            let mut next_pos = None;
            for idx in 0..beam.len() {
                if !expanded[idx] {
                    next_pos = Some(idx);
                    break;
                }
            }
            let Some(pos) = next_pos else { break };
            expanded[pos] = true;
            let u = beam[pos].0 as usize;

            for &nb in &adj_graph[u] {
                let nb_idx = nb as usize;
                if visited_flags[nb_idx] != visit_token {
                    visited_flags[nb_idx] = visit_token;
                    let nb_vec = &vectors[nb_idx * dimension..(nb_idx + 1) * dimension];
                    let (s, d) = compute_vector_metric(cfg.metric, query_vec, nb_vec);
                    let cand = (nb, s, d);
                    visited_list.push(cand);

                    // Insert into sorted `beam` of capacity `l_build`
                    let ins = beam.binary_search_by(|probe| compare_cand(probe, &cand)).unwrap_or_else(|e| e);
                    if ins < l_build {
                        beam.insert(ins, cand);
                        expanded.insert(ins, false);
                        if beam.len() > l_build {
                            beam.pop();
                            expanded.pop();
                        }
                    }
                }
            }
        }
    };

    // Helper: RobustPrune(u, candidates, alpha, r)
    let robust_prune = |u: u32,
                        candidates: &mut Vec<(u32, f32, f32)>,
                        alpha_cur: f32|
     -> Vec<u32> {
        candidates.retain(|c| c.0 != u);
        candidates.sort_by(compare_cand);
        candidates.dedup_by_key(|c| c.0);

        let mut new_nbrs: Vec<u32> = Vec::with_capacity(r);
        let mut active = vec![true; candidates.len()];

        for i in 0..candidates.len() {
            if !active[i] {
                continue;
            }
            let p_star = candidates[i].0;
            new_nbrs.push(p_star);
            if new_nbrs.len() >= r {
                break;
            }
            let p_star_vec =
                &vectors[(p_star as usize) * dimension..(p_star as usize + 1) * dimension];
            for j in (i + 1)..candidates.len() {
                if !active[j] {
                    continue;
                }
                let p_prime = candidates[j].0;
                let dist_u_pprime = candidates[j].2;
                let p_prime_vec =
                    &vectors[(p_prime as usize) * dimension..(p_prime as usize + 1) * dimension];
                let (_s, dist_pstar_pprime) =
                    compute_vector_metric(cfg.metric, p_star_vec, p_prime_vec);
                if alpha_cur * dist_pstar_pprime <= dist_u_pprime {
                    active[j] = false;
                }
            }
        }
        new_nbrs
    };

    // 3. Two-pass Vamana refinement (Pass 1: alpha = 1.0, Pass 2: alpha = cfg.alpha)
    if node_count > 1 {
        let mut visited_flags = vec![0u32; node_count];
        let mut visit_token = 1u32;
        let mut visited_list: Vec<(u32, f32, f32)> = Vec::with_capacity(l_build * 4);
        let mut prune_buf: Vec<(u32, f32, f32)> = Vec::with_capacity(l_build * 4 + r);

        for &alpha_pass in &[1.0f32, cfg.alpha] {
            for u in 0..node_count {
                let u_id = u as u32;
                let u_vec = &vectors[u * dimension..(u + 1) * dimension];
                visit_token = visit_token.wrapping_add(1);
                if visit_token == 0 {
                    visited_flags.fill(0);
                    visit_token = 1;
                }
                greedy_search_build(&adj, u_vec, &mut visited_list, &mut visited_flags, visit_token);

                // Merge existing out-neighbors of `u` into candidate pool
                prune_buf.clear();
                prune_buf.extend_from_slice(&visited_list);
                for &existing_nb in &adj[u] {
                    let nb_idx = existing_nb as usize;
                    let nb_vec = &vectors[nb_idx * dimension..(nb_idx + 1) * dimension];
                    let (s, d) = compute_vector_metric(cfg.metric, u_vec, nb_vec);
                    prune_buf.push((existing_nb, s, d));
                }

                let pruned = robust_prune(u_id, &mut prune_buf, alpha_pass);
                adj[u] = pruned.clone();

                // Add bidirectional back-edges and prune if degree exceeds R
                for &nb in &pruned {
                    let nb_usize = nb as usize;
                    if !adj[nb_usize].contains(&u_id) {
                        adj[nb_usize].push(u_id);
                        if adj[nb_usize].len() > r {
                            let nb_vec = &vectors[nb_usize * dimension..(nb_usize + 1) * dimension];
                            let mut nb_cands: Vec<(u32, f32, f32)> =
                                Vec::with_capacity(adj[nb_usize].len());
                            for &w in &adj[nb_usize] {
                                let w_usize = w as usize;
                                let w_vec =
                                    &vectors[w_usize * dimension..(w_usize + 1) * dimension];
                                let (s, d) = compute_vector_metric(cfg.metric, nb_vec, w_vec);
                                nb_cands.push((w, s, d));
                            }
                            adj[nb_usize] = robust_prune(nb, &mut nb_cands, alpha_pass);
                        }
                    }
                }
            }
        }
    }

    // 4. Serialize fixed-stride adjacency slab: N * (R + 1) * 4 bytes
    let row_words = r + 1;
    let adj_slab_bytes = node_count * row_words * 4;
    let mut adj_slab = vec![0u8; adj_slab_bytes];
    for (u, nbrs) in adj.iter().enumerate() {
        let deg = nbrs.len().min(r);
        let row_byte_off = u * row_words * 4;
        adj_slab[row_byte_off..row_byte_off + 4].copy_from_slice(&(deg as u32).to_le_bytes());
        for (j, &nb) in nbrs.iter().take(deg).enumerate() {
            let off = row_byte_off + (j + 1) * 4;
            adj_slab[off..off + 4].copy_from_slice(&nb.to_le_bytes());
        }
        // Pad unused slots with 0xFFFF_FFFF
        for j in deg..r {
            let off = row_byte_off + (j + 1) * 4;
            adj_slab[off..off + 4].copy_from_slice(&0xFFFF_FFFFu32.to_le_bytes());
        }
    }

    // 5. Encode optional quantized routing slab
    let quant_slab = encode_quant_slab(vectors, node_count, dimension, cfg.quant_type, cfg.metric);

    let header = VamanaHeader {
        magic: VAMANA_MAGIC,
        version: VAMANA_VERSION,
        metric: cfg.metric as u8,
        quant_type: cfg.quant_type as u8,
        domain_id,
        vec_attr_id,
        dimension: dimension as u32,
        node_count: node_count as u64,
        max_degree_r: cfg.max_degree_r,
        medoid_node_id,
        alpha: cfg.alpha,
        ef_construction: cfg.ef_construction,
        adj_slab_offset: 0, // Patched by SnapshotWriter::finalize
        adj_slab_bytes: adj_slab_bytes as u64,
        quant_code_offset: 0, // Patched by SnapshotWriter::finalize
    };

    Ok(BuiltVamanaIndex {
        header,
        adj_slab,
        quant_slab,
    })
}

/// Permutes an existing Vamana index in $O(N \cdot R + N \cdot D)$ time under a node ID bijection:
/// - `old_to_new[old_u] = new_u`
/// - `new_to_old[new_u] = old_u`
pub fn permute_vamana_index(
    old_hdr: &VamanaHeader,
    old_adj_slab: &[u8],
    old_quant_slab: Option<&[u8]>,
    old_to_new: &[u32],
    new_to_old: &[u32],
) -> Result<BuiltVamanaIndex, String> {
    let n = old_hdr.node_count as usize;
    let r = old_hdr.max_degree_r as usize;
    let row_stride = (r + 1) * 4;
    if old_to_new.len() != n || new_to_old.len() != n {
        return Err(format!(
            "Permutation slice length ({}) does not match Vamana node_count ({})",
            old_to_new.len(),
            n
        ));
    }
    if old_adj_slab.len() < n * row_stride {
        return Err("Old Vamana adjacency slab is truncated".to_string());
    }

    let mut new_adj_slab = vec![0u8; n * row_stride];
    for new_u in 0..n {
        let old_u = new_to_old[new_u] as usize;
        let old_row_off = old_u * row_stride;
        let new_row_off = new_u * row_stride;
        let deg = u32::from_le_bytes(
            old_adj_slab[old_row_off..old_row_off + 4]
                .try_into()
                .unwrap(),
        ) as usize;
        let deg_clamped = deg.min(r);
        new_adj_slab[new_row_off..new_row_off + 4]
            .copy_from_slice(&(deg_clamped as u32).to_le_bytes());

        for j in 0..deg_clamped {
            let nb_off = old_row_off + (j + 1) * 4;
            let old_nb =
                u32::from_le_bytes(old_adj_slab[nb_off..nb_off + 4].try_into().unwrap()) as usize;
            let new_nb = old_to_new[old_nb];
            let dst_off = new_row_off + (j + 1) * 4;
            new_adj_slab[dst_off..dst_off + 4].copy_from_slice(&new_nb.to_le_bytes());
        }
        for j in deg_clamped..r {
            let dst_off = new_row_off + (j + 1) * 4;
            new_adj_slab[dst_off..dst_off + 4].copy_from_slice(&0xFFFF_FFFFu32.to_le_bytes());
        }
    }

    let quant_type = VamanaQuantType::from_u8(old_hdr.quant_type).unwrap_or(VamanaQuantType::None);
    let q_row_bytes = quant_type.row_bytes(old_hdr.dimension as usize);
    let mut new_quant_slab = Vec::new();
    if q_row_bytes > 0 {
        if let Some(old_q) = old_quant_slab {
            if old_q.len() >= n * q_row_bytes {
                new_quant_slab = vec![0u8; n * q_row_bytes];
                for new_u in 0..n {
                    let old_u = new_to_old[new_u] as usize;
                    new_quant_slab[new_u * q_row_bytes..(new_u + 1) * q_row_bytes]
                        .copy_from_slice(&old_q[old_u * q_row_bytes..(old_u + 1) * q_row_bytes]);
                }
            }
        }
    }

    let new_medoid = if n > 0 {
        old_to_new[old_hdr.medoid_node_id as usize]
    } else {
        0
    };

    let mut new_hdr = *old_hdr;
    new_hdr.medoid_node_id = new_medoid;
    new_hdr.adj_slab_offset = 0;
    new_hdr.adj_slab_bytes = (n * row_stride) as u64;
    new_hdr.quant_code_offset = 0;

    Ok(BuiltVamanaIndex {
        header: new_hdr,
        adj_slab: new_adj_slab,
        quant_slab: new_quant_slab,
    })
}

/// Computes a BFS node ordering starting from `medoid_node_id` over the Vamana routing graph
/// so nodes visited consecutively during greedy beam search are co-located in memory.
/// Returns `(old_to_new, new_to_old)`.
pub fn compute_vamana_bfs_order(hdr: &VamanaHeader, adj_slab: &[u8]) -> (Vec<u32>, Vec<u32>) {
    let n = hdr.node_count as usize;
    let r = hdr.max_degree_r as usize;
    let row_stride = (r + 1) * 4;
    let mut visited = vec![false; n];
    let mut new_to_old = Vec::with_capacity(n);
    let mut queue = std::collections::VecDeque::new();

    if n > 0 && (hdr.medoid_node_id as usize) < n {
        let m = hdr.medoid_node_id as usize;
        visited[m] = true;
        queue.push_back(m as u32);
    }

    for start in 0..n {
        if !visited[start] && queue.is_empty() {
            visited[start] = true;
            queue.push_back(start as u32);
        }
        while let Some(u) = queue.pop_front() {
            new_to_old.push(u);
            let row_off = (u as usize) * row_stride;
            if row_off + 4 <= adj_slab.len() {
                let deg = u32::from_le_bytes(adj_slab[row_off..row_off + 4].try_into().unwrap())
                    as usize;
                let deg = deg.min(r);
                for j in 0..deg {
                    let nb_off = row_off + (j + 1) * 4;
                    if nb_off + 4 <= adj_slab.len() {
                        let nb =
                            u32::from_le_bytes(adj_slab[nb_off..nb_off + 4].try_into().unwrap())
                                as usize;
                        if nb < n && !visited[nb] {
                            visited[nb] = true;
                            queue.push_back(nb as u32);
                        }
                    }
                }
            }
        }
    }

    let mut old_to_new = vec![0u32; n];
    for (new_id, &old_id) in new_to_old.iter().enumerate() {
        old_to_new[old_id as usize] = new_id as u32;
    }
    (old_to_new, new_to_old)
}

/// Evaluates quantized routing score & distance for node `u` from `quant_slab`.
#[inline]
pub fn eval_quantized_routing_node(
    hdr: &VamanaHeader,
    quant_slab: &[u8],
    query_vec: &[f32],
    u: usize,
    approx_buf: &mut [f32],
) -> Result<(u32, f32, f32), String> {
    let dim = (hdr.dimension as usize).min(query_vec.len());
    let quant_type = VamanaQuantType::from_u8(hdr.quant_type)
        .ok_or_else(|| format!("Invalid Vamana quant_type {}", hdr.quant_type))?;
    let metric = VamanaMetric::from_u8(hdr.metric)
        .ok_or_else(|| format!("Invalid Vamana metric {}", hdr.metric))?;
    let row_bytes = quant_type.row_bytes(hdr.dimension as usize);
    let row_off = u * row_bytes;
    if row_off + row_bytes > quant_slab.len() {
        return Err("Vamana quantized routing read out of bounds".to_string());
    }

    match quant_type {
        VamanaQuantType::None => Err("eval_quantized_routing_node called with QuantNone".to_string()),
        VamanaQuantType::Bq1 => {
            imp_assert::testcase(401, true);
            let mut ham = 0u32;
            for (d, &qv) in query_vec.iter().enumerate().take(dim) {
                let byte_val = quant_slab[row_off + (d >> 3)];
                let bv = ((byte_val >> (d & 7)) & 1) != 0;
                let bq = qv >= 0.0;
                if bq != bv {
                    ham += 1;
                }
            }
            let dist = ham as f32;
            Ok((u as u32, -dist, dist))
        }
        VamanaQuantType::Sq8 => {
            imp_assert::testcase(402, true);
            for (d, out) in approx_buf.iter_mut().enumerate().take(dim) {
                let code = quant_slab[row_off + d];
                *out = (code as f32 - 128.0) / 127.0;
            }
            let (score, dist) = compute_vector_metric(metric, &query_vec[..dim], &approx_buf[..dim]);
            Ok((u as u32, score, dist))
        }
        VamanaQuantType::Fp16 => {
            imp_assert::testcase(403, true);
            for (d, out) in approx_buf.iter_mut().enumerate().take(dim) {
                let h16 = u16::from_le_bytes([
                    quant_slab[row_off + d * 2],
                    quant_slab[row_off + d * 2 + 1],
                ]);
                *out = f32_from_fp16(h16);
            }
            let (score, dist) = compute_vector_metric(metric, &query_vec[..dim], &approx_buf[..dim]);
            Ok((u as u32, score, dist))
        }
    }
}

/// Executes Vamana Greedy Beam Search over `adj_slab`, optional `quant_slab`, and full-precision `vectors`.
/// Supports Two-Stage Quantized Routing (`BQ1` / `SQ8` / `FP16` during beam expansion followed by exact
/// `Float32` reranking of the `ef_search` beam candidates), matching OCaml `Snapshot.vamana_ann_search`.
pub fn vamana_ann_search(
    hdr: &VamanaHeader,
    adj_slab: &[u8],
    quant_slab: Option<&[u8]>,
    vectors: &[f32],
    query_vec: &[f32],
    top_k: usize,
    ef_search: usize,
) -> Result<Vec<(u32, f32, f32)>, String> {
    let n_nodes = hdr.node_count as usize;
    let dim = hdr.dimension as usize;
    if n_nodes == 0 || top_k == 0 {
        return Ok(Vec::new());
    }
    let metric = VamanaMetric::from_u8(hdr.metric)
        .ok_or_else(|| format!("Unsupported Vamana metric {}", hdr.metric))?;
    let quant_type = VamanaQuantType::from_u8(hdr.quant_type)
        .ok_or_else(|| format!("Unsupported Vamana quant_type {}", hdr.quant_type))?;

    let ef = top_k.max(ef_search).max(1);
    let r = hdr.max_degree_r as usize;
    let row_stride = (r + 1) * 4;

    let use_quant_routing = quant_type != VamanaQuantType::None
        && quant_slab.map(|s| !s.is_empty()).unwrap_or(false);

    let mut approx_buf = vec![0.0f32; dim];

    let eval_exact_node = |u: usize| -> Result<(u32, f32, f32), String> {
        let start = u * dim;
        if start + dim > vectors.len() {
            return Err("Vector attribute read out of bounds".to_string());
        }
        let vec = &vectors[start..start + dim];
        let (score, dist) = compute_vector_metric(metric, query_vec, vec);
        Ok((u as u32, score, dist))
    };

    let mut visited = vec![false; n_nodes];
    let mut expanded = vec![false; n_nodes];
    let mut candidates: Vec<(u32, f32, f32)> = Vec::with_capacity(ef + 1);

    let medoid = hdr.medoid_node_id as usize;
    if medoid >= n_nodes {
        return Err(format!(
            "Vamana medoid_node_id {} out of bounds (node_count={})",
            medoid, n_nodes
        ));
    }

    let medoid_entry = if use_quant_routing {
        eval_quantized_routing_node(hdr, quant_slab.unwrap(), query_vec, medoid, &mut approx_buf)?
    } else {
        eval_exact_node(medoid)?
    };

    visited[medoid] = true;
    candidates.push(medoid_entry);

    loop {
        let next_unexpanded = candidates
            .iter()
            .find(|&&(u, _, _)| !expanded[u as usize])
            .copied();
        let Some((u, _, _)) = next_unexpanded else {
            break;
        };
        let u_usize = u as usize;
        expanded[u_usize] = true;

        let row_off = u_usize * row_stride;
        if row_off + 4 > adj_slab.len() {
            return Err("Vamana adjacency slab read out of bounds".to_string());
        }
        let deg = u32::from_le_bytes(adj_slab[row_off..row_off + 4].try_into().unwrap()) as usize;
        let deg_clamped = deg.min(r);

        for j in 0..deg_clamped {
            let nb_off = row_off + (j + 1) * 4;
            if nb_off + 4 > adj_slab.len() {
                return Err("Vamana adjacency neighbor read out of bounds".to_string());
            }
            let nb = u32::from_le_bytes(adj_slab[nb_off..nb_off + 4].try_into().unwrap()) as usize;
            if nb < n_nodes && !visited[nb] {
                visited[nb] = true;
                let entry = if use_quant_routing {
                    eval_quantized_routing_node(
                        hdr,
                        quant_slab.unwrap(),
                        query_vec,
                        nb,
                        &mut approx_buf,
                    )?
                } else {
                    eval_exact_node(nb)?
                };
                candidates.push(entry);
                candidates.sort_by(compare_cand);
                if candidates.len() > ef {
                    candidates.truncate(ef);
                }
            }
        }
    }

    if !use_quant_routing {
        candidates.sort_by(compare_cand);
        candidates.truncate(top_k);
        Ok(candidates)
    } else {
        // Stage 2: Exact Full-Precision Float32 Reranking of the `ef` beam candidates
        let mut reranked: Vec<(u32, f32, f32)> = Vec::with_capacity(candidates.len());
        for &(u, _, _) in &candidates {
            reranked.push(eval_exact_node(u as usize)?);
        }
        reranked.sort_by(compare_cand);
        reranked.truncate(top_k);
        Ok(reranked)
    }
}

/// Exact brute-force nearest-neighbor oracle for `Recall@K` and `nDCG@K` verification.
pub fn brute_force_ann_search(
    metric: VamanaMetric,
    vectors: &[f32],
    node_count: usize,
    dimension: usize,
    query_vec: &[f32],
    top_k: usize,
) -> Vec<(u32, f32, f32)> {
    let mut all: Vec<(u32, f32, f32)> = Vec::with_capacity(node_count);
    for u in 0..node_count {
        let v = &vectors[u * dimension..(u + 1) * dimension];
        let (score, dist) = compute_vector_metric(metric, query_vec, v);
        all.push((u as u32, score, dist));
    }
    all.sort_by(compare_cand);
    all.truncate(top_k);
    all
}

/// Computes `(recall_at_1, recall_at_k, ndcg_at_k, mrr)` comparing `approx` results against `exact` oracle.
pub fn evaluate_ann_quality(
    approx: &[(u32, f32, f32)],
    exact: &[(u32, f32, f32)],
    k: usize,
) -> (f64, f64, f64, f64) {
    let k_eff = k.min(exact.len()).max(1);
    let exact_top_k: Vec<u32> = exact.iter().take(k_eff).map(|x| x.0).collect();
    let approx_top_k: Vec<u32> = approx.iter().take(k_eff).map(|x| x.0).collect();

    let recall_at_1 = if !approx_top_k.is_empty() && !exact_top_k.is_empty() && approx_top_k[0] == exact_top_k[0] {
        1.0
    } else {
        0.0
    };

    let mut hits = 0usize;
    let mut dcg = 0.0f64;
    let mut idcg = 0.0f64;
    let mut mrr = 0.0f64;

    for i in 0..k_eff {
        let rel_ideal = (k_eff - i) as f64;
        idcg += rel_ideal / ((i + 2) as f64).log2();
    }

    for (rank, &u) in approx_top_k.iter().enumerate() {
        if let Some(exact_pos) = exact_top_k.iter().position(|&e| e == u) {
            hits += 1;
            let rel = (k_eff - exact_pos) as f64;
            dcg += rel / ((rank + 2) as f64).log2();
            if exact_pos == 0 && mrr == 0.0 {
                mrr = 1.0 / ((rank + 1) as f64);
            }
        }
    }

    let recall_at_k = (hits as f64) / (k_eff as f64);
    let ndcg_at_k = if idcg > 0.0 { dcg / idcg } else { 1.0 };
    (recall_at_1, recall_at_k, ndcg_at_k, mrr)
}
