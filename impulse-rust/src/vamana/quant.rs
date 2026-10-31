//! Quantization codecs (`FP16`, `SQ8`, `BQ1`) and fused single-pass Stage-1 routing kernels.

use crate::assert as imp_assert;
use crate::spec::{VamanaHeader, VamanaMetric, VamanaQuantType};
use super::fast_dot_f32;

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
pub(crate) fn f32_from_fp16_fast(h: u16) -> f32 {
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

/// Fused single-pass `SQ8` distance evaluator (eliminates intermediate `approx_buf` materialization
/// and redundant `/ 127.0` or query norm `sqrt()` inside the loop).
#[inline(always)]
pub(crate) fn eval_sq8_row_fused(
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
pub(crate) fn eval_fp16_row_fused(
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
