use impulse_graph_tooling::commands;
use impulse_graph_tooling::GenerateArgs;
use std::fs;
use std::path::{Path, PathBuf};

fn setup_test_workspace(sub: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!(
        "impulse_roundtrip_test_{}_{}",
        sub,
        rand::random::<u32>()
    ));
    let _ = fs::create_dir_all(&dir);
    dir
}

fn assert_dirs_equal(dir1: &Path, dir2: &Path) {
    let mut files1: Vec<PathBuf> = Vec::new();
    for entry in walkdir(dir1) {
        if entry.is_file() {
            files1.push(entry.strip_prefix(dir1).unwrap().to_path_buf());
        }
    }
    files1.sort();

    let mut files2: Vec<PathBuf> = Vec::new();
    for entry in walkdir(dir2) {
        if entry.is_file() {
            files2.push(entry.strip_prefix(dir2).unwrap().to_path_buf());
        }
    }
    files2.sort();

    assert_eq!(files1, files2, "Directory file lists differ");

    for rel_path in &files1 {
        let p1 = dir1.join(rel_path);
        let p2 = dir2.join(rel_path);
        let b1 = fs::read(&p1).unwrap_or_else(|e| panic!("Failed to read {:?}: {}", p1, e));
        let b2 = fs::read(&p2).unwrap_or_else(|e| panic!("Failed to read {:?}: {}", p2, e));
        if b1 != b2 {
            let s1 = String::from_utf8_lossy(&b1);
            let s2 = String::from_utf8_lossy(&b2);
            panic!(
                "File {:?} differed between export 1 and export 2!\n--- Exp1:\n{}\n--- Exp2:\n{}",
                rel_path, s1, s2
            );
        }
    }
}

fn walkdir(dir: &Path) -> Vec<PathBuf> {
    let mut out = Vec::new();
    if let Ok(entries) = fs::read_dir(dir) {
        for entry in entries.flatten() {
            let p = entry.path();
            if p.is_dir() {
                out.extend(walkdir(&p));
            } else if p.is_file() {
                out.push(p);
            }
        }
    }
    out
}

#[test]
fn test_roundtrip_idempotency_erdos_renyi_csv() {
    let test_dir = setup_test_workspace("er_csv");
    let orig_imps = test_dir.join("orig.imps");
    let exp1_dir = test_dir.join("export_1");
    let rebuilt_imps = test_dir.join("rebuilt.imps");
    let exp2_dir = test_dir.join("export_2");

    // 1. Generate snapshot
    let args = GenerateArgs {
        profile: "erdos_renyi".to_string(),
        output: orig_imps.clone(),
        format: "imps".to_string(),
        scale: 6,
        edge_factor: 16,
        a: 0.57,
        b: 0.19,
        c: 0.19,
        d: 0.05,
        nodes: Some(60),
        edges: Some(150),
        edges_per_node: 4,
        p: Some(0.08),
        communities: 4,
        p_intra: 0.05,
        p_inter: 0.005,
        dim_x: 10,
        dim_y: 10,
        dim_z: None,
        toroidal: false,
        branching: 2,
        depth: 4,
        star: false,
        src_nodes: None,
        tgt_nodes: None,
        seed: Some(42),
        undirected: false,
        allow_self_loops: false,
        include_csc: true,
        domain_name: "Node".to_string(),
        tgt_domain_name: "Target".to_string(),
        relation_name: "CONNECTS".to_string(),
        attributes: None,
        chunk_size: 1_000_000,
        embedding_dim: None,
        embedding_attr: "embedding".to_string(),
        vector_index: None,
    };
    commands::generate::run(&args).expect("generate failed");

    // 2. Export 1 (CSV)
    commands::export::run(&orig_imps, &exp1_dir, "csv").expect("export 1 failed");

    // 3. Rebuild
    let manifest1 = exp1_dir.join("manifest.json");
    commands::compile::run(&manifest1, &rebuilt_imps).expect("compile rebuilt failed");

    // 4. Validate rebuilt snapshot
    let val_opts = commands::validate::ValidateOptions {
        strict_alignment: true,
        deep_audit: true,
        public_key: None,
        quick_check: false,
    };
    commands::validate::run_with_options(&rebuilt_imps, &val_opts)
        .expect("validate rebuilt failed");

    // 5. Export 2 (CSV)
    commands::export::run(&rebuilt_imps, &exp2_dir, "csv").expect("export 2 failed");

    // 6. Assert directories are 100% bitwise identical
    assert_dirs_equal(&exp1_dir, &exp2_dir);

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_roundtrip_idempotency_bipartite_parquet() {
    let test_dir = setup_test_workspace("bipartite_parquet");
    let orig_imps = test_dir.join("orig.imps");
    let exp1_dir = test_dir.join("export_1");
    let rebuilt_imps = test_dir.join("rebuilt.imps");
    let exp2_dir = test_dir.join("export_2");

    // 1. Generate bipartite snapshot
    let args = GenerateArgs {
        profile: "bipartite".to_string(),
        output: orig_imps.clone(),
        format: "imps".to_string(),
        scale: 5,
        edge_factor: 16,
        a: 0.57,
        b: 0.19,
        c: 0.19,
        d: 0.05,
        nodes: None,
        edges: Some(120),
        edges_per_node: 4,
        p: Some(0.05),
        communities: 4,
        p_intra: 0.05,
        p_inter: 0.005,
        dim_x: 10,
        dim_y: 10,
        dim_z: None,
        toroidal: false,
        branching: 2,
        depth: 4,
        star: false,
        src_nodes: Some(40),
        tgt_nodes: Some(30),
        seed: Some(123),
        undirected: false,
        allow_self_loops: false,
        include_csc: true,
        domain_name: "Customer".to_string(),
        tgt_domain_name: "Order".to_string(),
        relation_name: "PLACED".to_string(),
        attributes: None,
        chunk_size: 1_000_000,
        embedding_dim: None,
        embedding_attr: "embedding".to_string(),
        vector_index: None,
    };
    commands::generate::run(&args).expect("generate failed");

    // 2. Export 1 (Parquet)
    commands::export::run(&orig_imps, &exp1_dir, "parquet").expect("export 1 failed");

    // 3. Rebuild
    let manifest1 = exp1_dir.join("manifest.json");
    commands::compile::run(&manifest1, &rebuilt_imps).expect("compile rebuilt failed");

    // 4. Validate rebuilt snapshot
    let val_opts = commands::validate::ValidateOptions {
        strict_alignment: true,
        deep_audit: true,
        public_key: None,
        quick_check: false,
    };
    commands::validate::run_with_options(&rebuilt_imps, &val_opts)
        .expect("validate rebuilt failed");

    // 5. Export 2 (Parquet)
    commands::export::run(&rebuilt_imps, &exp2_dir, "parquet").expect("export 2 failed");

    // 6. Assert manifest.json files are identical
    let m1 = fs::read_to_string(exp1_dir.join("manifest.json")).unwrap();
    let m2 = fs::read_to_string(exp2_dir.join("manifest.json")).unwrap();
    assert_eq!(m1, m2);

    let _ = fs::remove_dir_all(&test_dir);
}
