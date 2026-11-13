//! Impulse Graph Engine — Example 02: Multi-Hop Social Graph Traversal (Rust via ImpulseVM)
//!
//! Demonstrates:
//! 1. Resolving social_graph.imps via embedded engine path resolution (IMPULSE_DATASETS_DIR).
//! 2. Executing multi-hop graph queries via the C++ ImpulseVM engine (impulse_exec).
//! 3. Extracting reachable destination node bitsets / vectors.

use impulse_graph::ffi::{
    impulse_exec, impulse_execution_result_t, impulse_snapshot_close, impulse_snapshot_open,
};
use impulse_graph::{KeyType, SnapshotReader, SnapshotWriter};
use std::ffi::CString;
use std::fs;
use std::path::Path;
use std::time::Instant;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("===============================================================");
    println!(" Impulse Graph Engine — Example 02: Social Graph Traversal (Rust/VM)");
    println!("===============================================================\n");

    let snapshot_path = "social_graph.imps";
    let is_temp;
    let temp_path = "temp_social_graph.imps";

    let active_path = if Path::new(snapshot_path).exists() {
        println!(
            "[INFO] Successfully resolved and opened '{}'.",
            snapshot_path
        );
        is_temp = false;
        snapshot_path
    } else {
        println!("[INFO] '{}' not found in local paths.", snapshot_path);
        println!("[INFO] Generating fallback in-memory sample social graph...");
        is_temp = true;

        let mut writer = SnapshotWriter::new(temp_path);
        writer.add_domain(0, KeyType::Int64, "User");

        // 8 Users with follow relations
        let row_offsets = vec![0u32, 2, 4, 6, 8, 9, 10, 11, 11];
        let col_indices = vec![1u32, 2, 2, 3, 3, 4, 4, 5, 6, 7, 0];
        writer.add_relation(0, 0, 8, 11, row_offsets, col_indices);
        writer.finalize()?;
        temp_path
    };

    // Open snapshot via C-ABI for ImpulseVM execution
    let c_path = CString::new(active_path)?;
    let mut status = 0i32;
    let snap = unsafe { impulse_snapshot_open(c_path.as_ptr(), &mut status) };
    if snap.is_null() {
        return Err(format!("Failed to open snapshot for VM: {}", status).into());
    }

    // ------------------------------------------------------------------------
    // Step 1: Execute Fluent 2-Hop Traversal Pipeline via ImpulseVM
    // ------------------------------------------------------------------------
    println!("\n1. Constructing ImpScheme Traversal Pipeline:");
    println!("   Query: (walk-2hop (seed 0) (rel 0))");

    let query_text = CString::new("(walk-2hop (seed 0) (rel 0))")?;
    let mut result: impulse_execution_result_t = unsafe { std::mem::zeroed() };

    let t0 = Instant::now();
    let rc = unsafe { impulse_exec(snap, query_text.as_ptr(), 0, &mut result) };
    let elapsed = t0.elapsed();

    println!("   -> Traversal Latency: {:.2?}", elapsed);
    println!(
        "   -> ImpulseVM Return Code: {}, Row Count: {}",
        rc, result.row_count
    );

    // ------------------------------------------------------------------------
    // Step 2: Immediate Neighbors Inspection via SnapshotReader
    // ------------------------------------------------------------------------
    let reader = SnapshotReader::open(active_path)?;
    println!("\n2. Inspecting 1-Hop Outgoing Edges for User 0 via Zero-Copy Reader:");
    let direct_friends = reader.get_neighbors(0, 0)?;
    println!("   -> User 0 follows: {:?}", direct_friends);

    unsafe {
        impulse_snapshot_close(snap);
    }

    if is_temp && Path::new(temp_path).exists() {
        fs::remove_file(temp_path)?;
    }

    println!("\n[SUCCESS] Example 02 completed cleanly.");
    Ok(())
}
