//! Impulse Graph Engine — Example 02: Multi-Hop Social Graph Traversal (Rust)
//!
//! Demonstrates:
//! 1. Resolving social_graph.imps via embedded engine path resolution (IMPULSE_DATASETS_DIR).
//! 2. Constructing multi-hop fluent traversal pipelines (out / in_step).
//! 3. Extracting reachable destination node vectors.

use impulse_graph::{KeyType, SnapshotReader, SnapshotWriter};
use std::fs;
use std::path::Path;
use std::time::Instant;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("===============================================================");
    println!(" Impulse Graph Engine — Example 02: Social Graph Traversal (Rust)");
    println!("===============================================================\n");

    let snapshot_path = "social_graph.imps";
    let is_temp;
    let temp_path = "temp_social_graph.imps";

    let reader = match SnapshotReader::open(snapshot_path) {
        Ok(r) => {
            println!(
                "[INFO] Successfully resolved and opened '{}'.",
                snapshot_path
            );
            is_temp = false;
            r
        }
        Err(_) => {
            println!(
                "[INFO] '{}' not found in $IMPULSE_DATASETS_DIR or local paths.",
                snapshot_path
            );
            println!("[INFO] Generating fallback in-memory sample social graph...");
            is_temp = true;

            let mut writer = SnapshotWriter::new(temp_path);
            writer.add_domain(0, KeyType::Int64, "User");

            // 8 Users with follow relations
            let row_offsets = vec![0u32, 2, 4, 6, 8, 9, 10, 11, 11];
            let col_indices = vec![1u32, 2, 2, 3, 3, 4, 4, 5, 6, 7, 0];
            writer.add_relation(0, 0, 8, 11, row_offsets, col_indices);
            writer.finalize()?;

            SnapshotReader::open(temp_path)?
        }
    };

    // ------------------------------------------------------------------------
    // Step 1: Execute Fluent 2-Hop Traversal Pipeline
    // ------------------------------------------------------------------------
    println!("\n1. Constructing Fluent Traversal Pipeline:");
    println!("   Query: Seed(User 0) -> out(0) -> out(0)");

    let t0 = Instant::now();
    let hop2_friends = reader.traverse(0).out("0").out("0").to_vec()?;
    let elapsed = t0.elapsed();

    println!("   -> Traversal Latency: {:.2?}", elapsed);
    println!(
        "   -> 2-Hop Friends-of-Friends from User 0: {:?}",
        hop2_friends
    );

    // ------------------------------------------------------------------------
    // Step 2: Immediate Neighbors Inspection
    // ------------------------------------------------------------------------
    println!("\n2. Inspecting 1-Hop Outgoing & Incoming Edges for User 0:");
    let direct_friends = reader.traverse(0).out("0").to_vec()?;
    println!("   -> User 0 follows: {:?}", direct_friends);

    let followers = reader.traverse(0).in_step("0").to_vec()?;
    println!("   -> Users who follow User 0: {:?}", followers);

    if is_temp && Path::new(temp_path).exists() {
        fs::remove_file(temp_path)?;
    }

    println!("\n[SUCCESS] Example 02 completed cleanly.");
    Ok(())
}
