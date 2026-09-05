//! Impulse Graph Engine — Example 04: Structure-of-Arrays (SoA) Attributes & SIMD Math (Rust)
//!
//! Demonstrates:
//! 1. Writing and reading Structure-of-Arrays (SoA) edge attributes (float weights, timestamps).
//! 2. Zero-copy off-heap slice inspection of columnar attribute sections.
//! 3. High-throughput SIMD vector operations (Highway dot products, sorted set intersections).

use impulse_graph::simd;
use impulse_graph::{BaseDataType, KeyType, SnapshotReader, SnapshotWriter};
use std::fs;
use std::path::Path;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("===================================================================");
    println!(" Impulse Graph Engine — Example 04: SoA Attributes & SIMD (Rust)");
    println!("===================================================================\n");

    let snapshot_path = "sample_financial_vectors.imps";

    // ------------------------------------------------------------------------
    // Step 1: Create Snapshot with Columnar Edge Attributes
    // ------------------------------------------------------------------------
    println!("1. Creating snapshot with SoA columnar edge attributes...");

    let mut writer = SnapshotWriter::new(snapshot_path);
    writer.add_domain(0, KeyType::Int64, "Account");

    // 5 Accounts, 6 Transfer Edges
    let row_offsets = vec![0u32, 3, 4, 6, 6, 6];
    let col_indices = vec![1u32, 2, 3, 3, 0, 4];
    writer.add_relation(0, 0, 5, 6, row_offsets, col_indices);

    // Columnar Attribute 0: "amount" (Float32)
    let amounts: Vec<f32> = vec![12500.0, 4200.0, 8900.0, 310.0, 55000.0, 1200.0];
    let mut amount_bytes = Vec::new();
    for &val in &amounts {
        amount_bytes.extend_from_slice(&val.to_ne_bytes());
    }

    writer.add_attribute_to_relation(
        /*relation_id=*/ 0,
        /*name=*/ "amount",
        /*type_code=*/ BaseDataType::Float32 as u8,
        /*dimension=*/ 1,
        /*data_bytes=*/ amount_bytes,
        /*offsets=*/ None,
    );

    writer.finalize()?;
    println!("   -> Successfully finalized snapshot with SoA attributes.\n");

    // ------------------------------------------------------------------------
    // Step 2: Zero-Copy Topology & Attribute Inspection
    // ------------------------------------------------------------------------
    println!("2. Inspecting Zero-Copy Attribute Offsets & Topology:");
    let reader = SnapshotReader::open(snapshot_path)?;

    if let Some(rel) = reader.get_relation_entry(0) {
        println!(
            "   -> Relation: {} (Nodes: {}, Edges: {})",
            rel.name, rel.node_count, rel.edge_count
        );
        for attr in &rel.attributes {
            println!(
                "   -> Attribute Column: '{}' (Type: 0x{:02X}, Data Bytes: {})",
                attr.name, attr.type_code, attr.data_bytes
            );
        }
    }

    // ------------------------------------------------------------------------
    // Step 3: Hardware-Accelerated Dynamic SIMD Operations
    // ------------------------------------------------------------------------
    println!("\n3. Dynamic Hardware SIMD Vector Acceleration:");
    println!(
        "   -> Active Highway SIMD Target: {}",
        simd::current_target_name()
    );

    // Vector Dot Product
    let vec_a = vec![1.0f32, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0];
    let vec_b = vec![0.5f32, 0.5, 0.5, 0.5, 2.0, 2.0, 2.0, 2.0];
    let dot = simd::dot_product_f32(&vec_a, &vec_b);
    println!("   -> SIMD Dot Product (vec_a · vec_b): {}", dot);

    // Vector Addition
    let sum = simd::vector_sum_f32(&vec_a, &vec_b);
    println!("   -> SIMD Vector Sum (vec_a + vec_b): {:?}", sum);

    // Sorted Set Intersection (SIMD Fast-Path for Triangle Counting & Graph Clustering)
    let set1 = vec![1u32, 5, 10, 15, 20, 25, 30, 35, 40];
    let set2 = vec![5u32, 12, 15, 25, 35, 50];
    let common = simd::intersect_sorted_u32(&set1, &set2);
    println!("   -> SIMD Sorted Set Intersection: {:?}", common);

    // Cleanup
    if Path::new(snapshot_path).exists() {
        fs::remove_file(snapshot_path)?;
    }

    println!("\n[SUCCESS] Example 04 completed cleanly.");
    Ok(())
}
