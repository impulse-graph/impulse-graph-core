//! Spec v0.9.0 Graph Data Generator for Impulse Tooling
//! Supports Graph500 (R-MAT), Social (Barabási-Albert & SBM), Erdős-Rényi, Grid, Tree, and Bipartite topologies.

use crate::GenerateArgs;
use impulse_graph::spec::KeyType;
use impulse_graph::SnapshotWriter;
use rand::rngs::StdRng;
use rand::{Rng, SeedableRng};
use rayon::prelude::*;
use std::collections::HashSet;
use std::error::Error;
use std::fs::File;
use std::io::{BufWriter, Write};
use std::path::Path;
use std::time::Instant;

#[derive(Debug, Clone)]
struct Edge {
    src: u32,
    tgt: u32,
}

#[derive(Debug, Clone)]
struct GeneratedGraph {
    #[allow(dead_code)]
    domain_count: u16,
    domain_names: Vec<String>,
    domain_sizes: Vec<u64>,
    src_domain_id: u16,
    tgt_domain_id: u16,
    relation_name: String,
    edges: Vec<Edge>,
}

#[derive(Debug, Clone)]
struct AttributeSpec {
    name: String,
    type_code: u8, // 1: i32, 2: i64, 3: f32, 4: f64
    dimension: u32,
}

fn parse_attribute_specs(attrs_opt: Option<&str>) -> Vec<AttributeSpec> {
    let mut specs = Vec::new();
    if let Some(attr_str) = attrs_opt {
        for part in attr_str.split(',') {
            let part = part.trim();
            if part.is_empty() {
                continue;
            }
            let subparts: Vec<&str> = part.split(':').collect();
            let name = subparts[0].trim().to_string();
            let type_str = if subparts.len() > 1 {
                subparts[1].trim().to_lowercase()
            } else {
                "f32".to_string()
            };

            let type_code = match type_str.as_str() {
                "i32" | "int32" | "int" => 1,
                "i64" | "int64" | "long" | "timestamp" => 2,
                "f32" | "float32" | "float" | "weight" => 3,
                "f64" | "float64" | "double" => 4,
                _ => 3,
            };

            specs.push(AttributeSpec {
                name,
                type_code,
                dimension: 1,
            });
        }
    }
    specs
}

pub fn run(args: &GenerateArgs) -> Result<(), Box<dyn Error>> {
    let start_time = Instant::now();
    let seed = args.seed.unwrap_or_else(|| rand::thread_rng().gen());

    println!("============================================================");
    println!("  Impulse Graph Data Generator (Spec v0.9.0)");
    println!("============================================================");
    println!("  Profile:         {}", args.profile);
    println!("  Output Target:   {}", args.output.display());
    println!("  Output Format:   {}", args.format.to_lowercase());
    println!("  RNG Seed:        0x{:016X}", seed);

    let graph = match args.profile.to_lowercase().as_str() {
        "graph500" | "rmat" | "kronecker" => generate_graph500(args, seed)?,
        "social" | "barabasi" | "barabasi-albert" | "ba" | "powerlaw" => {
            generate_social_ba(args, seed)?
        }
        "social-sbm" | "sbm" | "communities" | "clusters" => generate_social_sbm(args, seed)?,
        "erdos-renyi" | "erdos_renyi" | "random" | "er" => generate_erdos_renyi(args, seed)?,
        "grid" | "mesh" | "lattice" => generate_grid(args)?,
        "tree" | "hierarchy" | "dag" => generate_tree(args)?,
        "bipartite" | "two-mode" => generate_bipartite(args, seed)?,
        other => {
            return Err(format!(
                "Unknown graph profile '{}'. Supported: graph500, social, social-sbm, erdos-renyi, grid, tree, bipartite",
                other
            ).into());
        }
    };

    let total_nodes: u64 = graph.domain_sizes.iter().sum();
    let edge_count = graph.edges.len();
    println!("  Vertices Total:  {}", total_nodes);
    println!("  Edges Total:     {}", edge_count);
    println!(
        "  Generation Time: {:.3}s",
        start_time.elapsed().as_secs_f64()
    );

    let attr_specs = parse_attribute_specs(args.attributes.as_deref());

    match args.format.to_lowercase().as_str() {
        "imps" | "binary" => {
            write_imps_snapshot(&graph, &args.output, &attr_specs, seed, args)?;
        }
        "tsv" => {
            write_delimited(&graph, &args.output, '\t', &attr_specs, seed)?;
        }
        "csv" => {
            write_delimited(&graph, &args.output, ',', &attr_specs, seed)?;
        }
        other => {
            return Err(format!(
                "Unsupported output format '{}'. Supported: imps, tsv, csv",
                other
            )
            .into());
        }
    }

    println!("------------------------------------------------------------");
    println!(
        "  SUCCESS: Generated {} in {:.3}s",
        args.output.display(),
        start_time.elapsed().as_secs_f64()
    );
    println!("============================================================");

    Ok(())
}

/// Generates a Graph500 Kronecker / R-MAT benchmark graph
fn generate_graph500(args: &GenerateArgs, seed: u64) -> Result<GeneratedGraph, Box<dyn Error>> {
    let scale = args.scale;
    if scale == 0 || scale > 36 {
        return Err("Scale factor S must be between 1 and 36".into());
    }

    let num_nodes = 1u64 << scale;
    let edge_factor = args.edge_factor as u64;
    let target_edges = args.edges.unwrap_or(num_nodes.saturating_mul(edge_factor));

    println!(
        "  Scale Factor S:  {} (N = 2^{} = {} vertices)",
        scale, scale, num_nodes
    );
    println!("  Edge Factor E:   {}", edge_factor);
    println!("  Target Edges:    {}", target_edges);

    let sum_prob = args.a + args.b + args.c + args.d;
    let (prob_a, prob_b, prob_c, _) = if sum_prob > 0.0 {
        (
            args.a / sum_prob,
            args.b / sum_prob,
            args.c / sum_prob,
            args.d / sum_prob,
        )
    } else {
        (0.57, 0.19, 0.19, 0.05)
    };

    let thresh_a = prob_a;
    let thresh_b = prob_a + prob_b;
    let thresh_c = prob_a + prob_b + prob_c;

    println!(
        "  R-MAT Matrix:    [A={:.3}, B={:.3}; C={:.3}, D={:.3}]",
        prob_a,
        prob_b,
        prob_c,
        1.0 - thresh_c
    );

    let num_threads = rayon::current_num_threads();
    let chunk_size = (target_edges as usize).div_ceil(num_threads);

    let raw_edges: Vec<Edge> = (0..num_threads)
        .into_par_iter()
        .flat_map(|thread_id| {
            let mut thread_rng = StdRng::seed_from_u64(
                seed.wrapping_add((thread_id as u64).wrapping_mul(0x9E3779B97F4A7C15)),
            );
            let start_idx = (thread_id * chunk_size) as u64;
            let count = if start_idx >= target_edges {
                0
            } else {
                std::cmp::min(chunk_size as u64, target_edges - start_idx)
            };

            let mut thread_edges =
                Vec::with_capacity(count as usize * if args.undirected { 2 } else { 1 });

            for _ in 0..count {
                let mut u = 0u32;
                let mut v = 0u32;

                for bit in 0..scale {
                    let r: f64 = thread_rng.gen();
                    let bit_val = 1u32 << (scale - 1 - bit);

                    if r < thresh_a {
                        // quadrant 0, 0: u bit 0, v bit 0
                    } else if r < thresh_b {
                        // quadrant 0, 1: u bit 0, v bit 1
                        v |= bit_val;
                    } else if r < thresh_c {
                        // quadrant 1, 0: u bit 1, v bit 0
                        u |= bit_val;
                    } else {
                        // quadrant 1, 1: u bit 1, v bit 1
                        u |= bit_val;
                        v |= bit_val;
                    }
                }

                if !args.allow_self_loops && u == v {
                    continue;
                }

                thread_edges.push(Edge { src: u, tgt: v });
                if args.undirected && u != v {
                    thread_edges.push(Edge { src: v, tgt: u });
                }
            }

            thread_edges
        })
        .collect();

    Ok(GeneratedGraph {
        domain_count: 1,
        domain_names: vec![args.domain_name.clone()],
        domain_sizes: vec![num_nodes],
        src_domain_id: 0,
        tgt_domain_id: 0,
        relation_name: args.relation_name.clone(),
        edges: raw_edges,
    })
}

/// Generates a scale-free social network using Barabási–Albert Preferential Attachment
fn generate_social_ba(args: &GenerateArgs, seed: u64) -> Result<GeneratedGraph, Box<dyn Error>> {
    let num_nodes = args.nodes.unwrap_or(10_000);
    let m = args.edges_per_node as usize;
    let m0 = std::cmp::max(m + 1, 4);

    if (num_nodes as usize) <= m0 {
        return Err("Node count N must be strictly greater than edges-per-node m".into());
    }

    println!("  Vertices N:      {}", num_nodes);
    println!("  Attachment m:    {} edges/node", m);

    let mut rng = StdRng::seed_from_u64(seed);
    let mut edges = Vec::new();
    let mut repeated_nodes = Vec::new();

    // Initial fully connected clique of m0 nodes
    for i in 0..m0 as u32 {
        for j in (i + 1)..m0 as u32 {
            edges.push(Edge { src: i, tgt: j });
            if args.undirected {
                edges.push(Edge { src: j, tgt: i });
            }
            repeated_nodes.push(i);
            repeated_nodes.push(j);
        }
    }

    // Add nodes m0..N with preferential attachment
    let mut chosen_targets = HashSet::with_capacity(m);
    for u in m0 as u32..(num_nodes as u32) {
        chosen_targets.clear();
        let rep_len = repeated_nodes.len();

        while chosen_targets.len() < m {
            let idx = rng.gen_range(0..rep_len);
            let target = repeated_nodes[idx];
            if target != u {
                chosen_targets.insert(target);
            }
        }

        for &v in &chosen_targets {
            edges.push(Edge { src: u, tgt: v });
            if args.undirected {
                edges.push(Edge { src: v, tgt: u });
            }
            repeated_nodes.push(u);
            repeated_nodes.push(v);
        }
    }

    Ok(GeneratedGraph {
        domain_count: 1,
        domain_names: vec![args.domain_name.clone()],
        domain_sizes: vec![num_nodes],
        src_domain_id: 0,
        tgt_domain_id: 0,
        relation_name: args.relation_name.clone(),
        edges,
    })
}

/// Generates a Stochastic Block Model (SBM) social network with community clusters
fn generate_social_sbm(args: &GenerateArgs, seed: u64) -> Result<GeneratedGraph, Box<dyn Error>> {
    let num_nodes = args.nodes.unwrap_or(5_000);
    let k = std::cmp::max(1, args.communities as u64);
    let p_intra = args.p_intra;
    let p_inter = args.p_inter;

    println!("  Vertices N:      {}", num_nodes);
    println!("  Communities K:   {}", k);
    println!("  P(intra):        {:.5}", p_intra);
    println!("  P(inter):        {:.5}", p_inter);

    let num_threads = rayon::current_num_threads();
    let chunk = (num_nodes as usize).div_ceil(num_threads);

    let edges: Vec<Edge> = (0..num_threads)
        .into_par_iter()
        .flat_map(|thread_id| {
            let mut thread_rng = StdRng::seed_from_u64(
                seed.wrapping_add((thread_id as u64).wrapping_mul(0x85EBCA6B)),
            );
            let start_u = (thread_id * chunk) as u32;
            let end_u = std::cmp::min(num_nodes as u32, start_u + chunk as u32);

            let mut thread_edges = Vec::new();

            for u in start_u..end_u {
                let comm_u = (u as u64) % k;
                for v in 0..num_nodes as u32 {
                    if u == v && !args.allow_self_loops {
                        continue;
                    }
                    if !args.undirected && u >= v {
                        // Avoid duplicate processing in directed/undirected mode
                    }

                    let comm_v = (v as u64) % k;
                    let prob = if comm_u == comm_v { p_intra } else { p_inter };

                    let r: f64 = thread_rng.gen();
                    if r < prob {
                        thread_edges.push(Edge { src: u, tgt: v });
                        if args.undirected && u != v {
                            thread_edges.push(Edge { src: v, tgt: u });
                        }
                    }
                }
            }

            thread_edges
        })
        .collect();

    Ok(GeneratedGraph {
        domain_count: 1,
        domain_names: vec![args.domain_name.clone()],
        domain_sizes: vec![num_nodes],
        src_domain_id: 0,
        tgt_domain_id: 0,
        relation_name: args.relation_name.clone(),
        edges,
    })
}

/// Generates an Erdős–Rényi uniform random graph
fn generate_erdos_renyi(args: &GenerateArgs, seed: u64) -> Result<GeneratedGraph, Box<dyn Error>> {
    let num_nodes = args.nodes.unwrap_or(10_000);
    let target_edges = if let Some(m) = args.edges {
        m
    } else if let Some(p) = args.p {
        let possible = (num_nodes * (num_nodes.saturating_sub(1))) as f64;
        (possible * p) as u64
    } else {
        num_nodes * 10
    };

    println!("  Vertices N:      {}", num_nodes);
    println!("  Target Edges M:  {}", target_edges);

    let num_threads = rayon::current_num_threads();
    let chunk = (target_edges as usize).div_ceil(num_threads);

    let edges: Vec<Edge> = (0..num_threads)
        .into_par_iter()
        .flat_map(|thread_id| {
            let mut thread_rng = StdRng::seed_from_u64(
                seed.wrapping_add((thread_id as u64).wrapping_mul(0xC2B2AE35)),
            );
            let start = (thread_id * chunk) as u64;
            let count = if start >= target_edges {
                0
            } else {
                std::cmp::min(chunk as u64, target_edges - start)
            };

            let mut thread_edges =
                Vec::with_capacity(count as usize * if args.undirected { 2 } else { 1 });

            for _ in 0..count {
                let mut u = thread_rng.gen_range(0..num_nodes as u32);
                let mut v = thread_rng.gen_range(0..num_nodes as u32);

                if !args.allow_self_loops && num_nodes > 1 {
                    while u == v {
                        u = thread_rng.gen_range(0..num_nodes as u32);
                        v = thread_rng.gen_range(0..num_nodes as u32);
                    }
                }

                thread_edges.push(Edge { src: u, tgt: v });
                if args.undirected && u != v {
                    thread_edges.push(Edge { src: v, tgt: u });
                }
            }

            thread_edges
        })
        .collect();

    Ok(GeneratedGraph {
        domain_count: 1,
        domain_names: vec![args.domain_name.clone()],
        domain_sizes: vec![num_nodes],
        src_domain_id: 0,
        tgt_domain_id: 0,
        relation_name: args.relation_name.clone(),
        edges,
    })
}

/// Generates a 2D or 3D regular spatial grid/mesh
fn generate_grid(args: &GenerateArgs) -> Result<GeneratedGraph, Box<dyn Error>> {
    let dim_x = args.dim_x;
    let dim_y = args.dim_y;
    let dim_z = args.dim_z.unwrap_or(1);
    let toroidal = args.toroidal;

    let total_nodes = (dim_x as u64) * (dim_y as u64) * (dim_z as u64);
    println!("  Grid Dimensions: {} x {} x {}", dim_x, dim_y, dim_z);
    println!("  Total Vertices:  {}", total_nodes);
    println!("  Toroidal Wrap:   {}", toroidal);

    let mut edges = Vec::new();

    for z in 0..dim_z {
        for y in 0..dim_y {
            for x in 0..dim_x {
                let u = z * (dim_x * dim_y) + y * dim_x + x;

                // +X neighbor
                if x + 1 < dim_x {
                    let v = z * (dim_x * dim_y) + y * dim_x + (x + 1);
                    edges.push(Edge { src: u, tgt: v });
                    if args.undirected {
                        edges.push(Edge { src: v, tgt: u });
                    }
                } else if toroidal && dim_x > 1 {
                    let v = z * (dim_x * dim_y) + y * dim_x;
                    edges.push(Edge { src: u, tgt: v });
                    if args.undirected {
                        edges.push(Edge { src: v, tgt: u });
                    }
                }

                // +Y neighbor
                if y + 1 < dim_y {
                    let v = z * (dim_x * dim_y) + (y + 1) * dim_x + x;
                    edges.push(Edge { src: u, tgt: v });
                    if args.undirected {
                        edges.push(Edge { src: v, tgt: u });
                    }
                } else if toroidal && dim_y > 1 {
                    let v = z * (dim_x * dim_y) + x;
                    edges.push(Edge { src: u, tgt: v });
                    if args.undirected {
                        edges.push(Edge { src: v, tgt: u });
                    }
                }

                // +Z neighbor
                if dim_z > 1 {
                    if z + 1 < dim_z {
                        let v = (z + 1) * (dim_x * dim_y) + y * dim_x + x;
                        edges.push(Edge { src: u, tgt: v });
                        if args.undirected {
                            edges.push(Edge { src: v, tgt: u });
                        }
                    } else if toroidal {
                        let v = y * dim_x + x;
                        edges.push(Edge { src: u, tgt: v });
                        if args.undirected {
                            edges.push(Edge { src: v, tgt: u });
                        }
                    }
                }
            }
        }
    }

    Ok(GeneratedGraph {
        domain_count: 1,
        domain_names: vec![args.domain_name.clone()],
        domain_sizes: vec![total_nodes],
        src_domain_id: 0,
        tgt_domain_id: 0,
        relation_name: args.relation_name.clone(),
        edges,
    })
}

/// Generates a balanced K-ary tree or star topology
fn generate_tree(args: &GenerateArgs) -> Result<GeneratedGraph, Box<dyn Error>> {
    if args.star {
        let num_nodes = args.nodes.unwrap_or(1000);
        println!("  Star Topology:   Center 0 -> Leaves 1..{}", num_nodes - 1);
        let mut edges =
            Vec::with_capacity((num_nodes - 1) as usize * if args.undirected { 2 } else { 1 });
        for v in 1..num_nodes as u32 {
            edges.push(Edge { src: 0, tgt: v });
            if args.undirected {
                edges.push(Edge { src: v, tgt: 0 });
            }
        }
        return Ok(GeneratedGraph {
            domain_count: 1,
            domain_names: vec![args.domain_name.clone()],
            domain_sizes: vec![num_nodes],
            src_domain_id: 0,
            tgt_domain_id: 0,
            relation_name: args.relation_name.clone(),
            edges,
        });
    }

    let k = std::cmp::max(2, args.branching as u64);
    let depth = args.depth;

    // Total nodes = (k^(depth+1) - 1) / (k - 1)
    let mut total_nodes: u64 = 0;
    let mut cur_level = 1u64;
    for _ in 0..=depth {
        total_nodes = total_nodes.saturating_add(cur_level);
        cur_level = cur_level.saturating_mul(k);
    }

    println!("  Tree Branching K: {}", k);
    println!("  Tree Depth D:     {}", depth);
    println!("  Total Vertices:   {}", total_nodes);

    let mut edges = Vec::new();
    let max_parent = (total_nodes - 1) / k;

    for u in 0..max_parent as u32 {
        for child_idx in 1..=k as u32 {
            let v = u * (k as u32) + child_idx;
            if (v as u64) < total_nodes {
                edges.push(Edge { src: u, tgt: v });
                if args.undirected {
                    edges.push(Edge { src: v, tgt: u });
                }
            }
        }
    }

    Ok(GeneratedGraph {
        domain_count: 1,
        domain_names: vec![args.domain_name.clone()],
        domain_sizes: vec![total_nodes],
        src_domain_id: 0,
        tgt_domain_id: 0,
        relation_name: args.relation_name.clone(),
        edges,
    })
}

/// Generates a multi-domain bipartite graph (e.g. Users -> Resources)
fn generate_bipartite(args: &GenerateArgs, seed: u64) -> Result<GeneratedGraph, Box<dyn Error>> {
    let src_nodes = args.src_nodes.unwrap_or(5_000);
    let tgt_nodes = args.tgt_nodes.unwrap_or(5_000);
    let target_edges = args.edges.unwrap_or(src_nodes * 10);

    println!(
        "  Domain 0 (Src):  {} vertices ({})",
        src_nodes, args.domain_name
    );
    println!(
        "  Domain 1 (Tgt):  {} vertices ({})",
        tgt_nodes, args.tgt_domain_name
    );
    println!("  Target Edges:    {}", target_edges);

    let num_threads = rayon::current_num_threads();
    let chunk = (target_edges as usize).div_ceil(num_threads);

    let edges: Vec<Edge> = (0..num_threads)
        .into_par_iter()
        .flat_map(|thread_id| {
            let mut thread_rng = StdRng::seed_from_u64(
                seed.wrapping_add((thread_id as u64).wrapping_mul(0x27D4EB2F)),
            );
            let start = (thread_id * chunk) as u64;
            let count = if start >= target_edges {
                0
            } else {
                std::cmp::min(chunk as u64, target_edges - start)
            };

            let mut thread_edges = Vec::with_capacity(count as usize);

            for _ in 0..count {
                let u = thread_rng.gen_range(0..src_nodes as u32);
                let v = thread_rng.gen_range(0..tgt_nodes as u32);
                thread_edges.push(Edge { src: u, tgt: v });
            }

            thread_edges
        })
        .collect();

    Ok(GeneratedGraph {
        domain_count: 2,
        domain_names: vec![args.domain_name.clone(), args.tgt_domain_name.clone()],
        domain_sizes: vec![src_nodes, tgt_nodes],
        src_domain_id: 0,
        tgt_domain_id: 1,
        relation_name: args.relation_name.clone(),
        edges,
    })
}

/// Constructs a Spec v0.9.0 .imps binary snapshot directly from generated graph
fn write_imps_snapshot(
    graph: &GeneratedGraph,
    output_path: &Path,
    attr_specs: &[AttributeSpec],
    seed: u64,
    args: &GenerateArgs,
) -> Result<(), Box<dyn Error>> {
    let include_csc = args.include_csc;
    println!("  Constructing Spec v0.9.0 binary snapshot...");
    let mut writer = SnapshotWriter::new(output_path.to_str().unwrap_or("snapshot.imps"));

    for (d_id, (name, &size)) in graph
        .domain_names
        .iter()
        .zip(graph.domain_sizes.iter())
        .enumerate()
    {
        writer.add_domain(d_id as u16, KeyType::Int64, name);
        writer.set_domain_node_count(d_id as u16, size);
    }

    let src_node_count = graph.domain_sizes[graph.src_domain_id as usize];
    let tgt_node_count = graph.domain_sizes[graph.tgt_domain_id as usize];

    // Parallel sort of edges by src then tgt for standard CSR layout
    println!("  Sorting {} edges into CSR order...", graph.edges.len());
    let mut sorted_edges = graph.edges.clone();
    sorted_edges.par_sort_unstable_by(|a, b| a.src.cmp(&b.src).then_with(|| a.tgt.cmp(&b.tgt)));

    let mut row_offsets = vec![0u32; (src_node_count + 1) as usize];
    let mut col_indices = Vec::with_capacity(sorted_edges.len());

    for edge in &sorted_edges {
        if (edge.src as u64) < src_node_count && (edge.tgt as u64) < tgt_node_count {
            row_offsets[(edge.src + 1) as usize] += 1;
            col_indices.push(edge.tgt);
        }
    }

    // Cumulative sum for row offsets
    for i in 0..src_node_count as usize {
        row_offsets[i + 1] += row_offsets[i];
    }

    let edge_count = col_indices.len() as u64;

    let emb_dim = if let Some(d) = args.embedding_dim {
        d as usize
    } else if args.vector_index.is_some() {
        64usize
    } else {
        0usize
    };

    let manifold_vecs = if emb_dim > 0 {
        let (_ro, _ci, vecs, _q) =
            crate::commands::bench_vector::generate_manifold_graph_and_queries(
                src_node_count as usize,
                emb_dim,
                args.communities.max(8) as usize,
                1,
                seed.wrapping_add(0xE3B0),
            );
        Some(vecs)
    } else {
        None
    };

    writer.add_relation(
        graph.src_domain_id,
        graph.tgt_domain_id,
        src_node_count,
        edge_count,
        row_offsets,
        col_indices,
    );
    writer.set_relation_name(0, &graph.relation_name);
    if include_csc {
        writer.set_relation_include_csc(0, true);
    }

    if let Some(vecs) = manifold_vecs {
        let mut emb_bytes = Vec::with_capacity(vecs.len() * 4);
        for &v in &vecs {
            emb_bytes.extend_from_slice(&v.to_le_bytes());
        }
        writer.add_attribute_to_domain(
            graph.src_domain_id,
            &args.embedding_attr,
            impulse_graph::spec::BaseDataType::Float32 as u8,
            emb_dim as u32,
            emb_bytes,
            None,
        );
    }

    // Generate synthetic edge attributes aligned with CSR edge order
    let mut rng = StdRng::seed_from_u64(seed.wrapping_add(0xF00D));
    for attr in attr_specs {
        let mut data_bytes = Vec::new();
        match attr.type_code {
            1 => {
                // i32
                for _ in 0..edge_count {
                    let v: i32 = rng.gen_range(0..10);
                    data_bytes.extend_from_slice(&v.to_le_bytes());
                }
            }
            2 => {
                // i64 timestamp
                let base_ts = 1700000000i64;
                for _ in 0..edge_count {
                    let v: i64 = base_ts + rng.gen_range(0..10_000_000);
                    data_bytes.extend_from_slice(&v.to_le_bytes());
                }
            }
            3 => {
                // f32 weight
                for _ in 0..edge_count {
                    let v: f32 = rng.gen_range(0.01..100.0);
                    data_bytes.extend_from_slice(&v.to_le_bytes());
                }
            }
            4 => {
                // f64 weight
                for _ in 0..edge_count {
                    let v: f64 = rng.gen_range(0.01..100.0);
                    data_bytes.extend_from_slice(&v.to_le_bytes());
                }
            }
            _ => {}
        }

        writer.add_attribute_to_relation(
            0,
            &attr.name,
            attr.type_code,
            attr.dimension,
            data_bytes,
            None,
        );
    }

    // Build minimal domain lookup hash index
    for (d_id, &size) in graph.domain_sizes.iter().enumerate() {
        let key_count = std::cmp::max(16, (size as f64 * 1.5) as u64);
        let index_seed = 0x1234567890ABCDEF_u64;

        let mut index_data = Vec::new();
        index_data.extend_from_slice(&key_count.to_le_bytes());
        index_data.extend_from_slice(&index_seed.to_le_bytes());
        index_data.extend_from_slice(&0u32.to_le_bytes()); // empty string table bytes
        index_data.extend_from_slice(&[0u8; 12]); // padding

        writer.add_index(
            d_id as u16,
            0xFFFF,
            0,
            4, // IMP_INDEX_MINIMAL_PERFECT_HASH
            "_domain_index",
            index_data,
        );
    }

    if let Some(vspec_str) = &args.vector_index {
        let vspec = crate::embeddings::VectorIndexSpec::parse(vspec_str)?;
        writer.add_vamana_index(
            graph.src_domain_id,
            &vspec.attr_name,
            &vspec.index_name,
            &vspec.config,
        )?;
    }

    writer.finalize()?;
    Ok(())
}

/// Exports generated graph to delimited text format (TSV or CSV)
fn write_delimited(
    graph: &GeneratedGraph,
    output_path: &Path,
    delimiter: char,
    attr_specs: &[AttributeSpec],
    seed: u64,
) -> Result<(), Box<dyn Error>> {
    println!(
        "  Writing delimited text file to {}...",
        output_path.display()
    );
    let file = File::create(output_path)?;
    let mut writer = BufWriter::new(file);

    // Header
    let mut header = format!("src{}tgt", delimiter);
    for attr in attr_specs {
        header.push(delimiter);
        header.push_str(&attr.name);
    }
    writeln!(writer, "{}", header)?;

    let mut rng = StdRng::seed_from_u64(seed.wrapping_add(0xCAFE));

    for edge in &graph.edges {
        write!(writer, "{}{}{}", edge.src, delimiter, edge.tgt)?;

        for attr in attr_specs {
            write!(writer, "{}", delimiter)?;
            match attr.type_code {
                1 => write!(writer, "{}", rng.gen_range(0..10))?,
                2 => write!(writer, "{}", 1700000000i64 + rng.gen_range(0..10_000_000))?,
                3 => write!(writer, "{:.4}", rng.gen_range(0.01f32..100.0f32))?,
                4 => write!(writer, "{:.6}", rng.gen_range(0.01f64..100.0f64))?,
                _ => write!(writer, "0")?,
            }
        }
        writeln!(writer)?;
    }

    writer.flush()?;
    Ok(())
}
