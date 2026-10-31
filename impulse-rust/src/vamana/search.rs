//! Vamana Two-Stage Quantized Beam Search, Reusable Scratchpad, and Recall/nDCG Oracles.

use crate::assert as imp_assert;
use crate::spec::{VamanaHeader, VamanaMetric, VamanaQuantType};
use std::cell::RefCell;
use std::cmp::Ordering;

use super::{
    compare_cand, compute_vector_metric, eval_bq1_words_row, eval_fp16_row_fused,
    eval_sq8_row_fused, fast_dot_f32, pack_bq1_query_words,
};

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
