//! Impulse Graph Engine — Example 01: Snapshot Creation & Zero-Copy Reading (Rust)
//!
//! Demonstrates:
//! 1. Programmatic creation of an immutable binary snapshot (.imps) using SnapshotWriter.
//! 2. Opening the snapshot via zero-copy OS memory mapping with SnapshotReader.
//! 3. Inspecting domains, relations, degree arrays, and point reachability.

use impulse_graph::{KeyType, SnapshotReader, SnapshotWriter};
use std::fs;
use std::path::Path;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("===============================================================");
    println!(" Impulse Graph Engine — Example 01: Snapshot Basics (Rust)");
    println!("===============================================================\n");

    let snapshot_path = "sample_basics.imps";

    // ------------------------------------------------------------------------
    // Step 1: Programmatic Snapshot Creation with SnapshotWriter
    // ------------------------------------------------------------------------
    println!("1. Creating binary snapshot file: {}...", snapshot_path);

    let mut writer = SnapshotWriter::new(snapshot_path);
    writer.add_domain(0, KeyType::Int64, "User");

    // CSR Topology: 4 Users (0, 1, 2, 3)
    // Node 0 -> [1, 2]
    // Node 1 -> [2, 3]
    // Node 2 -> [3]
    // Node 3 -> []
    let row_offsets = vec![0u32, 2, 4, 5, 5];
    let col_indices = vec![1u32, 2, 2, 3, 3];

    writer.add_relation(
        /*src_domain_id=*/ 0,
        /*tgt_domain_id=*/ 0,
        /*node_count=*/ 4,
        /*edge_count=*/ 5,
        row_offsets,
        col_indices,
    );

    writer.finalize()?;
    let file_size = fs::metadata(snapshot_path)?.len();
    println!("   -> Successfully wrote snapshot ({} bytes).\n", file_size);

    // ------------------------------------------------------------------------
    // Step 2: Zero-Copy Memory-Mapped Reading with SnapshotReader
    // ------------------------------------------------------------------------
    println!("2. Opening snapshot via zero-copy memory mapping...");
    let reader = SnapshotReader::open(snapshot_path)?;

    println!("   -> Magic:     0x{:X} ('IMPS')", reader.header().magic());
    println!("   -> Version:   {}", reader.header().version());
    println!("   -> Domains:   {}", reader.domain_count());
    println!("   -> Relations: {}\n", reader.relation_count());

    // ------------------------------------------------------------------------
    // Step 3: Inspect Relation Directory & Slices
    // ------------------------------------------------------------------------
    println!("3. Inspecting relation 0 topology:");
    if let Some(rel) = reader.get_relation_entry(0) {
        println!("   -> Node Count: {}", rel.node_count);
        println!("   -> Edge Count: {}", rel.edge_count);
    }

    let offsets = reader.get_row_offsets(0)?;
    let targets = reader.get_col_indices(0)?;

    for node in 0..4 {
        let start = offsets[node] as usize;
        let end = offsets[node + 1] as usize;
        let node_targets = &targets[start..end];
        println!(
            "   -> Node {} out-degree: {} edges (neighbors: {:?})",
            node,
            node_targets.len(),
            node_targets
        );
    }

    println!("\n4. Direct Point Reachability Queries:");
    println!("   -> Node 0 -> Node 1 reachable? {}", reader.is_reachable(0, 0, 1));
    println!("   -> Node 0 -> Node 3 reachable? {}", reader.is_reachable(0, 0, 3));

    // Cleanup
    if Path::new(snapshot_path).exists() {
        fs::remove_file(snapshot_path)?;
    }

    println!("\n[SUCCESS] Example 01 completed cleanly.");
    Ok(())
}
