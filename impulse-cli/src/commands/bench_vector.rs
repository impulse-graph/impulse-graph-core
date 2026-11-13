//! Empirical Vamana (`IMP_INDEX_VAMANA_ANN = 8`) Indexing & Two-Stage Quantized Search Benchmark Suite
//!
//! Benchmarks Vamana graph construction throughput, index memory footprint, two-stage quantized
//! beam search (`NONE`, `FP16`, `SQ8`, `BQ1`) + exact `Float32` reranking latency (`p50`/`p95`/`p99` us),
//! throughput (`QPS`), and retrieval quality (`Recall@1`, `Recall@K`, `nDCG@K`, `MRR`) against an exact
//! brute-force oracle, plus pre- vs post-`optimize` (`vamana-bfs` / `rcm`) layout comparisons.

use crate::commands::optimize::{compute_rcm_permutation, OptimizeOptions};
use crate::embeddings::load_embeddings_from_path;
use comfy_table::Table;
use impulse_graph::spec::{
    BaseDataType, KeyType, VamanaMetric, VamanaQuantType, IMP_INDEX_VAMANA_ANN,
};
use impulse_graph::vamana::{
    brute_force_ann_search, build_vamana_index, compute_vamana_bfs_order, evaluate_ann_quality,
    permute_vamana_index, VamanaBuildConfig,
};
use impulse_graph::{SnapshotReader, SnapshotWriter};
use rand::rngs::StdRng;
use rand::{Rng, SeedableRng};
use serde_json::json;
use std::error::Error;
use std::path::PathBuf;
use std::time::Instant;

#[derive(clap::Args, Debug, Clone)]
pub struct BenchVectorConfig {
    /// Optional existing binary snapshot (.imps) with Float32 vector embeddings
    #[arg(long, value_name = "SNAPSHOT")]
    pub snapshot: Option<PathBuf>,

    /// Optional external embeddings file (.npy, .fvecs, .bvecs, .safetensors, .parquet, .jsonl, .csv)
    #[arg(long = "embeddings", value_name = "FILE")]
    pub embeddings_file: Option<PathBuf>,

    /// Number of synthetic nodes when generating community-manifold + homophily graph
    #[arg(short = 'n', long, default_value_t = 2_000)]
    pub nodes: usize,

    /// Embedding vector dimension D
    #[arg(short = 'd', long, default_value_t = 64)]
    pub dim: usize,

    /// Number of Gaussian manifold clusters / communities
    #[arg(short = 'c', long, default_value_t = 24)]
    pub clusters: usize,

    /// Number of benchmark query vectors
    #[arg(short = 'q', long, default_value_t = 100)]
    pub queries: usize,

    /// Top-K nearest neighbors to retrieve and evaluate
    #[arg(short = 'k', long, default_value_t = 10)]
    pub top_k: usize,

    /// Vamana max out-degree R
    #[arg(long = "max-degree", default_value_t = 24)]
    pub max_degree_r: u32,

    /// Vamana construction beam width L (ef_construction)
    #[arg(long = "ef-construction", default_value_t = 64)]
    pub ef_construction: u32,

    /// Vamana robust-prune diversity parameter alpha
    #[arg(long, default_value_t = 1.2)]
    pub alpha: f32,

    /// Distance metric (cosine, l2, dot)
    #[arg(long, default_value = "cosine")]
    pub metric: String,

    /// Comma-separated search beam widths ef_search to sweep
    #[arg(long = "ef-search", value_delimiter = ',', default_values_t = vec![16usize, 32usize, 64usize])]
    pub ef_search_list: Vec<usize>,

    /// Compare pre-optimize vs post-optimize (vamana-bfs & rcm) search latency and recall
    #[arg(long, default_value_t = true)]
    pub compare_optimize: bool,

    /// Output format (text, json)
    #[arg(long, default_value = "text")]
    pub format: String,

    /// Deterministic PRNG seed
    #[arg(long, default_value_t = 42)]
    pub seed: u64,
}

pub type BenchVectorOptions = BenchVectorConfig;

impl Default for BenchVectorConfig {
    fn default() -> Self {
        Self {
            snapshot: None,
            embeddings_file: None,
            nodes: 2_000,
            dim: 64,
            clusters: 24,
            queries: 100,
            top_k: 10,
            max_degree_r: 24,
            ef_construction: 64,
            alpha: 1.2,
            metric: "cosine".to_string(),
            ef_search_list: vec![16, 32, 64],
            compare_optimize: true,
            format: "table".to_string(),
            seed: 0x5641_4D41_BEEF_2026,
        }
    }
}

/// Generates a realistic clustered-manifold + CSR graph-homophily smoothed dataset:
/// - `K` latent topic centroids on the unit hypersphere $\mathbb{S}^{D-1}$
/// - Barabási-Albert + intra-cluster community edges (`row_offsets`, `col_indices`)
/// - 1-hop neighbor homophily smoothing on embeddings
/// - `num_queries` held-out query vectors sampled from the manifold
pub fn generate_manifold_graph_and_queries(
    num_nodes: usize,
    dim: usize,
    num_clusters: usize,
    num_queries: usize,
    seed: u64,
) -> (Vec<u32>, Vec<u32>, Vec<f32>, Vec<f32>) {
    let mut rng = StdRng::seed_from_u64(seed);
    let k = num_clusters.max(2);

    // 1. Sample K cluster centroids on unit hypersphere
    let mut centroids = vec![0.0f32; k * dim];
    for c in 0..k {
        let mut norm = 0.0f32;
        for d in 0..dim {
            let v: f32 = rng.gen_range(-1.0..1.0);
            centroids[c * dim + d] = v;
            norm += v * v;
        }
        norm = norm.sqrt().max(1e-6);
        for d in 0..dim {
            centroids[c * dim + d] /= norm;
        }
    }

    // 2. Generate community-clustered graph edges (8 intra-cluster + 2 inter-cluster edges/node)
    let mut row_offsets = vec![0u32; num_nodes + 1];
    let mut col_indices = Vec::with_capacity(num_nodes * 10);
    for u in 0..num_nodes {
        let cu = u % k;
        let mut nbrs = Vec::with_capacity(10);
        for step in 1..=8 {
            let cand = (u + step * k) % num_nodes;
            if cand != u && !nbrs.contains(&(cand as u32)) {
                nbrs.push(cand as u32);
            }
        }
        for _ in 0..2 {
            let cand = rng.gen_range(0..num_nodes);
            if cand != u && (cand % k != cu) && !nbrs.contains(&(cand as u32)) {
                nbrs.push(cand as u32);
            }
        }
        nbrs.sort_unstable();
        row_offsets[u + 1] = row_offsets[u] + (nbrs.len() as u32);
        col_indices.extend_from_slice(&nbrs);
    }

    // 3. Sample raw node embeddings around cluster centroids
    let mut raw_vecs = vec![0.0f32; num_nodes * dim];
    for u in 0..num_nodes {
        let cu = u % k;
        let c_slice = &centroids[cu * dim..(cu + 1) * dim];
        let mut norm = 0.0f32;
        for d in 0..dim {
            let noise: f32 = rng.gen_range(-0.32..0.32);
            let val = c_slice[d] + noise;
            raw_vecs[u * dim + d] = val;
            norm += val * val;
        }
        norm = norm.sqrt().max(1e-6);
        for d in 0..dim {
            raw_vecs[u * dim + d] /= norm;
        }
    }

    // 4. Apply 1-hop CSR graph homophily smoothing: v_u <- normalize(0.70 * v_u + 0.30 * mean_{w in N(u)} v_w)
    let mut smoothed = vec![0.0f32; num_nodes * dim];
    for u in 0..num_nodes {
        let start = row_offsets[u] as usize;
        let end = row_offsets[u + 1] as usize;
        let deg = end.saturating_sub(start);
        let mut norm = 0.0f32;
        for d in 0..dim {
            let mut nb_sum = 0.0f32;
            for &w in &col_indices[start..end] {
                nb_sum += raw_vecs[(w as usize) * dim + d];
            }
            let val = if deg > 0 {
                0.70 * raw_vecs[u * dim + d] + 0.30 * (nb_sum / (deg as f32))
            } else {
                raw_vecs[u * dim + d]
            };
            smoothed[u * dim + d] = val;
            norm += val * val;
        }
        norm = norm.sqrt().max(1e-6);
        for d in 0..dim {
            smoothed[u * dim + d] /= norm;
        }
    }

    // 5. Sample held-out query vectors by perturbing random anchor nodes along manifold directions
    let mut queries = vec![0.0f32; num_queries * dim];
    for q_idx in 0..num_queries {
        let anchor = rng.gen_range(0..num_nodes);
        let anchor_vec = &smoothed[anchor * dim..(anchor + 1) * dim];
        let mut norm = 0.0f32;
        for d in 0..dim {
            let noise: f32 = rng.gen_range(-0.12..0.12);
            let val = anchor_vec[d] + noise;
            queries[q_idx * dim + d] = val;
            norm += val * val;
        }
        norm = norm.sqrt().max(1e-6);
        for d in 0..dim {
            queries[q_idx * dim + d] /= norm;
        }
    }

    (row_offsets, col_indices, smoothed, queries)
}

#[derive(Debug, Clone)]
pub struct BenchRowResult {
    pub label: String,
    pub quant: String,
    pub ef_search: usize,
    pub build_ms: f64,
    pub build_vec_per_sec: f64,
    pub index_kb: f64,
    pub bytes_per_vec: f64,
    pub recall_at_1: f64,
    pub recall_at_k: f64,
    pub ndcg_at_k: f64,
    pub mrr: f64,
    pub p50_us: f64,
    pub p95_us: f64,
    pub p99_us: f64,
    pub qps: f64,
    pub speedup_vs_bf: f64,
}

pub fn run(cfg: &BenchVectorConfig) -> Result<Vec<BenchRowResult>, Box<dyn Error>> {
    let metric = match cfg.metric.to_ascii_lowercase().as_str() {
        "l2" | "euclidean" => VamanaMetric::L2,
        "dot" | "ip" => VamanaMetric::Dot,
        "hamming" => VamanaMetric::Hamming,
        _ => VamanaMetric::Cosine,
    };

    // 1. Load or generate dataset
    let (row_offsets, col_indices, vectors, n_nodes, dim, query_vecs) =
        if let Some(ref snap_path) = cfg.snapshot {
            let reader = SnapshotReader::open(snap_path)?;
            let dom_id = reader.domains().first().map(|d| d.domain_id).unwrap_or(0);
            let (n, d, vecs) = reader.get_domain_float32_vectors(dom_id, None, None)?;
            let (ro, ci) = if !reader.relations().is_empty() {
                (
                    reader.get_row_offsets(0)?.to_vec(),
                    reader.get_col_indices(0)?.to_vec(),
                )
            } else {
                (vec![0u32; n + 1], Vec::new())
            };
            if n == 0 || d == 0 {
                return Err("Cannot benchmark vector queries on empty dataset (n == 0 or d == 0)".into());
            }
            let mut rng = StdRng::seed_from_u64(cfg.seed);
            let mut qvecs = vec![0.0f32; cfg.queries * d];
            for qi in 0..cfg.queries {
                let u = rng.gen_range(0..n);
                for j in 0..d {
                    qvecs[qi * d + j] = vecs[u * d + j] + rng.gen_range(-0.08..0.08);
                }
            }
            (ro, ci, vecs, n, d, qvecs)
        } else if let Some(ref emb_path) = cfg.embeddings_file {
            let loaded = load_embeddings_from_path(emb_path, None, metric == VamanaMetric::Cosine)?;
            let n = loaded.num_vectors;
            let d = loaded.dimension;
            if n == 0 || d == 0 {
                return Err("Cannot benchmark vector queries on empty dataset (n == 0 or d == 0)".into());
            }
            let mut rng = StdRng::seed_from_u64(cfg.seed);
            let mut qvecs = vec![0.0f32; cfg.queries * d];
            for qi in 0..cfg.queries {
                let u = rng.gen_range(0..n);
                for j in 0..d {
                    qvecs[qi * d + j] = loaded.vectors[u * d + j] + rng.gen_range(-0.08..0.08);
                }
            }
            (vec![0u32; n + 1], Vec::new(), loaded.vectors, n, d, qvecs)
        } else {
            let (ro, ci, vecs, qvecs) = generate_manifold_graph_and_queries(
                cfg.nodes,
                cfg.dim,
                cfg.clusters,
                cfg.queries,
                cfg.seed,
            );
            (ro, ci, vecs, cfg.nodes, cfg.dim, qvecs)
        };

    let num_queries = cfg.queries.max(1);
    let top_k = cfg.top_k.min(n_nodes).max(1);

    // 2. Compute Exact Brute-Force Ground Truth Oracle & Baseline Latency
    let bf_start = Instant::now();
    let mut exact_results: Vec<Vec<(u32, f32, f32)>> = Vec::with_capacity(num_queries);
    for qi in 0..num_queries {
        let q = &query_vecs[qi * dim..(qi + 1) * dim];
        exact_results.push(brute_force_ann_search(
            metric, &vectors, n_nodes, dim, q, top_k,
        ));
    }
    let bf_total_sec = bf_start.elapsed().as_secs_f64();
    let bf_avg_us = (bf_total_sec * 1e6) / (num_queries as f64);
    let bf_qps = (num_queries as f64) / bf_total_sec.max(1e-9);

    let mut vec_bytes = Vec::with_capacity(vectors.len() * 4);
    for &f in &vectors {
        vec_bytes.extend_from_slice(&f.to_le_bytes());
    }

    let mut results = Vec::new();

    // 3. Benchmark all 4 quantization modes: None (FP32), FP16, SQ8, BQ1
    for quant in [
        VamanaQuantType::None,
        VamanaQuantType::Fp16,
        VamanaQuantType::Sq8,
        VamanaQuantType::Bq1,
    ] {
        let build_cfg = VamanaBuildConfig {
            max_degree_r: cfg.max_degree_r,
            ef_construction: cfg.ef_construction,
            alpha: cfg.alpha,
            metric,
            quant_type: quant,
            seed: cfg.seed,
        };

        let t_build = Instant::now();
        let built = build_vamana_index(0, 0, &vectors, n_nodes, dim, &build_cfg)?;
        let build_sec = t_build.elapsed().as_secs_f64();
        let build_ms = build_sec * 1000.0;
        let build_vec_per_sec = (n_nodes as f64) / build_sec.max(1e-9);

        let mut writer = SnapshotWriter::new("__bench_in_mem.imps");
        writer.add_domain(0, KeyType::Int64, "Entity");
        writer.set_domain_node_count(0, n_nodes as u64);
        writer.add_relation(
            0,
            0,
            n_nodes as u64,
            col_indices.len() as u64,
            row_offsets.clone(),
            col_indices.clone(),
        );
        writer.set_relation_name(0, "LINKS");
        writer.add_attribute_to_domain(
            0,
            "embedding",
            BaseDataType::Float32 as u8,
            dim as u32,
            vec_bytes.clone(),
            None,
        );
        writer.add_built_vamana_index("Entity_embedding_vamana", built);
        let snap_bytes = writer.finalize_to_bytes()?;
        let reader = SnapshotReader::from_bytes(snap_bytes)?;
        let entry = reader
            .index_entries()
            .iter()
            .find(|e| e.index_type == IMP_INDEX_VAMANA_ANN)
            .ok_or("Missing Vamana entry")?;

        let index_kb = (entry.data_bytes as f64) / 1024.0;
        let bytes_per_vec = (entry.data_bytes as f64) / (n_nodes.max(1) as f64);

        for &ef_s in &cfg.ef_search_list {
            let row = measure_search_queries(
                &reader,
                entry,
                &query_vecs,
                &exact_results,
                num_queries,
                dim,
                top_k,
                ef_s,
                "baseline",
                quant.name(),
                build_ms,
                build_vec_per_sec,
                index_kb,
                bytes_per_vec,
                bf_avg_us,
                None,
            )?;
            results.push(row);
        }

        // 4. Optional pre- vs post-optimize comparison on SQ8 (canonical production quantization)
        if cfg.compare_optimize && quant == VamanaQuantType::Sq8 {
            let ef_s = *cfg.ef_search_list.last().unwrap_or(&64);
            let hdr = reader.parse_vamana_header(entry)?;
            let adj_slab = reader.get_vamana_adj_slab(&hdr)?;
            let quant_slab = reader.get_vamana_quant_slab(&hdr)?;

            for (opt_label, (old_to_new, new_to_old)) in [
                ("opt:vamana-bfs", compute_vamana_bfs_order(&hdr, adj_slab)),
                (
                    "opt:rcm",
                    if !col_indices.is_empty() {
                        compute_rcm_permutation(n_nodes, &row_offsets, &col_indices)
                    } else {
                        compute_vamana_bfs_order(&hdr, adj_slab)
                    },
                ),
            ] {
                let permuted_idx =
                    permute_vamana_index(&hdr, adj_slab, quant_slab, &old_to_new, &new_to_old)?;
                let mut permuted_vec_bytes = Vec::with_capacity(vectors.len() * 4);
                for &old_u in &new_to_old {
                    let u = old_u as usize;
                    for &f in &vectors[u * dim..(u + 1) * dim] {
                        permuted_vec_bytes.extend_from_slice(&f.to_le_bytes());
                    }
                }
                let mut opt_writer = SnapshotWriter::new("__bench_opt_in_mem.imps");
                opt_writer.add_domain(0, KeyType::Int64, "Entity");
                opt_writer.set_domain_node_count(0, n_nodes as u64);
                opt_writer.add_attribute_to_domain(
                    0,
                    "embedding",
                    BaseDataType::Float32 as u8,
                    dim as u32,
                    permuted_vec_bytes,
                    None,
                );
                opt_writer.add_built_vamana_index("Entity_embedding_vamana", permuted_idx);
                let opt_bytes = opt_writer.finalize_to_bytes()?;
                let opt_reader = SnapshotReader::from_bytes(opt_bytes)?;
                let opt_entry = &opt_reader.index_entries()[0];

                let opt_row = measure_search_queries(
                    &opt_reader,
                    opt_entry,
                    &query_vecs,
                    &exact_results,
                    num_queries,
                    dim,
                    top_k,
                    ef_s,
                    opt_label,
                    quant.name(),
                    build_ms,
                    build_vec_per_sec,
                    index_kb,
                    bytes_per_vec,
                    bf_avg_us,
                    Some(&new_to_old),
                )?;
                results.push(opt_row);
            }
        }
    }

    // 5. Render output
    if cfg.format == "json" {
        let rows_json: Vec<_> = results
            .iter()
            .map(|r| {
                json!({
                    "layout": r.label,
                    "quant": r.quant,
                    "ef_search": r.ef_search,
                    "build_ms": r.build_ms,
                    "build_vec_per_sec": r.build_vec_per_sec,
                    "index_kb": r.index_kb,
                    "bytes_per_vec": r.bytes_per_vec,
                    "recall_at_1": r.recall_at_1,
                    "recall_at_k": r.recall_at_k,
                    "ndcg_at_k": r.ndcg_at_k,
                    "mrr": r.mrr,
                    "p50_us": r.p50_us,
                    "p95_us": r.p95_us,
                    "p99_us": r.p99_us,
                    "qps": r.qps,
                    "speedup_vs_bf": r.speedup_vs_bf,
                })
            })
            .collect();
        let out = json!({
            "dataset": {
                "nodes": n_nodes,
                "dimension": dim,
                "edges": col_indices.len(),
                "queries": num_queries,
                "top_k": top_k,
                "max_degree_r": cfg.max_degree_r,
                "ef_construction": cfg.ef_construction,
                "alpha": cfg.alpha,
                "metric": metric.name(),
            },
            "brute_force_baseline": {
                "avg_latency_us": bf_avg_us,
                "qps": bf_qps,
            },
            "benchmarks": rows_json,
        });
        println!("{}", serde_json::to_string_pretty(&out)?);
    } else {
        println!("==========================================================================================");
        println!("        IMPULSE VAMANA ANN (IMP_INDEX_VAMANA_ANN = 0x08) EMPIRICAL BENCHMARK SUITE        ");
        println!("==========================================================================================");
        println!(
            "Dataset: N={} vectors, D={} dims, E={} CSR edges | Queries={} | Top-K={} | Metric={}",
            n_nodes,
            dim,
            col_indices.len(),
            num_queries,
            top_k,
            metric.name()
        );
        println!(
            "Vamana Params: R={}, L_build={}, alpha={:.2} | Exact Brute-Force Oracle: {:.1} us/query ({:.0} QPS)",
            cfg.max_degree_r, cfg.ef_construction, cfg.alpha, bf_avg_us, bf_qps
        );
        println!();

        let mut table = Table::new();
        table.set_header(vec![
            "Layout",
            "Quant",
            "ef",
            "Build(ms)",
            "Vec/s",
            "B/Vec",
            "R@1",
            &format!("R@{}", top_k),
            &format!("nDCG@{}", top_k),
            "MRR",
            "p50(us)",
            "p95(us)",
            "p99(us)",
            "QPS",
            "Speedup",
        ]);

        for r in &results {
            table.add_row(vec![
                r.label.clone(),
                r.quant.clone(),
                r.ef_search.to_string(),
                format!("{:.1}", r.build_ms),
                format!("{:.0}", r.build_vec_per_sec),
                format!("{:.0}", r.bytes_per_vec),
                format!("{:.3}", r.recall_at_1),
                format!("{:.3}", r.recall_at_k),
                format!("{:.3}", r.ndcg_at_k),
                format!("{:.3}", r.mrr),
                format!("{:.1}", r.p50_us),
                format!("{:.1}", r.p95_us),
                format!("{:.1}", r.p99_us),
                format!("{:.0}", r.qps),
                format!("{:.2}x", r.speedup_vs_bf),
            ]);
        }
        println!("{}", table);
    }

    let _ = OptimizeOptions::default();
    Ok(results)
}

#[allow(clippy::too_many_arguments)]
fn measure_search_queries(
    reader: &SnapshotReader,
    entry: &impulse_graph::IndexEntryInfo,
    query_vecs: &[f32],
    exact_results: &[Vec<(u32, f32, f32)>],
    num_queries: usize,
    dim: usize,
    top_k: usize,
    ef_search: usize,
    label: &str,
    quant_name: &str,
    build_ms: f64,
    build_vec_per_sec: f64,
    index_kb: f64,
    bytes_per_vec: f64,
    bf_avg_us: f64,
    new_to_old: Option<&[u32]>,
) -> Result<BenchRowResult, Box<dyn Error>> {
    let mut latencies_us = Vec::with_capacity(num_queries);
    let mut sum_r1 = 0.0f64;
    let mut sum_rk = 0.0f64;
    let mut sum_ndcg = 0.0f64;
    let mut sum_mrr = 0.0f64;

    // Warmup 5 queries
    for qi in 0..num_queries.min(5) {
        let q = &query_vecs[qi * dim..(qi + 1) * dim];
        let _ = reader.vamana_ann_search(entry, q, top_k, ef_search)?;
    }

    let t_batch = Instant::now();
    for qi in 0..num_queries {
        let q = &query_vecs[qi * dim..(qi + 1) * dim];
        let t0 = Instant::now();
        let mut approx = reader.vamana_ann_search(entry, q, top_k, ef_search)?;
        let elapsed_us = t0.elapsed().as_secs_f64() * 1e6;
        latencies_us.push(elapsed_us);

        // If snapshot was permuted, map `new_u` back to `old_u` before comparing with ground truth
        if let Some(n2o) = new_to_old {
            for item in &mut approx {
                if (item.0 as usize) < n2o.len() {
                    item.0 = n2o[item.0 as usize];
                }
            }
        }

        let (r1, rk, ndcg, mrr) = evaluate_ann_quality(&approx, &exact_results[qi], top_k);
        sum_r1 += r1;
        sum_rk += rk;
        sum_ndcg += ndcg;
        sum_mrr += mrr;
    }
    let total_sec = t_batch.elapsed().as_secs_f64().max(1e-9);
    latencies_us.sort_by(|a, b| a.total_cmp(b));

    let p50_us = latencies_us[latencies_us.len() * 50 / 100];
    let p95_us = latencies_us[(latencies_us.len() * 95 / 100).min(latencies_us.len() - 1)];
    let p99_us = latencies_us[(latencies_us.len() * 99 / 100).min(latencies_us.len() - 1)];
    let avg_us = (total_sec * 1e6) / (num_queries as f64);
    let qps = (num_queries as f64) / total_sec;
    let speedup_vs_bf = bf_avg_us / avg_us.max(1e-9);

    let inv_q = 1.0 / (num_queries as f64);
    Ok(BenchRowResult {
        label: label.to_string(),
        quant: quant_name.to_string(),
        ef_search,
        build_ms,
        build_vec_per_sec,
        index_kb,
        bytes_per_vec,
        recall_at_1: sum_r1 * inv_q,
        recall_at_k: sum_rk * inv_q,
        ndcg_at_k: sum_ndcg * inv_q,
        mrr: sum_mrr * inv_q,
        p50_us,
        p95_us,
        p99_us,
        qps,
        speedup_vs_bf,
    })
}
