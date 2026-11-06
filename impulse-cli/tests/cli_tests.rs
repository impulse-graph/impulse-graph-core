use impulse_graph_tooling::commands;
use std::fs;
use std::path::PathBuf;
use std::process::Command;

fn setup_test_workspace() -> PathBuf {
    let dir = std::env::temp_dir().join(format!("impulse_test_{}", rand::random::<u32>()));
    let _ = fs::create_dir_all(&dir);
    dir
}

#[test]
fn test_full_cli_command_suite() {
    let test_dir = setup_test_workspace();

    let manifest_path = test_dir.join("manifest.json");
    let edges_path = test_dir.join("user_follows.tsv");
    let base_snapshot = test_dir.join("base_graph.imps");
    let opt_snapshot = test_dir.join("opt_graph.imps");
    let export_dir = test_dir.join("exported_data");
    let key_prefix = test_dir.join("test_key");

    let manifest_json = r#"{
        "version": "0.9.0",
        "domains": [
            { "id": 0, "name": "User", "key_type": "string" },
            { "id": 1, "name": "Group", "key_type": "string" }
        ],
        "relations": [
            { "src_domain": 0, "tgt_domain": 1, "encoding": "raw_uint32", "file": "user_follows.tsv" }
        ]
    }"#;

    let tsv_content = "user_A\tgroup_1\nuser_A\tgroup_2\nuser_B\tgroup_2\nuser_C\tgroup_3\n";

    fs::write(&manifest_path, manifest_json).expect("Failed to write manifest");
    fs::write(&edges_path, tsv_content).expect("Failed to write edge TSV");

    // 1. COMPILE
    let compile_res = commands::compile::run(&manifest_path, &base_snapshot);
    assert!(compile_res.is_ok(), "compile failed: {:?}", compile_res);
    assert!(base_snapshot.exists(), "Compiled binary snapshot missing");

    // 2. INSPECT (Text & JSON mode)
    let inspect_text_res = commands::inspect::run(&base_snapshot, "text", true);
    assert!(
        inspect_text_res.is_ok(),
        "inspect (text) failed: {:?}",
        inspect_text_res
    );

    let inspect_json_res = commands::inspect::run(&base_snapshot, "json", false);
    assert!(
        inspect_json_res.is_ok(),
        "inspect (json) failed: {:?}",
        inspect_json_res
    );

    // 3. VALIDATE
    let validate_res = commands::validate::run(&base_snapshot, true, None);
    assert!(validate_res.is_ok(), "validate failed: {:?}", validate_res);

    // 4. OPTIMIZE
    let opt_res = commands::optimize::run(
        &base_snapshot,
        &opt_snapshot,
        true,  // RCM
        false, // degree_sort
        false, // csc
        Some("delta_vbyte"),
        false, // strip_mappings
        false, // strip_properties
    );
    assert!(opt_res.is_ok(), "optimize failed: {:?}", opt_res);
    assert!(opt_snapshot.exists(), "Optimized binary snapshot missing");

    // 5. KEYGEN
    let keygen_res = commands::crypto::keygen(key_prefix.to_str().unwrap());
    assert!(keygen_res.is_ok(), "keygen failed: {:?}", keygen_res);

    let priv_key_path = PathBuf::from(format!("{}.priv", key_prefix.to_string_lossy()));
    let pub_key_path = PathBuf::from(format!("{}.pub", key_prefix.to_string_lossy()));
    assert!(priv_key_path.exists(), "Private key file missing");
    assert!(pub_key_path.exists(), "Public key file missing");

    // 6. SIGN
    let sign_res = commands::crypto::sign(&base_snapshot, &priv_key_path);
    assert!(sign_res.is_ok(), "sign failed: {:?}", sign_res);

    let sig_path = PathBuf::from(format!("{}.sig", base_snapshot.to_string_lossy()));
    assert!(sig_path.exists(), "Signature file missing");

    // 7. VERIFY
    let verify_res = commands::crypto::verify(&base_snapshot, &pub_key_path);
    assert!(verify_res.is_ok(), "verify failed: {:?}", verify_res);

    // 8. DIFF
    let diff_res = commands::diff::run(&base_snapshot, &opt_snapshot);
    assert!(diff_res.is_ok(), "diff failed: {:?}", diff_res);

    // 9. EXPORT
    let export_res = commands::export::run(&base_snapshot, &export_dir, "tsv");
    assert!(export_res.is_ok(), "export failed: {:?}", export_res);
    assert!(export_dir.exists(), "Export directory missing");

    let exported_files: Vec<_> = fs::read_dir(&export_dir).unwrap().collect();
    assert!(!exported_files.is_empty(), "Export directory is empty");

    // Clean up
    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_binary_executable_invocation() {
    let cargo_bin = env!("CARGO_BIN_EXE_impulse-graph");

    let status = Command::new(cargo_bin)
        .arg("--help")
        .status()
        .expect("Failed to execute impulse-graph binary");

    assert!(
        status.success(),
        "impulse-graph --help returned non-zero exit status"
    );
}

#[test]
fn test_compile_multi_relation_unique_domain_count() {
    let test_dir = setup_test_workspace();

    let manifest_path = test_dir.join("rbac_manifest.json");
    let user_group_path = test_dir.join("user_group.tsv");
    let group_role_path = test_dir.join("group_role.tsv");
    let output_snapshot = test_dir.join("rbac_snapshot.imps");

    let manifest_json = r#"{
        "version": "0.9.0",
        "domains": [
            { "id": 0, "name": "USER", "key_type": "string" },
            { "id": 1, "name": "GROUP", "key_type": "string" },
            { "id": 2, "name": "ROLE", "key_type": "string" }
        ],
        "relations": [
            { "src_domain": 0, "tgt_domain": 1, "encoding": "raw_uint32", "file": "user_group.tsv" },
            { "src_domain": 1, "tgt_domain": 2, "encoding": "raw_uint32", "file": "group_role.tsv" }
        ]
    }"#;

    fs::write(&manifest_path, manifest_json).expect("Failed to write manifest");
    fs::write(&user_group_path, "u1\tg1\nu2\tg2\n").expect("Failed to write user_group TSV");
    fs::write(&group_role_path, "g1\tr1\ng2\tr2\n").expect("Failed to write group_role TSV");

    let compile_res = commands::compile::run(&manifest_path, &output_snapshot);
    assert!(compile_res.is_ok(), "compile failed: {:?}", compile_res);

    let reader =
        impulse_graph::SnapshotReader::open(&output_snapshot).expect("Failed to open snapshot");
    assert_eq!(
        reader.header().domain_count(),
        3,
        "Domain count MUST be exactly 3 for 3 unique domains"
    );
    assert_eq!(reader.relation_count(), 2, "Relation count MUST be 2");

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_compile_duplicate_relation_rejection() {
    let test_dir = setup_test_workspace();

    let manifest_path = test_dir.join("duplicate_rel_manifest.json");
    let user_group_path = test_dir.join("user_group.tsv");
    let output_snapshot = test_dir.join("dup_rel_snapshot.imps");

    let manifest_json = r#"{
        "version": "0.9.0",
        "domains": [
            { "id": 0, "name": "USER", "key_type": "string" },
            { "id": 1, "name": "GROUP", "key_type": "string" }
        ],
        "relations": [
            { "src_domain": 0, "tgt_domain": 1, "encoding": "raw_uint32", "file": "user_group.tsv" },
            { "src_domain": 0, "tgt_domain": 1, "encoding": "raw_uint32", "file": "user_group.tsv" }
        ]
    }"#;

    fs::write(&manifest_path, manifest_json).expect("Failed to write manifest");
    fs::write(&user_group_path, "u1\tg1\n").expect("Failed to write user_group TSV");

    let compile_res = commands::compile::run(&manifest_path, &output_snapshot);
    assert!(
        compile_res.is_err(),
        "compile MUST fail when duplicate relations are defined"
    );
    let err_msg = compile_res.unwrap_err().to_string();
    assert!(
        err_msg.contains("Duplicate relation definition"),
        "Error message MUST indicate duplicate relation: {}",
        err_msg
    );

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_stats_cli_invocation_and_json_mode() {
    let test_dir = setup_test_workspace();
    let snapshot_path = test_dir.join("stats_graph.imps");

    let gen_args = impulse_graph_tooling::GenerateArgs {
        profile: "graph500".to_string(),
        output: snapshot_path.clone(),
        format: "imps".to_string(),
        scale: 8,
        edge_factor: 16,
        a: 0.57,
        b: 0.19,
        c: 0.19,
        d: 0.05,
        nodes: None,
        edges: None,
        edges_per_node: 8,
        p: None,
        communities: 10,
        p_intra: 0.02,
        p_inter: 0.0005,
        dim_x: 32,
        dim_y: 32,
        dim_z: None,
        toroidal: false,
        branching: 3,
        depth: 6,
        star: false,
        src_nodes: None,
        tgt_nodes: None,
        seed: Some(42),
        undirected: false,
        allow_self_loops: false,
        include_csc: false,
        domain_name: "Vertex".to_string(),
        tgt_domain_name: "Resource".to_string(),
        relation_name: "EDGES".to_string(),
        attributes: Some("weight:f32".to_string()),
        chunk_size: 1_000_000,
        embedding_dim: None,
        embedding_attr: "embedding".to_string(),
        vector_index: None,
    };

    let gen_res = commands::generate::run(&gen_args);
    assert!(gen_res.is_ok(), "generate failed: {:?}", gen_res);

    // Text format stats
    let stats_text_res = commands::stats::run(&snapshot_path, "text", true, 3.0);
    assert!(
        stats_text_res.is_ok(),
        "stats (text) failed: {:?}",
        stats_text_res
    );

    // JSON format stats
    let stats_json_res = commands::stats::run(&snapshot_path, "json", false, 3.0);
    assert!(
        stats_json_res.is_ok(),
        "stats (json) failed: {:?}",
        stats_json_res
    );

    let _ = fs::remove_dir_all(&test_dir);
}
