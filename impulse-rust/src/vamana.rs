//! Vamana (DiskANN) Single-Layer Graph Index Builder, Permuter & Two-Stage Quantized Search
//!
//! Implements Section 2.6 `IMP_INDEX_VAMANA_ANN = 8` (`0x08`) in pure zero-dependency Rust,
//! maintaining 100% binary layout and numerical parity with OCaml `snapshot.ml` and C++ `ImpulseVM`.

use crate::assert as imp_assert;
pub use crate::spec::{VamanaHeader, VamanaMetric, VamanaQuantType, VAMANA_MAGIC, VAMANA_VERSION};
use std::cell::RefCell;
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
/// Fast branchless path handles normal and zero numbers; cold path handles subnormals and Inf/NaN.
#[inline(always)]
pub fn f32_from_fp16(h: u16) -> f32 {
    let exp_bits = h & 0x7C00;
    let sign = ((h >> 15) & 1) as u32;
    if exp_bits != 0 && exp_bits != 0x7C00 {
        let bits = (sign << 31) | ((((h & 0x7FFF) as u32) + 0x1C000) << 13);
        return f32::from_bits(bits);
    }
    if (h & 0x7FFF) == 0 {
        return f32::from_bits(sign << 31);
    }
    let exp = ((h >> 10) & 0x1F) as i32;
    let frac = (h & 0x03FF) as u32;
    let bits: u32 = if exp == 0 {
        let mut m = frac;
        let mut e = -14i32;
        while (m & 0x0400) == 0 {
            m <<= 1;
            e -= 1;
        }
        m &= 0x03FF;
        let exp32 = (e + 127) as u32;
        (sign << 31) | (exp32 << 23) | (m << 13)
    } else {
        let frac32 = if frac == 0 { 0 } else { 0x0040_0000 | (frac << 13) };
        (sign << 31) | (0xFF << 23) | frac32
    };
    f32::from_bits(bits)
}

/// Fast branchless FP16 -> FP32 conversion for vector inner loops (auto-vectorizes with NEON/AVX2).
#[inline(always)]
fn f32_from_fp16_fast(h: u16) -> f32 {
    let sign = ((h as u32) & 0x8000) << 16;
    let abs = (h & 0x7FFF) as u32;
    let body = if abs == 0 {
        0u32
    } else {
        (abs + 0x1C000) << 13
    };
    f32::from_bits(sign | body)
}

/// Encodes an `f32` into an IEEE-754 binary16 (FP16) 16-bit unsigned integer.
#[inline(always)]
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

/// 8-way unrolled dot product for auto-vectorization (NEON / AVX2).
#[inline(always)]
fn fast_dot_f32(a: &[f32], b: &[f32]) -> f32 {
    let dim = a.len().min(b.len());
    let a = &a[..dim];
    let b = &b[..dim];
    let mut s0 = 0.0f32;
    let mut s1 = 0.0f32;
    let mut s2 = 0.0f32;
    let mut s3 = 0.0f32;
    let mut i = 0;
    let n8 = dim & !7;
    while i < n8 {
        s0 += a[i] * b[i] + a[i + 4] * b[i + 4];
        s1 += a[i + 1] * b[i + 1] + a[i + 5] * b[i + 5];
        s2 += a[i + 2] * b[i + 2] + a[i + 6] * b[i + 6];
        s3 += a[i + 3] * b[i + 3] + a[i + 7] * b[i + 7];
        i += 8;
    }
    let mut tail = 0.0f32;
    while i < dim {
        tail += a[i] * b[i];
        i += 1;
    }
    (s0 + s1) + (s2 + s3) + tail
}

/// 8-way unrolled squared Euclidean distance for auto-vectorization (NEON / AVX2).
#[inline(always)]
fn fast_l2_sq_f32(a: &[f32], b: &[f32]) -> f32 {
    let dim = a.len().min(b.len());
    let a = &a[..dim];
    let b = &b[..dim];
    let mut s0 = 0.0f32;
    let mut s1 = 0.0f32;
    let mut s2 = 0.0f32;
    let mut s3 = 0.0f32;
    let mut i = 0;
    let n8 = dim & !7;
    while i < n8 {
        let d0 = a[i] - b[i];
        let d1 = a[i + 1] - b[i + 1];
        let d2 = a[i + 2] - b[i + 2];
        let d3 = a[i + 3] - b[i + 3];
        let d4 = a[i + 4] - b[i + 4];
        let d5 = a[i + 5] - b[i + 5];
        let d6 = a[i + 6] - b[i + 6];
        let d7 = a[i + 7] - b[i + 7];
        s0 += d0 * d0 + d4 * d4;
        s1 += d1 * d1 + d5 * d5;
        s2 += d2 * d2 + d6 * d6;
        s3 += d3 * d3 + d7 * d7;
        i += 8;
    }
    let mut tail = 0.0f32;
    while i < dim {
        let d = a[i] - b[i];
        tail += d * d;
        i += 1;
    }
    (s0 + s1) + (s2 + s3) + tail
}

/// Computes `(score, distance)` between `q` and `v` where higher `score` is more similar
/// and lower `distance` is closer (matching OCaml `Snapshot.compute_vector_metric`).
#[inline]
pub fn compute_vector_metric(metric: VamanaMetric, q: &[f32], v: &[f32]) -> (f32, f32) {
    let dim = q.len().min(v.len());
    match metric {
        VamanaMetric::L2 => {
            let sum_sq = fast_l2_sq_f32(&q[..dim], &v[..dim]);
            (-sum_sq, sum_sq)
        }
        VamanaMetric::Dot => {
            let dot = fast_dot_f32(&q[..dim], &v[..dim]);
            (dot, -dot)
        }
        VamanaMetric::Cosine => {
            let mut d0 = 0.0f32;
            let mut d1 = 0.0f32;
            let mut nq0 = 0.0f32;
            let mut nq1 = 0.0f32;
            let mut nv0 = 0.0f32;
            let mut nv1 = 0.0f32;
            let mut i = 0;
            let n4 = dim & !3;
            while i < n4 {
                let q0 = q[i];
                let q1 = q[i + 1];
                let q2 = q[i + 2];
                let q3 = q[i + 3];
                let v0 = v[i];
                let v1 = v[i + 1];
                let v2 = v[i + 2];
                let v3 = v[i + 3];
                d0 += q0 * v0 + q2 * v2;
                d1 += q1 * v1 + q3 * v3;
                nq0 += q0 * q0 + q2 * q2;
                nq1 += q1 * q1 + q3 * q3;
                nv0 += v0 * v0 + v2 * v2;
                nv1 += v1 * v1 + v3 * v3;
                i += 4;
            }
            let mut dot = d0 + d1;
            let mut nq = nq0 + nq1;
            let mut nv = nv0 + nv1;
            while i < dim {
                dot += q[i] * v[i];
                nq += q[i] * q[i];
                nv += v[i] * v[i];
                i += 1;
            }
            let denom = (nq * nv).sqrt();
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

#[inline(always)]
fn compare_cand(a: &(u32, f32, f32), b: &(u32, f32, f32)) -> Ordering {
    a.2.total_cmp(&b.2).then_with(|| a.0.cmp(&b.0))
}

/// Packs the sign bits (`v >= 0.0`) of `vec[0..dim]` into 64-bit little-endian words.
#[inline]
pub fn pack_bq1_query_words(vec: &[f32], dim: usize, out_words: &mut Vec<u64>) {
    let num_words = (dim + 63) >> 6;
    out_words.clear();
    out_words.resize(num_words, 0u64);
    for (d, &val) in vec.iter().enumerate().take(dim) {
        if val >= 0.0 {
            out_words[d >> 6] |= 1u64 << (d & 63);
        }
    }
}

/// Evaluates `BQ1` Hamming distance using 64-bit word XOR + hardware `POPCNT` (`u64::count_ones`).
#[inline(always)]
pub fn eval_bq1_words_row(quant_row: &[u8], query_words: &[u64], dim: usize, u: u32) -> (u32, f32, f32) {
    let full_words = dim >> 6;
    let mut ham = 0u32;
    for w in 0..full_words {
        let off = w * 8;
        let v_word = u64::from_le_bytes([
            quant_row[off],
            quant_row[off + 1],
            quant_row[off + 2],
            quant_row[off + 3],
            quant_row[off + 4],
            quant_row[off + 5],
            quant_row[off + 6],
            quant_row[off + 7],
        ]);
        ham += (query_words[w] ^ v_word).count_ones();
    }
    let rem_bits = dim & 63;
    if rem_bits != 0 {
        let off = full_words * 8;
        let rem_bytes = (rem_bits + 7) >> 3;
        let mut v_word = 0u64;
        for b in 0..rem_bytes {
            v_word |= (quant_row[off + b] as u64) << (b * 8);
        }
        let mask = (1u64 << rem_bits) - 1;
        ham += ((query_words[full_words] ^ v_word) & mask).count_ones();
    }
    let dist = ham as f32;
    (u, -dist, dist)
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

/// Fast pairwise metric evaluator used during `build_vamana_index` where Cosine vectors are pre-normalized
/// to unit length and Hamming vectors are pre-packed into `u64` words.
#[inline(always)]
fn eval_build_pair(
    metric: VamanaMetric,
    work_vectors: &[f32],
    hamming_words: &[u64],
    words_per_vec: usize,
    dimension: usize,
    u: usize,
    v: usize,
) -> (f32, f32) {
    match metric {
        VamanaMetric::Cosine => {
            let u_slice = &work_vectors[u * dimension..(u + 1) * dimension];
            let v_slice = &work_vectors[v * dimension..(v + 1) * dimension];
            let cos_sim = fast_dot_f32(u_slice, v_slice);
            (cos_sim, 1.0 - cos_sim)
        }
        VamanaMetric::L2 => {
            let u_slice = &work_vectors[u * dimension..(u + 1) * dimension];
            let v_slice = &work_vectors[v * dimension..(v + 1) * dimension];
            let sum_sq = fast_l2_sq_f32(u_slice, v_slice);
            (-sum_sq, sum_sq)
        }
        VamanaMetric::Dot => {
            let u_slice = &work_vectors[u * dimension..(u + 1) * dimension];
            let v_slice = &work_vectors[v * dimension..(v + 1) * dimension];
            let dot = fast_dot_f32(u_slice, v_slice);
            (dot, -dot)
        }
        VamanaMetric::Hamming => {
            let u_words = &hamming_words[u * words_per_vec..(u + 1) * words_per_vec];
            let v_words = &hamming_words[v * words_per_vec..(v + 1) * words_per_vec];
            let mut ham = 0u32;
            for w in 0..words_per_vec {
                ham += (u_words[w] ^ v_words[w]).count_ones();
            }
            let d = ham as f32;
            (-d, d)
        }
    }
}

/// Reusable thread-local scratchpad for Vamana index construction.
struct BuildWorkerScratch {
    visited_flags: Vec<u32>,
    visit_token: u32,
    beam: Vec<(u32, f32, f32, bool)>,
    visited_list: Vec<(u32, f32, f32)>,
    prune_buf: Vec<(u32, f32, f32)>,
    active_buf: Vec<bool>,
}

impl BuildWorkerScratch {
    fn new(node_count: usize, l_build: usize, r: usize) -> Self {
        Self {
            visited_flags: vec![0u32; node_count],
            visit_token: 1,
            beam: Vec::with_capacity(l_build + 1),
            visited_list: Vec::with_capacity(l_build * 2 + 2),
            prune_buf: Vec::with_capacity(l_build * 2 + r * 2 + 4),
            active_buf: Vec::with_capacity(l_build * 2 + r * 2 + 4),
        }
    }

    #[inline]
    fn next_token(&mut self) -> u32 {
        self.visit_token = self.visit_token.wrapping_add(1);
        if self.visit_token == 0 {
            self.visited_flags.fill(0);
            self.visit_token = 1;
        }
        self.visit_token
    }

    #[inline]
    fn greedy_search_build(
        &mut self,
        adj_graph: &[Vec<u32>],
        medoid_node_id: u32,
        query_idx: usize,
        r: usize,
        l_build: usize,
        max_prune_cands: usize,
        metric: VamanaMetric,
        work_vectors: &[f32],
        hamming_words: &[u64],
        words_per_vec: usize,
        dimension: usize,
    ) {
        let token = self.next_token();
        self.visited_list.clear();
        self.beam.clear();

        let m_idx = medoid_node_id as usize;
        self.visited_flags[m_idx] = token;
        let (m_score, m_dist) = eval_build_pair(
            metric,
            work_vectors,
            hamming_words,
            words_per_vec,
            dimension,
            query_idx,
            m_idx,
        );
        self.beam.push((medoid_node_id, m_score, m_dist, false));
        self.visited_list.push((medoid_node_id, m_score, m_dist));

        let mut first_unexpanded = 0usize;
        loop {
            let mut next_pos = None;
            for idx in first_unexpanded..self.beam.len() {
                if !self.beam[idx].3 {
                    next_pos = Some(idx);
                    break;
                }
            }
            let Some(pos) = next_pos else { break };
            self.beam[pos].3 = true;
            first_unexpanded = pos + 1;

            let (u_id, _u_score, _u_dist, _) = self.beam[pos];
            let u = u_id as usize;

            for &nb in adj_graph[u].iter().take(r) {
                let nb_idx = nb as usize;
                if self.visited_flags[nb_idx] != token {
                    self.visited_flags[nb_idx] = token;
                    let (s, d) = eval_build_pair(
                        metric,
                        work_vectors,
                        hamming_words,
                        words_per_vec,
                        dimension,
                        query_idx,
                        nb_idx,
                    );
                    let cand_key = (nb, s, d);
                    if max_prune_cands > l_build {
                        self.visited_list.push(cand_key);
                    }

                    if self.beam.len() == l_build {
                        let last = &self.beam[l_build - 1];
                        if compare_cand(&cand_key, &(last.0, last.1, last.2)) != Ordering::Less {
                            continue;
                        }
                    }

                    if max_prune_cands <= l_build {
                        self.visited_list.push(cand_key);
                    }
                    let ins = self
                        .beam
                        .binary_search_by(|probe| {
                            compare_cand(&(probe.0, probe.1, probe.2), &cand_key)
                        })
                        .unwrap_or_else(|e| e);
                    if ins < l_build {
                        self.beam.insert(ins, (nb, s, d, false));
                        if ins < first_unexpanded {
                            first_unexpanded = ins;
                        }
                        if self.beam.len() > l_build {
                            self.beam.pop();
                        }
                    }
                }
            }
        }

        if self.visited_list.len() > max_prune_cands {
            self.visited_list
                .select_nth_unstable_by(max_prune_cands - 1, compare_cand);
            self.visited_list.truncate(max_prune_cands);
        }
    }

    #[inline]
    fn robust_prune(
        &mut self,
        u: u32,
        alpha_cur: f32,
        r: usize,
        metric: VamanaMetric,
        work_vectors: &[f32],
        hamming_words: &[u64],
        words_per_vec: usize,
        dimension: usize,
        out_nbrs: &mut Vec<(u32, f32, f32)>,
    ) {
        self.prune_buf.retain(|c| c.0 != u);
        self.prune_buf.sort_unstable_by(compare_cand);
        self.prune_buf.dedup_by_key(|c| c.0);

        out_nbrs.clear();
        let n_cands = self.prune_buf.len();
        self.active_buf.clear();
        self.active_buf.resize(n_cands, true);

        for i in 0..n_cands {
            if !self.active_buf[i] {
                continue;
            }
            let cand_i = self.prune_buf[i];
            let p_star = cand_i.0;
            out_nbrs.push(cand_i);
            if out_nbrs.len() >= r {
                break;
            }
            let p_star_idx = p_star as usize;
            for j in (i + 1)..n_cands {
                if !self.active_buf[j] {
                    continue;
                }
                let p_prime_idx = self.prune_buf[j].0 as usize;
                let dist_u_pprime = self.prune_buf[j].2;
                let (_s, dist_pstar_pprime) = eval_build_pair(
                    metric,
                    work_vectors,
                    hamming_words,
                    words_per_vec,
                    dimension,
                    p_star_idx,
                    p_prime_idx,
                );
                if alpha_cur * dist_pstar_pprime <= dist_u_pprime {
                    self.active_buf[j] = false;
                }
            }
        }
    }
}

/// Builds a Vamana (`IMP_INDEX_VAMANA_ANN = 8`) index over `vectors` (`[node_count, dimension]`).
/// Automatically uses multi-threaded batched construction via `std::thread::scope` for `node_count >= 1024`.
pub fn build_vamana_index(
    domain_id: u16,
    vec_attr_id: u16,
    vectors: &[f32],
    node_count: usize,
    dimension: usize,
    cfg: &VamanaBuildConfig,
) -> Result<BuiltVamanaIndex, String> {
    build_vamana_index_with_threads(domain_id, vec_attr_id, vectors, node_count, dimension, cfg, 0)
}

/// Builds a Vamana (`IMP_INDEX_VAMANA_ANN = 8`) index using `num_threads` worker threads
/// (`0` = auto-detect available CPU parallelism for `node_count >= 1024`, `1` = single-threaded).
pub fn build_vamana_index_with_threads(
    domain_id: u16,
    vec_attr_id: u16,
    vectors: &[f32],
    node_count: usize,
    dimension: usize,
    cfg: &VamanaBuildConfig,
    num_threads: usize,
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

    // 1. Pre-normalize working vectors for Cosine (eliminating sqrt & division in inner loops)
    //    or pre-pack sign bits for Hamming.
    let mut norm_storage: Vec<f32> = Vec::new();
    let work_vectors: &[f32] = if cfg.metric == VamanaMetric::Cosine {
        norm_storage.resize(node_count * dimension, 0.0f32);
        for u in 0..node_count {
            let src = &vectors[u * dimension..(u + 1) * dimension];
            let dst = &mut norm_storage[u * dimension..(u + 1) * dimension];
            let norm_sq = fast_dot_f32(src, src);
            if norm_sq > 1e-12 {
                let inv_norm = 1.0 / norm_sq.sqrt();
                for d in 0..dimension {
                    dst[d] = src[d] * inv_norm;
                }
            }
        }
        &norm_storage
    } else {
        vectors
    };

    let words_per_vec = (dimension + 63) >> 6;
    let mut hamming_words: Vec<u64> = Vec::new();
    if cfg.metric == VamanaMetric::Hamming {
        hamming_words.resize(node_count * words_per_vec, 0u64);
        for u in 0..node_count {
            let src = &vectors[u * dimension..(u + 1) * dimension];
            let dst = &mut hamming_words[u * words_per_vec..(u + 1) * words_per_vec];
            for (d, &val) in src.iter().enumerate() {
                if val >= 0.0 {
                    dst[d >> 6] |= 1u64 << (d & 63);
                }
            }
        }
    }

    // 2. Compute dataset centroid & exact medoid node m*
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

    // 3. Initialize deterministic R-regular out-neighborhood graph
    let mut adj: Vec<Vec<u32>> = vec![Vec::with_capacity(r + 1); node_count];
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

    // 4. Two-pass Vamana refinement (Pass 1: alpha = 1.0, Pass 2: alpha = cfg.alpha)
    if node_count > 1 {
        let effective_threads = if num_threads > 0 {
            num_threads
        } else {
            1
        };

        let mut scratch = BuildWorkerScratch::new(node_count, l_build, r);
        let mut pruned: Vec<(u32, f32, f32)> = Vec::with_capacity(r);
        let mut nb_pruned: Vec<(u32, f32, f32)> = Vec::with_capacity(r);
        // Cached sorted (nb_id, score, dist) for nodes already alpha-pruned in the current pass
        let mut adj_scored: Vec<Vec<(u32, f32, f32)>> = vec![Vec::with_capacity(r + 1); node_count];
        let mut is_alpha_pruned = vec![false; node_count];
        let max_prune_cands = l_build * 2;

        for &alpha_pass in &[1.0f32, cfg.alpha] {
            is_alpha_pruned.fill(false);
            if effective_threads <= 1 || node_count < 512 {
                for u in 0..node_count {
                    let u_id = u as u32;
                    scratch.greedy_search_build(
                        &adj,
                        medoid_node_id,
                        u,
                        r,
                        l_build,
                        max_prune_cands,
                        cfg.metric,
                        work_vectors,
                        &hamming_words,
                        words_per_vec,
                        dimension,
                    );

                    scratch.prune_buf.clear();
                    scratch.prune_buf.extend_from_slice(&scratch.visited_list);
                    if !adj_scored[u].is_empty() {
                        scratch.prune_buf.extend_from_slice(&adj_scored[u]);
                    } else {
                        for &existing_nb in &adj[u] {
                            let nb_idx = existing_nb as usize;
                            let (s, d) = eval_build_pair(
                                cfg.metric,
                                work_vectors,
                                &hamming_words,
                                words_per_vec,
                                dimension,
                                u,
                                nb_idx,
                            );
                            scratch.prune_buf.push((existing_nb, s, d));
                        }
                    }

                    scratch.robust_prune(
                        u_id,
                        alpha_pass,
                        r,
                        cfg.metric,
                        work_vectors,
                        &hamming_words,
                        words_per_vec,
                        dimension,
                        &mut pruned,
                    );
                    adj[u].clear();
                    adj_scored[u].clear();
                    for &entry in &pruned {
                        adj[u].push(entry.0);
                        adj_scored[u].push(entry);
                    }
                    is_alpha_pruned[u] = pruned.len() == r;

                    for i in 0..pruned.len() {
                        let (nb, s_u_nb, d_u_nb) = pruned[i];
                        let nb_usize = nb as usize;
                        if !adj[nb_usize].contains(&u_id) {
                            let cand = (u_id, s_u_nb, d_u_nb);
                            if adj[nb_usize].len() < r {
                                adj[nb_usize].push(u_id);
                                if !adj_scored[nb_usize].is_empty() {
                                    adj_scored[nb_usize].push(cand);
                                }
                                is_alpha_pruned[nb_usize] = false;
                            } else if is_alpha_pruned[nb_usize] {
                                // adj[nb_usize].len() == r and all r elements are mutually alpha-pruned
                                if compare_cand(&cand, &adj_scored[nb_usize][r - 1])
                                    != Ordering::Less
                                {
                                    continue;
                                }
                                let ins = adj_scored[nb_usize]
                                    .binary_search_by(|probe| compare_cand(probe, &cand))
                                    .unwrap_or_else(|e| e);
                                let mut shadowed = false;
                                for k in 0..ins {
                                    let p_star_idx = adj_scored[nb_usize][k].0 as usize;
                                    let (_s, dist_pstar_u) = eval_build_pair(
                                        cfg.metric,
                                        work_vectors,
                                        &hamming_words,
                                        words_per_vec,
                                        dimension,
                                        p_star_idx,
                                        u,
                                    );
                                    if alpha_pass * dist_pstar_u <= d_u_nb {
                                        shadowed = true;
                                        break;
                                    }
                                }
                                if !shadowed {
                                    adj_scored[nb_usize].insert(ins, cand);
                                    let mut idx = ins + 1;
                                    while idx < adj_scored[nb_usize].len() {
                                        let p_prime = adj_scored[nb_usize][idx];
                                        let (_s, dist_u_pprime) = eval_build_pair(
                                            cfg.metric,
                                            work_vectors,
                                            &hamming_words,
                                            words_per_vec,
                                            dimension,
                                            u,
                                            p_prime.0 as usize,
                                        );
                                        if alpha_pass * dist_u_pprime <= p_prime.2 {
                                            adj_scored[nb_usize].remove(idx);
                                        } else {
                                            idx += 1;
                                        }
                                    }
                                    if adj_scored[nb_usize].len() > r {
                                        adj_scored[nb_usize].truncate(r);
                                    }
                                    adj[nb_usize].clear();
                                    for &e in &adj_scored[nb_usize] {
                                        adj[nb_usize].push(e.0);
                                    }
                                    is_alpha_pruned[nb_usize] = adj_scored[nb_usize].len() == r;
                                }
                            } else {
                                scratch.prune_buf.clear();
                                if adj_scored[nb_usize].len() == adj[nb_usize].len() {
                                    scratch.prune_buf.extend_from_slice(&adj_scored[nb_usize]);
                                } else {
                                    for &w in &adj[nb_usize] {
                                        let w_usize = w as usize;
                                        let (s, d) = eval_build_pair(
                                            cfg.metric,
                                            work_vectors,
                                            &hamming_words,
                                            words_per_vec,
                                            dimension,
                                            nb_usize,
                                            w_usize,
                                        );
                                        scratch.prune_buf.push((w, s, d));
                                    }
                                }
                                scratch.prune_buf.push(cand);
                                scratch.robust_prune(
                                    nb,
                                    alpha_pass,
                                    r,
                                    cfg.metric,
                                    work_vectors,
                                    &hamming_words,
                                    words_per_vec,
                                    dimension,
                                    &mut nb_pruned,
                                );
                                adj[nb_usize].clear();
                                adj_scored[nb_usize].clear();
                                for &entry in &nb_pruned {
                                    adj[nb_usize].push(entry.0);
                                    adj_scored[nb_usize].push(entry);
                                }
                                is_alpha_pruned[nb_usize] = nb_pruned.len() == r;
                            }
                        }
                    }
                }
            } else {
                // Multi-threaded batched Vamana construction via std::thread::scope
                let batch_size = (node_count / 16).clamp(64, 512);
                let mut batch_pruned: Vec<Vec<(u32, f32, f32)>> =
                    vec![Vec::with_capacity(r); batch_size];
                let mut dirty_flags = vec![false; node_count];
                let mut dirty_nodes: Vec<usize> = Vec::with_capacity(batch_size * r);
                let metric = cfg.metric;
                let hm_ref = &hamming_words;

                let mut batch_start = 0usize;
                while batch_start < node_count {
                    let batch_end = (batch_start + batch_size).min(node_count);
                    let cur_batch_len = batch_end - batch_start;
                    let n_workers = effective_threads.min(cur_batch_len).max(1);
                    let chunk_len = (cur_batch_len + n_workers - 1) / n_workers;

                    // Phase A: Parallel greedy_search_build + robust_prune over immutable `&adj`
                    {
                        let adj_ref = &adj;
                        let out_slices = &mut batch_pruned[..cur_batch_len];
                        std::thread::scope(|s| {
                            for (w_idx, out_chunk) in out_slices.chunks_mut(chunk_len).enumerate() {
                                let c_start = batch_start + w_idx * chunk_len;
                                s.spawn(move || {
                                    let mut w_scratch =
                                        BuildWorkerScratch::new(node_count, l_build, r);
                                    for (offset, out_vec) in out_chunk.iter_mut().enumerate() {
                                        let u = c_start + offset;
                                        let u_id = u as u32;
                                        w_scratch.greedy_search_build(
                                            adj_ref,
                                            medoid_node_id,
                                            u,
                                            r,
                                            l_build,
                                            max_prune_cands,
                                            metric,
                                            work_vectors,
                                            hm_ref,
                                            words_per_vec,
                                            dimension,
                                        );
                                        w_scratch.prune_buf.clear();
                                        w_scratch
                                            .prune_buf
                                            .extend_from_slice(&w_scratch.visited_list);
                                        for &existing_nb in adj_ref[u].iter().take(r) {
                                            let nb_idx = existing_nb as usize;
                                            let (sc, d) = eval_build_pair(
                                                metric,
                                                work_vectors,
                                                hm_ref,
                                                words_per_vec,
                                                dimension,
                                                u,
                                                nb_idx,
                                            );
                                            w_scratch.prune_buf.push((existing_nb, sc, d));
                                        }
                                        w_scratch.robust_prune(
                                            u_id,
                                            alpha_pass,
                                            r,
                                            metric,
                                            work_vectors,
                                            hm_ref,
                                            words_per_vec,
                                            dimension,
                                            out_vec,
                                        );
                                    }
                                });
                            }
                        });
                    }

                    // Phase B: Commit forward edges & insert bidirectional back-edges
                    dirty_nodes.clear();
                    for offset in 0..cur_batch_len {
                        let u = batch_start + offset;
                        let u_id = u as u32;
                        adj[u].clear();
                        for &entry in &batch_pruned[offset] {
                            adj[u].push(entry.0);
                        }
                        for &(nb, _, _) in &batch_pruned[offset] {
                            let nb_usize = nb as usize;
                            if !adj[nb_usize].contains(&u_id) {
                                adj[nb_usize].push(u_id);
                                if adj[nb_usize].len() > r && !dirty_flags[nb_usize] {
                                    dirty_flags[nb_usize] = true;
                                    dirty_nodes.push(nb_usize);
                                }
                            }
                        }
                    }

                    // Phase C: Parallel prune of overflowed back-edge neighborhoods
                    if !dirty_nodes.is_empty() {
                        let mut overflow_pruned: Vec<Vec<(u32, f32, f32)>> =
                            vec![Vec::with_capacity(r); dirty_nodes.len()];
                        let n_pworkers = effective_threads.min(dirty_nodes.len()).max(1);
                        let p_chunk_len = (dirty_nodes.len() + n_pworkers - 1) / n_pworkers;
                        let adj_ref = &adj;
                        let dirty_ref = &dirty_nodes;

                        std::thread::scope(|s| {
                            for (w_idx, out_chunk) in
                                overflow_pruned.chunks_mut(p_chunk_len).enumerate()
                            {
                                let d_start = w_idx * p_chunk_len;
                                s.spawn(move || {
                                    let mut w_scratch = BuildWorkerScratch {
                                        visited_flags: Vec::new(),
                                        visit_token: 1,
                                        beam: Vec::new(),
                                        visited_list: Vec::new(),
                                        prune_buf: Vec::with_capacity(r * 2 + 8),
                                        active_buf: Vec::with_capacity(r * 2 + 8),
                                    };
                                    for (off, out_vec) in out_chunk.iter_mut().enumerate() {
                                        let nb_usize = dirty_ref[d_start + off];
                                        w_scratch.prune_buf.clear();
                                        for &w in &adj_ref[nb_usize] {
                                            let w_usize = w as usize;
                                            let (sc, d) = eval_build_pair(
                                                metric,
                                                work_vectors,
                                                hm_ref,
                                                words_per_vec,
                                                dimension,
                                                nb_usize,
                                                w_usize,
                                            );
                                            w_scratch.prune_buf.push((w, sc, d));
                                        }
                                        w_scratch.robust_prune(
                                            nb_usize as u32,
                                            alpha_pass,
                                            r,
                                            metric,
                                            work_vectors,
                                            hm_ref,
                                            words_per_vec,
                                            dimension,
                                            out_vec,
                                        );
                                    }
                                });
                            }
                        });

                        for (idx, &nb_usize) in dirty_nodes.iter().enumerate() {
                            dirty_flags[nb_usize] = false;
                            adj[nb_usize].clear();
                            for &entry in &overflow_pruned[idx] {
                                adj[nb_usize].push(entry.0);
                            }
                        }
                    }

                    batch_start = batch_end;
                }
            }
        }
    }

    // 5. Serialize fixed-stride adjacency slab: N * (R + 1) * 4 bytes
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

    // 6. Encode optional quantized routing slab
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

/// Fused single-pass `SQ8` distance evaluator (eliminates intermediate `approx_buf` materialization
/// and redundant `/ 127.0` or query norm `sqrt()` inside the loop).
#[inline(always)]
fn eval_sq8_row_fused(
    metric: VamanaMetric,
    row: &[u8],
    query_vec: &[f32],
    query_inv_norm: f32,
    dim: usize,
    u: u32,
) -> (u32, f32, f32) {
    const INV_127: f32 = 1.0 / 127.0;
    match metric {
        VamanaMetric::Cosine => {
            if query_inv_norm == 0.0 {
                return (u, 0.0, 1.0);
            }
            let mut dot0 = 0.0f32;
            let mut dot1 = 0.0f32;
            let mut nv0 = 0i32;
            let mut nv1 = 0i32;
            let mut d = 0usize;
            let n4 = dim & !3;
            while d < n4 {
                let c0 = (row[d] as i32) - 128;
                let c1 = (row[d + 1] as i32) - 128;
                let c2 = (row[d + 2] as i32) - 128;
                let c3 = (row[d + 3] as i32) - 128;
                dot0 += query_vec[d] * (c0 as f32) + query_vec[d + 2] * (c2 as f32);
                dot1 += query_vec[d + 1] * (c1 as f32) + query_vec[d + 3] * (c3 as f32);
                nv0 += c0 * c0 + c2 * c2;
                nv1 += c1 * c1 + c3 * c3;
                d += 4;
            }
            let mut dot = dot0 + dot1;
            let mut nv_i32 = nv0 + nv1;
            while d < dim {
                let c = (row[d] as i32) - 128;
                dot += query_vec[d] * (c as f32);
                nv_i32 += c * c;
                d += 1;
            }
            // Note: the 1/127.0 scale cancels in dot / ||v||!
            let nv = nv_i32 as f32;
            let cos_sim = if nv <= 1e-12 {
                0.0
            } else {
                (dot * query_inv_norm) / nv.sqrt()
            };
            (u, cos_sim, 1.0 - cos_sim)
        }
        VamanaMetric::Dot => {
            let mut dot0 = 0.0f32;
            let mut dot1 = 0.0f32;
            let mut d = 0usize;
            let n4 = dim & !3;
            while d < n4 {
                let c0 = ((row[d] as i32) - 128) as f32;
                let c1 = ((row[d + 1] as i32) - 128) as f32;
                let c2 = ((row[d + 2] as i32) - 128) as f32;
                let c3 = ((row[d + 3] as i32) - 128) as f32;
                dot0 += query_vec[d] * c0 + query_vec[d + 2] * c2;
                dot1 += query_vec[d + 1] * c1 + query_vec[d + 3] * c3;
                d += 4;
            }
            let mut dot_raw = dot0 + dot1;
            while d < dim {
                dot_raw += query_vec[d] * (((row[d] as i32) - 128) as f32);
                d += 1;
            }
            let dot = dot_raw * INV_127;
            (u, dot, -dot)
        }
        VamanaMetric::L2 => {
            let mut s0 = 0.0f32;
            let mut s1 = 0.0f32;
            let mut d = 0usize;
            let n4 = dim & !3;
            while d < n4 {
                let v0 = (((row[d] as i32) - 128) as f32) * INV_127;
                let v1 = (((row[d + 1] as i32) - 128) as f32) * INV_127;
                let v2 = (((row[d + 2] as i32) - 128) as f32) * INV_127;
                let v3 = (((row[d + 3] as i32) - 128) as f32) * INV_127;
                let df0 = query_vec[d] - v0;
                let df1 = query_vec[d + 1] - v1;
                let df2 = query_vec[d + 2] - v2;
                let df3 = query_vec[d + 3] - v3;
                s0 += df0 * df0 + df2 * df2;
                s1 += df1 * df1 + df3 * df3;
                d += 4;
            }
            let mut sum_sq = s0 + s1;
            while d < dim {
                let v = (((row[d] as i32) - 128) as f32) * INV_127;
                let df = query_vec[d] - v;
                sum_sq += df * df;
                d += 1;
            }
            (u, -sum_sq, sum_sq)
        }
        VamanaMetric::Hamming => {
            let mut ham = 0u32;
            for d in 0..dim {
                let bq = query_vec[d] >= 0.0;
                let bv = row[d] >= 128;
                if bq != bv {
                    ham += 1;
                }
            }
            let dist = ham as f32;
            (u, -dist, dist)
        }
    }
}

/// Fused single-pass `FP16` distance evaluator (eliminates intermediate `approx_buf` materialization).
#[inline(always)]
fn eval_fp16_row_fused(
    metric: VamanaMetric,
    row: &[u8],
    query_vec: &[f32],
    query_inv_norm: f32,
    dim: usize,
    u: u32,
) -> (u32, f32, f32) {
    match metric {
        VamanaMetric::Cosine => {
            if query_inv_norm == 0.0 {
                return (u, 0.0, 1.0);
            }
            let mut dot0 = 0.0f32;
            let mut dot1 = 0.0f32;
            let mut dot2 = 0.0f32;
            let mut dot3 = 0.0f32;
            let mut nv0 = 0.0f32;
            let mut nv1 = 0.0f32;
            let mut nv2 = 0.0f32;
            let mut nv3 = 0.0f32;
            let mut d = 0usize;
            let n8 = dim & !7;
            while d < n8 {
                let off = d * 2;
                let v0 = f32_from_fp16_fast(u16::from_le_bytes([row[off], row[off + 1]]));
                let v1 = f32_from_fp16_fast(u16::from_le_bytes([row[off + 2], row[off + 3]]));
                let v2 = f32_from_fp16_fast(u16::from_le_bytes([row[off + 4], row[off + 5]]));
                let v3 = f32_from_fp16_fast(u16::from_le_bytes([row[off + 6], row[off + 7]]));
                let v4 = f32_from_fp16_fast(u16::from_le_bytes([row[off + 8], row[off + 9]]));
                let v5 = f32_from_fp16_fast(u16::from_le_bytes([row[off + 10], row[off + 11]]));
                let v6 = f32_from_fp16_fast(u16::from_le_bytes([row[off + 12], row[off + 13]]));
                let v7 = f32_from_fp16_fast(u16::from_le_bytes([row[off + 14], row[off + 15]]));
                dot0 += query_vec[d] * v0 + query_vec[d + 4] * v4;
                dot1 += query_vec[d + 1] * v1 + query_vec[d + 5] * v5;
                dot2 += query_vec[d + 2] * v2 + query_vec[d + 6] * v6;
                dot3 += query_vec[d + 3] * v3 + query_vec[d + 7] * v7;
                nv0 += v0 * v0 + v4 * v4;
                nv1 += v1 * v1 + v5 * v5;
                nv2 += v2 * v2 + v6 * v6;
                nv3 += v3 * v3 + v7 * v7;
                d += 8;
            }
            let mut dot = (dot0 + dot1) + (dot2 + dot3);
            let mut nv = (nv0 + nv1) + (nv2 + nv3);
            while d < dim {
                let off = d * 2;
                let v = f32_from_fp16_fast(u16::from_le_bytes([row[off], row[off + 1]]));
                dot += query_vec[d] * v;
                nv += v * v;
                d += 1;
            }
            let cos_sim = if nv <= 1e-12 {
                0.0
            } else {
                (dot * query_inv_norm) / nv.sqrt()
            };
            (u, cos_sim, 1.0 - cos_sim)
        }
        VamanaMetric::Dot => {
            let mut dot0 = 0.0f32;
            let mut dot1 = 0.0f32;
            let mut dot2 = 0.0f32;
            let mut dot3 = 0.0f32;
            let mut d = 0usize;
            let n8 = dim & !7;
            while d < n8 {
                let off = d * 2;
                let v0 = f32_from_fp16_fast(u16::from_le_bytes([row[off], row[off + 1]]));
                let v1 = f32_from_fp16_fast(u16::from_le_bytes([row[off + 2], row[off + 3]]));
                let v2 = f32_from_fp16_fast(u16::from_le_bytes([row[off + 4], row[off + 5]]));
                let v3 = f32_from_fp16_fast(u16::from_le_bytes([row[off + 6], row[off + 7]]));
                let v4 = f32_from_fp16_fast(u16::from_le_bytes([row[off + 8], row[off + 9]]));
                let v5 = f32_from_fp16_fast(u16::from_le_bytes([row[off + 10], row[off + 11]]));
                let v6 = f32_from_fp16_fast(u16::from_le_bytes([row[off + 12], row[off + 13]]));
                let v7 = f32_from_fp16_fast(u16::from_le_bytes([row[off + 14], row[off + 15]]));
                dot0 += query_vec[d] * v0 + query_vec[d + 4] * v4;
                dot1 += query_vec[d + 1] * v1 + query_vec[d + 5] * v5;
                dot2 += query_vec[d + 2] * v2 + query_vec[d + 6] * v6;
                dot3 += query_vec[d + 3] * v3 + query_vec[d + 7] * v7;
                d += 8;
            }
            let mut dot = (dot0 + dot1) + (dot2 + dot3);
            while d < dim {
                let off = d * 2;
                let v = f32_from_fp16_fast(u16::from_le_bytes([row[off], row[off + 1]]));
                dot += query_vec[d] * v;
                d += 1;
            }
            (u, dot, -dot)
        }
        VamanaMetric::L2 => {
            let mut s0 = 0.0f32;
            let mut s1 = 0.0f32;
            let mut s2 = 0.0f32;
            let mut s3 = 0.0f32;
            let mut d = 0usize;
            let n8 = dim & !7;
            while d < n8 {
                let off = d * 2;
                let df0 =
                    query_vec[d] - f32_from_fp16_fast(u16::from_le_bytes([row[off], row[off + 1]]));
                let df1 = query_vec[d + 1]
                    - f32_from_fp16_fast(u16::from_le_bytes([row[off + 2], row[off + 3]]));
                let df2 = query_vec[d + 2]
                    - f32_from_fp16_fast(u16::from_le_bytes([row[off + 4], row[off + 5]]));
                let df3 = query_vec[d + 3]
                    - f32_from_fp16_fast(u16::from_le_bytes([row[off + 6], row[off + 7]]));
                let df4 = query_vec[d + 4]
                    - f32_from_fp16_fast(u16::from_le_bytes([row[off + 8], row[off + 9]]));
                let df5 = query_vec[d + 5]
                    - f32_from_fp16_fast(u16::from_le_bytes([row[off + 10], row[off + 11]]));
                let df6 = query_vec[d + 6]
                    - f32_from_fp16_fast(u16::from_le_bytes([row[off + 12], row[off + 13]]));
                let df7 = query_vec[d + 7]
                    - f32_from_fp16_fast(u16::from_le_bytes([row[off + 14], row[off + 15]]));
                s0 += df0 * df0 + df4 * df4;
                s1 += df1 * df1 + df5 * df5;
                s2 += df2 * df2 + df6 * df6;
                s3 += df3 * df3 + df7 * df7;
                d += 8;
            }
            let mut sum_sq = (s0 + s1) + (s2 + s3);
            while d < dim {
                let off = d * 2;
                let df =
                    query_vec[d] - f32_from_fp16_fast(u16::from_le_bytes([row[off], row[off + 1]]));
                sum_sq += df * df;
                d += 1;
            }
            (u, -sum_sq, sum_sq)
        }
        VamanaMetric::Hamming => {
            let mut ham = 0u32;
            for d in 0..dim {
                let off = d * 2;
                let h16 = u16::from_le_bytes([row[off], row[off + 1]]);
                let bq = query_vec[d] >= 0.0;
                let bv = (h16 & 0x8000) == 0 || (h16 & 0x7FFF) == 0;
                if bq != bv {
                    ham += 1;
                }
            }
            let dist = ham as f32;
            (u, -dist, dist)
        }
    }
}

/// Evaluates quantized routing score & distance for node `u` from `quant_slab`.
/// Uses word-packed `POPCNT` for `BQ1` and single-pass fused accumulation for `SQ8` and `FP16`
/// without writing to `approx_buf`.
#[inline]
pub fn eval_quantized_routing_node(
    hdr: &VamanaHeader,
    quant_slab: &[u8],
    query_vec: &[f32],
    u: usize,
    _approx_buf: &mut [f32],
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
    let row = &quant_slab[row_off..row_off + row_bytes];

    match quant_type {
        VamanaQuantType::None => Err("eval_quantized_routing_node called with QuantNone".to_string()),
        VamanaQuantType::Bq1 => {
            imp_assert::testcase(401, true);
            let mut q_words = Vec::with_capacity((dim + 63) >> 6);
            pack_bq1_query_words(query_vec, dim, &mut q_words);
            Ok(eval_bq1_words_row(row, &q_words, dim, u as u32))
        }
        VamanaQuantType::Sq8 => {
            imp_assert::testcase(402, true);
            let q_inv_norm = if metric == VamanaMetric::Cosine {
                let nq = fast_dot_f32(&query_vec[..dim], &query_vec[..dim]);
                if nq <= 1e-12 { 0.0 } else { 1.0 / nq.sqrt() }
            } else {
                1.0
            };
            Ok(eval_sq8_row_fused(
                metric,
                row,
                &query_vec[..dim],
                q_inv_norm,
                dim,
                u as u32,
            ))
        }
        VamanaQuantType::Fp16 => {
            imp_assert::testcase(403, true);
            let q_inv_norm = if metric == VamanaMetric::Cosine {
                let nq = fast_dot_f32(&query_vec[..dim], &query_vec[..dim]);
                if nq <= 1e-12 { 0.0 } else { 1.0 / nq.sqrt() }
            } else {
                1.0
            };
            Ok(eval_fp16_row_fused(
                metric,
                row,
                &query_vec[..dim],
                q_inv_norm,
                dim,
                u as u32,
            ))
        }
    }
}

/// Reusable zero-allocation search scratchpad for Vamana ANN queries.
/// Uses a generation-stamped `visited_gen` array (`cur_gen` = visited, `cur_gen + 1` = expanded)
/// so repeated queries over large $N$ perform zero $O(N)$ allocations or `memset`s.
#[derive(Debug, Clone, Default)]
pub struct VamanaSearchScratch {
    visited_gen: Vec<u32>,
    cur_gen: u32,
    candidates: Vec<(u32, f32, f32)>,
    query_bq1_words: Vec<u64>,
}

impl VamanaSearchScratch {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn with_capacity(node_count: usize, ef_capacity: usize, dim: usize) -> Self {
        Self {
            visited_gen: vec![0u32; node_count],
            cur_gen: 0,
            candidates: Vec::with_capacity(ef_capacity + 1),
            query_bq1_words: Vec::with_capacity((dim + 63) >> 6),
        }
    }

    #[inline]
    fn prepare_query(&mut self, n_nodes: usize, ef: usize) -> u32 {
        if self.visited_gen.len() < n_nodes {
            self.visited_gen.resize(n_nodes, 0u32);
        }
        if self.cur_gen >= u32::MAX - 4 {
            self.visited_gen.fill(0);
            self.cur_gen = 2;
        } else {
            self.cur_gen += 2;
        }
        self.candidates.clear();
        if self.candidates.capacity() < ef + 1 {
            self.candidates.reserve(ef + 1 - self.candidates.capacity());
        }
        self.cur_gen
    }
}

thread_local! {
    static TLS_VAMANA_SCRATCH: RefCell<VamanaSearchScratch> = RefCell::new(VamanaSearchScratch::new());
}

/// Executes Vamana Greedy Beam Search using a caller-provided reusable `VamanaSearchScratch`.
/// Eliminates per-query $O(N)$ `visited`/`expanded` heap allocations and uses pre-packed `BQ1`
/// `u64` words and fused single-pass `SQ8`/`FP16` Stage-1 routing kernels.
pub fn vamana_ann_search_with_scratch(
    hdr: &VamanaHeader,
    adj_slab: &[u8],
    quant_slab: Option<&[u8]>,
    vectors: &[f32],
    query_vec: &[f32],
    top_k: usize,
    ef_search: usize,
    scratch: &mut VamanaSearchScratch,
) -> Result<Vec<(u32, f32, f32)>, String> {
    let n_nodes = hdr.node_count as usize;
    let dim = hdr.dimension as usize;
    if n_nodes == 0 || top_k == 0 {
        return Ok(Vec::new());
    }
    if query_vec.len() < dim {
        return Err("Query vector dimension smaller than Vamana index dimension".to_string());
    }
    let metric = VamanaMetric::from_u8(hdr.metric)
        .ok_or_else(|| format!("Unsupported Vamana metric {}", hdr.metric))?;
    let quant_type = VamanaQuantType::from_u8(hdr.quant_type)
        .ok_or_else(|| format!("Unsupported Vamana quant_type {}", hdr.quant_type))?;

    let ef = top_k.max(ef_search).max(1);
    let r = hdr.max_degree_r as usize;
    let row_stride = (r + 1) * 4;

    let use_quant_routing =
        quant_type != VamanaQuantType::None && quant_slab.map(|s| !s.is_empty()).unwrap_or(false);
    let q_slice = &query_vec[..dim];

    // Precompute query-invariant state once per search
    let query_inv_norm = if metric == VamanaMetric::Cosine {
        let nq = fast_dot_f32(q_slice, q_slice);
        if nq <= 1e-12 { 0.0 } else { 1.0 / nq.sqrt() }
    } else {
        1.0
    };

    let visit_tok = scratch.prepare_query(n_nodes, ef);
    let expand_tok = visit_tok + 1;

    if use_quant_routing && quant_type == VamanaQuantType::Bq1 {
        pack_bq1_query_words(q_slice, dim, &mut scratch.query_bq1_words);
    }

    let q_row_bytes = quant_type.row_bytes(dim);
    let q_slab = quant_slab.unwrap_or(&[]);

    let eval_exact_node = |u: usize| -> Result<(u32, f32, f32), String> {
        let start = u * dim;
        if start + dim > vectors.len() {
            return Err("Vector attribute read out of bounds".to_string());
        }
        let vec = &vectors[start..start + dim];
        let (score, dist) = match metric {
            VamanaMetric::Cosine => {
                if query_inv_norm == 0.0 {
                    (0.0, 1.0)
                } else {
                    let dot = fast_dot_f32(q_slice, vec);
                    let nv = fast_dot_f32(vec, vec);
                    let cos_sim = if nv <= 1e-12 {
                        0.0
                    } else {
                        (dot * query_inv_norm) / nv.sqrt()
                    };
                    (cos_sim, 1.0 - cos_sim)
                }
            }
            _ => compute_vector_metric(metric, q_slice, vec),
        };
        Ok((u as u32, score, dist))
    };

    let eval_stage1_node = |u: usize, bq1_words: &[u64]| -> Result<(u32, f32, f32), String> {
        if !use_quant_routing {
            return eval_exact_node(u);
        }
        let row_off = u * q_row_bytes;
        if row_off + q_row_bytes > q_slab.len() {
            return Err("Vamana quantized routing read out of bounds".to_string());
        }
        let row = &q_slab[row_off..row_off + q_row_bytes];
        match quant_type {
            VamanaQuantType::None => eval_exact_node(u),
            VamanaQuantType::Bq1 => {
                imp_assert::testcase(401, true);
                Ok(eval_bq1_words_row(row, bq1_words, dim, u as u32))
            }
            VamanaQuantType::Sq8 => {
                imp_assert::testcase(402, true);
                Ok(eval_sq8_row_fused(
                    metric,
                    row,
                    q_slice,
                    query_inv_norm,
                    dim,
                    u as u32,
                ))
            }
            VamanaQuantType::Fp16 => {
                imp_assert::testcase(403, true);
                Ok(eval_fp16_row_fused(
                    metric,
                    row,
                    q_slice,
                    query_inv_norm,
                    dim,
                    u as u32,
                ))
            }
        }
    };

    let medoid = hdr.medoid_node_id as usize;
    if medoid >= n_nodes {
        return Err(format!(
            "Vamana medoid_node_id {} out of bounds (node_count={})",
            medoid, n_nodes
        ));
    }

    let medoid_entry = eval_stage1_node(medoid, &scratch.query_bq1_words)?;
    scratch.visited_gen[medoid] = visit_tok;
    scratch.candidates.push(medoid_entry);

    let mut first_unexpanded = 0usize;
    loop {
        let mut next_pos = None;
        for idx in first_unexpanded..scratch.candidates.len() {
            let u = scratch.candidates[idx].0 as usize;
            if scratch.visited_gen[u] != expand_tok {
                next_pos = Some(idx);
                break;
            }
        }
        let Some(pos) = next_pos else {
            break;
        };
        first_unexpanded = pos + 1;
        let u_usize = scratch.candidates[pos].0 as usize;
        scratch.visited_gen[u_usize] = expand_tok;

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
            if nb < n_nodes && scratch.visited_gen[nb] < visit_tok {
                scratch.visited_gen[nb] = visit_tok;
                let entry = eval_stage1_node(nb, &scratch.query_bq1_words)?;

                if scratch.candidates.len() == ef {
                    let worst = &scratch.candidates[ef - 1];
                    if compare_cand(&entry, worst) != Ordering::Less {
                        continue;
                    }
                }
                let ins = scratch
                    .candidates
                    .binary_search_by(|probe| compare_cand(probe, &entry))
                    .unwrap_or_else(|e| e);
                if ins < ef {
                    scratch.candidates.insert(ins, entry);
                    if ins < first_unexpanded {
                        first_unexpanded = ins;
                    }
                    if scratch.candidates.len() > ef {
                        scratch.candidates.pop();
                    }
                }
            }
        }
    }

    if !use_quant_routing {
        let out_len = top_k.min(scratch.candidates.len());
        Ok(scratch.candidates[..out_len].to_vec())
    } else {
        // Stage 2: Exact Full-Precision Float32 Reranking of the `ef` beam candidates
        let mut reranked: Vec<(u32, f32, f32)> = Vec::with_capacity(scratch.candidates.len());
        for &(u, _, _) in &scratch.candidates {
            reranked.push(eval_exact_node(u as usize)?);
        }
        reranked.sort_unstable_by(compare_cand);
        reranked.truncate(top_k);
        Ok(reranked)
    }
}

/// Executes Vamana Greedy Beam Search over `adj_slab`, optional `quant_slab`, and full-precision `vectors`.
/// Supports Two-Stage Quantized Routing (`BQ1` / `SQ8` / `FP16` during beam expansion followed by exact
/// `Float32` reranking of the `ef_search` beam candidates), matching OCaml `Snapshot.vamana_ann_search`.
/// Automatically reuses a thread-local `VamanaSearchScratch` to avoid per-query $O(N)$ allocations.
pub fn vamana_ann_search(
    hdr: &VamanaHeader,
    adj_slab: &[u8],
    quant_slab: Option<&[u8]>,
    vectors: &[f32],
    query_vec: &[f32],
    top_k: usize,
    ef_search: usize,
) -> Result<Vec<(u32, f32, f32)>, String> {
    TLS_VAMANA_SCRATCH.with(|cell| {
        let mut scratch = cell.borrow_mut();
        vamana_ann_search_with_scratch(
            hdr,
            adj_slab,
            quant_slab,
            vectors,
            query_vec,
            top_k,
            ef_search,
            &mut scratch,
        )
    })
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

    let recall_at_1 = if !approx_top_k.is_empty()
        && !exact_top_k.is_empty()
        && approx_top_k[0] == exact_top_k[0]
    {
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
