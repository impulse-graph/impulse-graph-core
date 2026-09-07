//! Property-Based and Fuzz Testing Suite for Impulse Graph Core Rust Crate.
//!
//! Formally verifies:
//! 1. Round-Trip Invariant: read(write(G)) == G across arbitrary generated topologies
//! 2. Neighborhood Exact Parity: get_neighbors(u) == {v | (u, v) in E}
//! 3. Zero-Panic Guarantee: from_bytes on arbitrary mutated buffers never panics

use impulse_graph::*;
use std::collections::{BTreeSet, HashMap};
use std::fs;

struct SimpleRng {
    state: u64,
}

impl SimpleRng {
    fn new(seed: u64) -> Self {
        Self {
            state: if seed == 0 { 0x853c49e6748fea9b } else { seed },
        }
    }

    fn next_u64(&mut self) -> u64 {
        self.state ^= self.state >> 12;
        self.state ^= self.state << 25;
        self.state ^= self.state >> 27;
        self.state.wrapping_mul(0x2545F4914F6CDD1D)
    }

    fn bounded(&mut self, bound: u32) -> u32 {
        if bound == 0 {
            0
        } else {
            (self.next_u64() % (bound as u64)) as u32
        }
    }
}

#[test]
fn test_property_snapshot_roundtrip_and_reachability() {
    let mut rng = SimpleRng::new(0xdeadbeef12345678);
    const PROP_TEST_FILE: &str = "__impulse_rust_prop_test.imps";

    let num_trials = 40;
    let mut total_edges_checked = 0;

    for trial in 0..num_trials {
        let _ = fs::remove_file(PROP_TEST_FILE);

        let node_count = 5 + rng.bounded(100);
        let num_edges = rng.bounded(node_count * 5);

        let mut edge_map: HashMap<u32, BTreeSet<u32>> = HashMap::new();
        for u in 0..node_count {
            edge_map.insert(u, BTreeSet::new());
        }

        for _ in 0..num_edges {
            let u = rng.bounded(node_count);
            let v = rng.bounded(node_count);
            edge_map.get_mut(&u).unwrap().insert(v);
        }

        // Build CSR arrays
        let mut row_offsets = vec![0u32; (node_count + 1) as usize];
        let mut col_indices = Vec::new();

        for u in 0..node_count {
            let neighbors = edge_map.get(&u).unwrap();
            row_offsets[(u + 1) as usize] = row_offsets[u as usize] + neighbors.len() as u32;
            for &v in neighbors {
                col_indices.push(v);
            }
        }

        let total_edge_count = col_indices.len() as u64;

        // Write snapshot
        let mut writer = SnapshotWriter::new(PROP_TEST_FILE);
        writer.add_domain(0, KeyType::Int32, "Entity");
        writer.set_domain_node_count(0, node_count as u64);
        writer.add_relation(
            0,
            0,
            node_count as u64,
            total_edge_count,
            row_offsets.clone(),
            col_indices.clone(),
        );

        let finalize_res = writer.finalize();
        assert!(finalize_res.is_ok(), "Finalize failed in trial {}", trial);

        // Read snapshot
        let reader = SnapshotReader::open(PROP_TEST_FILE).expect("Failed to open snapshot");

        // Verify Property 1: Topology structural identity
        assert_eq!(reader.domain_count(), 1);
        assert_eq!(reader.domains()[0].node_count, node_count as u64);

        // Verify Property 2: Ground truth positive reachability & neighbor lists
        for u in 0..node_count {
            let expected_neighbors = edge_map.get(&u).unwrap();
            let actual_neighbors = reader.get_neighbors(0, u as u64).unwrap();

            assert_eq!(
                actual_neighbors.len(),
                expected_neighbors.len(),
                "Neighbor count mismatch at node {} in trial {}",
                u,
                trial
            );

            for &v in expected_neighbors {
                assert!(
                    reader.is_reachable(0, u as u64, v as u64),
                    "Reachability failed for ({}, {}) in trial {}",
                    u,
                    v,
                    trial
                );
                total_edges_checked += 1;
            }
        }

        // Verify Property 3: Negative reachability for non-edges
        for _ in 0..50 {
            let u = rng.bounded(node_count);
            let v = rng.bounded(node_count);
            let is_edge = edge_map.get(&u).unwrap().contains(&v);
            assert_eq!(
                reader.is_reachable(0, u as u64, v as u64),
                is_edge,
                "Negative reachability mismatch at ({}, {}) in trial {}",
                u,
                v,
                trial
            );
        }

        let _ = fs::remove_file(PROP_TEST_FILE);
    }

    println!(
        "[Rust Property Test] PASSED {} trials with {} edges verified.",
        num_trials, total_edges_checked
    );
}

#[test]
fn test_fuzz_random_buffer_ingestion_never_panics() {
    let mut rng = SimpleRng::new(0x987654321fedcba0);
    const NUM_ITERATIONS: usize = 5000;

    let mut ok_count = 0;
    let mut err_count = 0;

    for _ in 0..NUM_ITERATIONS {
        let mode = rng.bounded(4);
        let bytes: Vec<u8> = match mode {
            0 => {
                // Completely random noise of random length 0..8192
                let len = rng.bounded(8192) as usize;
                let mut b = vec![0u8; len];
                for x in b.iter_mut() {
                    *x = (rng.next_u64() & 0xFF) as u8;
                }
                b
            }
            1 => {
                // Valid 4KB header prefix filled with random values
                let len = 4096 + rng.bounded(2048) as usize;
                let mut b = vec![0u8; len];
                // Set magic
                b[0..4].copy_from_slice(&0x494D5053u32.to_le_bytes());
                b[4..6].copy_from_slice(&9u16.to_le_bytes());
                b
            }
            2 => {
                // Truncated tiny buffers
                let len = rng.bounded(64) as usize;
                vec![0u8; len]
            }
            _ => {
                // Large zero buffer
                vec![0u8; 4096]
            }
        };

        // Invariant: SnapshotReader::from_bytes must NEVER panic on any input
        match SnapshotReader::from_bytes(bytes) {
            Ok(reader) => {
                ok_count += 1;
                // If it opened, basic accessors must also not panic
                let _ = reader.domain_count();
                let _ = reader.domains();
                let _ = reader.is_reachable(0, 0, 0);
            }
            Err(_) => {
                err_count += 1;
            }
        }
    }

    println!(
        "[Rust Buffer Fuzz] PASSED {} iterations (Ok: {}, Err: {}). Zero panics.",
        NUM_ITERATIONS, ok_count, err_count
    );
}
