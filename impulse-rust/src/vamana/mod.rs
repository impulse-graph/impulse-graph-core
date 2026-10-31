//! Vamana (DiskANN) Single-Layer Graph Index Builder, Permuter & Two-Stage Quantized Search
//!
//! Implements Section 2.6 `IMP_INDEX_VAMANA_ANN = 8` (`0x08`) in pure zero-dependency Rust,
//! maintaining 100% binary layout and numerical parity with OCaml `snapshot.ml` and C++ `ImpulseVM`.

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


/// 8-way unrolled dot product for auto-vectorization (NEON / AVX2).
#[inline(always)]
pub(crate) fn fast_dot_f32(a: &[f32], b: &[f32]) -> f32 {
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
pub(crate) fn fast_l2_sq_f32(a: &[f32], b: &[f32]) -> f32 {
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
pub(crate) fn compare_cand(a: &(u32, f32, f32), b: &(u32, f32, f32)) -> Ordering {
    a.2.total_cmp(&b.2).then_with(|| a.0.cmp(&b.0))
}


pub mod builder;
pub mod quant;
pub mod search;

pub use builder::*;
pub use quant::*;
pub use search::*;
