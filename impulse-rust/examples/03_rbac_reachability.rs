//! Impulse Graph Engine — Example 03: Relationship-Based Access Control (ReBAC) (Rust)
//!
//! Demonstrates:
//! 1. Resolving rbac_snapshot.imps via embedded engine path resolution (IMPULSE_DATASETS_DIR).
//! 2. Multi-domain authorization query:
//!    User -> assigned_role -> Role -> role_perm -> Permission
//! 3. Evaluating effective permissions for a user via ImpulseVM C-ABI query evaluation.

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
    println!(" Impulse Graph Engine — Example 03: ReBAC Authorization (Rust/VM)");
    println!("===============================================================\n");

    let snapshot_path = "rbac_snapshot.imps";
    let is_temp;
    let temp_path = "temp_rbac_snapshot.imps";

    let active_path = if Path::new(snapshot_path).exists() {
        println!(
            "[INFO] Successfully resolved and opened '{}'.",
            snapshot_path
        );
        is_temp = false;
        snapshot_path
    } else {
        println!("[INFO] '{}' not found in local paths.", snapshot_path);
        println!("[INFO] Generating fallback ReBAC snapshot...");
        is_temp = true;

        let mut writer = SnapshotWriter::new(temp_path);
        writer.add_domain(0, KeyType::String, "User");
        writer.add_domain(1, KeyType::String, "Role");
        writer.add_domain(2, KeyType::String, "Permission");

        // Relation 0: User -> Role (User 0 is Admin(0) and Editor(1))
        writer.add_relation(0, 1, 3, 4, vec![0, 2, 3, 4], vec![0, 1, 1, 2]);

        // Relation 1: Role -> Permission (Admin(0) -> [Read(0), Write(1), Delete(2)])
        writer.add_relation(1, 2, 3, 5, vec![0, 3, 4, 5], vec![0, 1, 2, 0, 0]);

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
    // Step 1: ReBAC Multi-Hop Permission Traversal via ImpulseVM
    // ------------------------------------------------------------------------
    println!("\n1. ReBAC Policy: Check permissions for User 0 (User -> Role -> Permission):");

    let query_text = CString::new("(walk (walk (seed 0) (rel 0)) (rel 1))")?;
    let mut result: impulse_execution_result_t = unsafe { std::mem::zeroed() };

    let t0 = Instant::now();
    let rc = unsafe { impulse_exec(snap, query_text.as_ptr(), 0, &mut result) };
    let elapsed = t0.elapsed();

    println!("   -> ReBAC Evaluation Latency: {:.2?}", elapsed);
    println!(
        "   -> ImpulseVM Status: {}, Row Count: {}",
        rc, result.row_count
    );

    // ------------------------------------------------------------------------
    // Step 2: Zero-Copy Reader Topological Inspection
    // ------------------------------------------------------------------------
    let reader = SnapshotReader::open(active_path)?;
    println!("\n2. Direct Zero-Copy Role Inspection for User 0:");
    let user0_roles = reader.get_neighbors(0, 0)?;
    println!("   -> User 0 assigned roles: {:?}", user0_roles);

    unsafe {
        impulse_snapshot_close(snap);
    }

    if is_temp && Path::new(temp_path).exists() {
        fs::remove_file(temp_path)?;
    }

    println!("\n[SUCCESS] Example 03 completed cleanly.");
    Ok(())
}
