use impulse_graph_tooling::commands;
use impulse_graph_tooling::compiler::{self, LanguageTarget};
use std::env;
use std::fs;
use std::path::Path;

#[test]
fn test_unified_benchmark_snapshot_and_multi_script_execution() {
    let temp_dir = env::temp_dir().join("impulse_unified_test");
    let _ = fs::create_dir_all(&temp_dir);

    let snapshot_path = temp_dir.join("unified_benchmark.imps");
    let manifest_path = Path::new("tests/fixtures/unified_benchmark_graph.json");
    assert!(
        manifest_path.exists(),
        "Unified benchmark graph manifest missing at tests/fixtures/unified_benchmark_graph.json"
    );

    // 1. Build single unified multi-domain multi-relation binary snapshot (.imps Spec v0.9.0)
    let build_res = commands::compile::run(manifest_path, &snapshot_path);
    assert!(
        build_res.is_ok(),
        "Failed to build unified .imps snapshot: {:?}",
        build_res
    );
    assert!(
        snapshot_path.exists(),
        "Unified .imps snapshot file was not created"
    );

    // 2. Validate unified snapshot header and alignment
    let validate_res = commands::validate::run(&snapshot_path, true, None);
    assert!(
        validate_res.is_ok(),
        "Failed to validate unified snapshot: {:?}",
        validate_res
    );

    // 3. Test ReBAC ImpLog Script against Unified Snapshot
    let implog_source = fs::read_to_string("examples/cugraph_benchmarks/authz_rebac.implog")
        .expect("Failed to read examples/cugraph_benchmarks/authz_rebac.implog");
    let rebac_asm = compiler::compile_script_to_impas(&implog_source, LanguageTarget::ImpLog)
        .expect("Failed to compile ImpLog");
    println!("Compiled ReBAC Assembly:\n{}", rebac_asm);

    let rebac_impas_path = temp_dir.join("rebac.impas");
    let rebac_impb_path = temp_dir.join("rebac.impb");
    fs::write(&rebac_impas_path, &rebac_asm).unwrap();

    let assemble_res = commands::assemble::run(&rebac_impas_path, &rebac_impb_path);
    assert!(
        assemble_res.is_ok(),
        "Failed to assemble rebac bytecode: {:?}",
        assemble_res.err()
    );

    let run_rebac = commands::run::run(&snapshot_path, &rebac_impb_path, 0);
    assert!(
        run_rebac.is_ok(),
        "Failed to execute ReBAC query against unified snapshot"
    );

    // 4. Test ImpK Triangles Script against Unified Snapshot
    let impk_triangles = fs::read_to_string("examples/cugraph_benchmarks/triangles.impk")
        .expect("Failed to read triangles.impk");
    let tri_asm = compiler::compile_script_to_impas(&impk_triangles, LanguageTarget::ImpK)
        .expect("Failed to compile ImpK triangles");

    let tri_impas_path = temp_dir.join("triangles.impas");
    let tri_impb_path = temp_dir.join("triangles.impb");
    fs::write(&tri_impas_path, &tri_asm).unwrap();

    let assemble_tri = commands::assemble::run(&tri_impas_path, &tri_impb_path);
    assert!(
        assemble_tri.is_ok(),
        "Failed to assemble triangles bytecode"
    );

    let run_tri = commands::run::run(&snapshot_path, &tri_impb_path, 0);
    assert!(
        run_tri.is_ok(),
        "Failed to execute Triangles query against unified snapshot"
    );

    // 5. Test ImpK BFS Script against Unified Snapshot
    let impk_bfs = fs::read_to_string("examples/cugraph_benchmarks/bfs.impk")
        .expect("Failed to read bfs.impk");
    let bfs_asm = compiler::compile_script_to_impas(&impk_bfs, LanguageTarget::ImpK)
        .expect("Failed to compile ImpK BFS");

    let bfs_impas_path = temp_dir.join("bfs.impas");
    let bfs_impb_path = temp_dir.join("bfs.impb");
    fs::write(&bfs_impas_path, &bfs_asm).unwrap();

    let assemble_bfs = commands::assemble::run(&bfs_impas_path, &bfs_impb_path);
    assert!(assemble_bfs.is_ok(), "Failed to assemble BFS bytecode");

    let run_bfs = commands::run::run(&snapshot_path, &bfs_impb_path, 0);
    assert!(
        run_bfs.is_ok(),
        "Failed to execute BFS query against unified snapshot"
    );

    // 6. Test ImpK PageRank Script against Unified Snapshot
    let impk_pr = fs::read_to_string("examples/cugraph_benchmarks/pagerank.impk")
        .expect("Failed to read pagerank.impk");
    let pr_asm = compiler::compile_script_to_impas(&impk_pr, LanguageTarget::ImpK)
        .expect("Failed to compile ImpK PageRank");

    let pr_impas_path = temp_dir.join("pagerank.impas");
    let pr_impb_path = temp_dir.join("pagerank.impb");
    fs::write(&pr_impas_path, &pr_asm).unwrap();

    let assemble_pr = commands::assemble::run(&pr_impas_path, &pr_impb_path);
    assert!(assemble_pr.is_ok(), "Failed to assemble PageRank bytecode");

    let run_pr = commands::run::run(&snapshot_path, &pr_impb_path, 0);
    assert!(
        run_pr.is_ok(),
        "Failed to execute PageRank query against unified snapshot"
    );
}
