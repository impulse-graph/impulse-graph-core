//! Parallel Vamana (DiskANN) graph builder, RNG α-pruning, and BFS cache-locality permuter.

use crate::spec::{VamanaHeader, VamanaMetric, VamanaQuantType, VAMANA_MAGIC, VAMANA_VERSION};
use std::cmp::Ordering;

use super::{
    compare_cand, compute_vector_metric, encode_quant_slab, fast_dot_f32, fast_l2_sq_f32,
    BuiltVamanaIndex, VamanaBuildConfig,
};

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
