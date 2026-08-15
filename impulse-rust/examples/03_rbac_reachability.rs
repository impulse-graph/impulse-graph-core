//! Impulse Graph Engine — Example 03: Relationship-Based Access Control (ReBAC) (Rust)
//!
//! Demonstrates:
//! 1. Resolving rbac_snapshot.imps via embedded engine path resolution (IMPULSE_DATASETS_DIR).
//! 2. Multi-domain authorization query:
//!    User -> assigned_role -> Role -> role_perm -> Permission
//! 3. Evaluating effective permissions for a user via typed graph traversal.

use impulse_graph::{KeyType, SnapshotReader, SnapshotWriter};
use std::fs;
use std::path::Path;
use std::time::Instant;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("===============================================================");
    println!(" Impulse Graph Engine — Example 03: ReBAC Authorization (Rust)");
    println!("===============================================================\n");

    let snapshot_path = "rbac_snapshot.imps";
    let is_temp;
    let temp_path = "temp_rbac_snapshot.imps";

    let reader = match SnapshotReader::open(snapshot_path) {
        Ok(r) => {
            println!("[INFO] Successfully resolved and opened '{}'.", snapshot_path);
            is_temp = false;
            r
        }
        Err(_) => {
            println!("[INFO] '{}' not found in $IMPULSE_DATASETS_DIR or local paths.", snapshot_path);
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
            SnapshotReader::open(temp_path)?
        }
    };

    // ------------------------------------------------------------------------
    // Step 1: ReBAC Multi-Hop Permission Traversal
    // ------------------------------------------------------------------------
    println!("\n1. ReBAC Policy: Check permissions for User 0 (User -> Role -> Permission):");

    let t0 = Instant::now();
    let effective_permissions = reader
        .traverse(0)
        .out("0") // Walk User -> Role
        .out("1") // Walk Role -> Permission
        .to_vec()?;
    let elapsed = t0.elapsed();

    println!("   -> ReBAC Evaluation Latency: {:.2?}", elapsed);
    println!("   -> Reached Permission IDs: {:?}", effective_permissions);

    // ------------------------------------------------------------------------
    // Step 2: Policy Decision Evaluation
    // ------------------------------------------------------------------------
    println!("\n2. Effective Permissions Evaluation for User 0:");
    let perm_labels = [("READ", 0), ("WRITE", 1), ("DELETE", 2)];
    for (name, id) in perm_labels {
        let allowed = effective_permissions.contains(&id);
        let status_str = if allowed { "ALLOWED [✓]" } else { "DENIED  [✗]" };
        println!("   -> Permission {}: {}", name, status_str);
    }

    if is_temp && Path::new(temp_path).exists() {
        fs::remove_file(temp_path)?;
    }

    println!("\n[SUCCESS] Example 03 completed cleanly.");
    Ok(())
}
