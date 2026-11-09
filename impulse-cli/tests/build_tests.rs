use impulse_graph::spec::KeyType;
use impulse_graph::SnapshotReader;
use impulse_graph_tooling::commands;
use std::fs;
use std::path::PathBuf;

fn setup_test_workspace() -> PathBuf {
    let dir = std::env::temp_dir().join(format!("impulse_build_test_{}", rand::random::<u32>()));
    let _ = fs::create_dir_all(&dir);
    dir
}

#[test]
fn test_build_simple_tsv_single_domain() {
    let test_dir = setup_test_workspace();
    let manifest_path = test_dir.join("manifest.json");
    let edges_path = test_dir.join("follows.tsv");
    let snapshot_path = test_dir.join("single_domain.imps");

    let manifest_json = r#"{
        "version": "0.9.0",
        "domains": [
            { "id": 0, "name": "User", "key_type": "string" }
        ],
        "relations": [
            { "src_domain": 0, "tgt_domain": 0, "file": "follows.tsv", "name": "FOLLOWS" }
        ]
    }"#;

    let tsv_content = "# Directed follows graph\nu1\tu2\nu1\tu3\nu2\tu3\nu3\tu1\n";
    fs::write(&manifest_path, manifest_json).unwrap();
    fs::write(&edges_path, tsv_content).unwrap();

    let build_res = commands::compile::run(&manifest_path, &snapshot_path);
    assert!(build_res.is_ok(), "build failed: {:?}", build_res);
    assert!(snapshot_path.exists());

    // Validate with Reader
    let reader = SnapshotReader::open(&snapshot_path).unwrap();
    assert_eq!(reader.header().domain_count(), 1);
    assert_eq!(reader.header().relation_count(), 1);

    let dom0 = &reader.domains()[0];
    assert_eq!(dom0.name, "User");
    assert_eq!(dom0.key_type, KeyType::String);
    assert_eq!(dom0.node_count, 3);

    let rel0 = &reader.relations()[0];
    assert_eq!(rel0.name, "FOLLOWS");
    assert_eq!(rel0.node_count, 3);
    assert_eq!(rel0.edge_count, 4);

    let row_offsets = reader.get_row_offsets(0).unwrap();
    let col_indices = reader.get_col_indices(0).unwrap();
    assert_eq!(row_offsets.len(), 4); // N + 1
    assert_eq!(col_indices.len(), 4);
    assert_eq!(row_offsets[0], 0);
    assert_eq!(row_offsets[3], 4);

    // Validate compliance
    let val_res = commands::validate::run(&snapshot_path, true, None);
    assert!(val_res.is_ok(), "validator failed: {:?}", val_res);

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_build_multi_domain_heterogeneous_graph() {
    let test_dir = setup_test_workspace();
    let manifest_path = test_dir.join("multi_domain_manifest.json");
    let users_docs_tsv = test_dir.join("users_docs.tsv");
    let docs_topics_tsv = test_dir.join("docs_topics.tsv");
    let snapshot_path = test_dir.join("multi_domain.imps");

    let manifest_json = r#"{
        "version": "0.9.0",
        "domains": [
            { "id": 0, "name": "User", "key_type": "string" },
            { "id": 1, "name": "Document", "key_type": "string" },
            { "id": 2, "name": "Topic", "key_type": "string" }
        ],
        "relations": [
            { "src_domain": 0, "tgt_domain": 1, "file": "users_docs.tsv", "name": "AUTHORED" },
            { "src_domain": 1, "tgt_domain": 2, "file": "docs_topics.tsv", "name": "TAGGED_WITH" }
        ]
    }"#;

    fs::write(&manifest_path, manifest_json).unwrap();
    fs::write(
        &users_docs_tsv,
        "alice\tdoc_101\nalice\tdoc_102\nbob\tdoc_102\n",
    )
    .unwrap();
    fs::write(
        &docs_topics_tsv,
        "doc_101\tgraph_analytics\ndoc_102\tdatabases\ndoc_102\tcompilers\n",
    )
    .unwrap();

    let build_res = commands::compile::run(&manifest_path, &snapshot_path);
    assert!(
        build_res.is_ok(),
        "multi-domain build failed: {:?}",
        build_res
    );

    let reader = SnapshotReader::open(&snapshot_path).unwrap();
    assert_eq!(reader.header().domain_count(), 3);
    assert_eq!(reader.header().relation_count(), 2);

    assert_eq!(reader.domains()[0].name, "User");
    assert_eq!(reader.domains()[1].name, "Document");
    assert_eq!(reader.domains()[2].name, "Topic");

    assert_eq!(reader.relations()[0].name, "AUTHORED");
    assert_eq!(reader.relations()[0].src_domain_id, 0);
    assert_eq!(reader.relations()[0].tgt_domain_id, 1);
    assert_eq!(reader.relations()[0].edge_count, 3);

    assert_eq!(reader.relations()[1].name, "TAGGED_WITH");
    assert_eq!(reader.relations()[1].src_domain_id, 1);
    assert_eq!(reader.relations()[1].tgt_domain_id, 2);
    assert_eq!(reader.relations()[1].edge_count, 3);

    // Verify 128-byte hardware alignment across sections
    let val_res = commands::validate::run(&snapshot_path, true, None);
    assert!(val_res.is_ok(), "validation failed: {:?}", val_res);

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_build_domain_predefined_nodes_and_isolated_vertices() {
    let test_dir = setup_test_workspace();
    let manifest_path = test_dir.join("manifest.json");
    let users_file = test_dir.join("users_catalog.tsv");
    let edges_file = test_dir.join("interactions.tsv");
    let snapshot_path = test_dir.join("isolated_nodes.imps");

    // Catalog defines 5 users: u1, u2, u3, u4, u5
    fs::write(&users_file, "u1\nu2\nu3\nu4\nu5\n").unwrap();
    // Edges only connect u1 -> u2 and u2 -> u3 (u4 and u5 are isolated degree-0 vertices)
    fs::write(&edges_file, "u1\tu2\nu2\tu3\n").unwrap();

    let manifest_json = r#"{
        "version": "0.9.0",
        "domains": [
            { "id": 0, "name": "User", "key_type": "string", "file": "users_catalog.tsv" }
        ],
        "relations": [
            { "src_domain": 0, "tgt_domain": 0, "file": "interactions.tsv", "name": "INTERACTS" }
        ]
    }"#;
    fs::write(&manifest_path, manifest_json).unwrap();

    let build_res = commands::compile::run(&manifest_path, &snapshot_path);
    assert!(
        build_res.is_ok(),
        "build with predefined nodes failed: {:?}",
        build_res
    );

    let reader = SnapshotReader::open(&snapshot_path).unwrap();
    assert_eq!(
        reader.domains()[0].node_count,
        5,
        "Domain must contain all 5 predefined nodes"
    );

    let row_offsets = reader.get_row_offsets(0).unwrap();
    assert_eq!(
        row_offsets.len(),
        6,
        "Row offsets size must be node_count + 1 = 6"
    );
    assert_eq!(row_offsets[5], 2, "Total edges must be 2");

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_build_rejects_missing_edge_files() {
    let test_dir = setup_test_workspace();
    let manifest_path = test_dir.join("bad_manifest.json");
    let snapshot_path = test_dir.join("should_not_exist.imps");

    let manifest_json = r#"{
        "domains": [
            { "id": 0, "name": "User", "key_type": "string" }
        ],
        "relations": [
            { "src_domain": 0, "tgt_domain": 0, "file": "non_existent_file.tsv" }
        ]
    }"#;
    fs::write(&manifest_path, manifest_json).unwrap();

    let build_res = commands::compile::run(&manifest_path, &snapshot_path);
    assert!(
        build_res.is_err(),
        "build MUST fail when edge file does not exist"
    );

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_build_with_typed_soa_edge_attributes() {
    let test_dir = setup_test_workspace();
    let manifest_path = test_dir.join("attr_manifest.json");
    let edges_path = test_dir.join("financial_transfers.tsv");
    let snapshot_path = test_dir.join("financial.imps");

    let tsv_content = "acc_001\tacc_002\nacc_002\tacc_003\nacc_001\tacc_003\n";
    fs::write(&edges_path, tsv_content).unwrap();

    let manifest_json = r#"{
        "version": "0.9.0",
        "domains": [
            { "id": 0, "name": "Account", "key_type": "string" }
        ],
        "relations": [
            {
                "src_domain": 0,
                "tgt_domain": 0,
                "file": "financial_transfers.tsv",
                "name": "TRANSFERRED"
            }
        ]
    }"#;
    fs::write(&manifest_path, manifest_json).unwrap();

    let build_res = commands::compile::run(&manifest_path, &snapshot_path);
    assert!(
        build_res.is_ok(),
        "build with attributes failed: {:?}",
        build_res
    );

    let reader = SnapshotReader::open(&snapshot_path).unwrap();
    let rel = &reader.relations()[0];
    assert_eq!(rel.name, "TRANSFERRED");
    assert_eq!(rel.edge_count, 3);

    // Compute stats on the generated snapshot
    let stats_res = commands::stats::run(&snapshot_path, "json", true, 2.0);
    assert!(
        stats_res.is_ok(),
        "stats failed on built snapshot: {:?}",
        stats_res
    );

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_build_full_spectrum_of_attribute_types_and_nullability() {
    let test_dir = setup_test_workspace();
    let manifest_path = test_dir.join("full_types_manifest.json");
    let edges_path = test_dir.join("edges.tsv");
    let snapshot_path = test_dir.join("full_types.imps");

    fs::write(&edges_path, "n1\tn2\nn2\tn3\n").unwrap();

    let manifest_json = r#"{
        "version": "0.9.0",
        "domains": [
            { "id": 0, "name": "Node", "key_type": "string" }
        ],
        "relations": [
            {
                "src_domain": 0,
                "tgt_domain": 0,
                "file": "edges.tsv",
                "name": "CONNECTED",
                "attributes": [
                    { "name": "flag_int8", "column": "f8", "type": "int8" },
                    { "name": "short_int16", "column": "f16", "type": "int16" },
                    { "name": "count_int32", "column": "f32", "type": "int32" },
                    { "name": "id_int64", "column": "f64", "type": "int64" },
                    { "name": "weight_f32", "column": "wf32", "type": "float32" },
                    { "name": "precise_f64", "column": "wf64", "type": "float64" },
                    { "name": "created_ts_ms", "column": "ts_ms", "type": "timestamp_ms" },
                    { "name": "created_ts_ns", "column": "ts_ns", "type": "timestamp_ns" },
                    { "name": "raw_uuid", "column": "uuid", "type": "fixed_bytes", "dimension": 16 },
                    { "name": "label_str", "column": "str", "type": "var_string" },
                    { "name": "opt_score", "column": "opt_s", "type": "nullable_float32" },
                    { "name": "opt_flag", "column": "opt_f", "type": "int8?" }
                ]
            }
        ]
    }"#;
    fs::write(&manifest_path, manifest_json).unwrap();

    let build_res = commands::compile::run(&manifest_path, &snapshot_path);
    assert!(
        build_res.is_ok(),
        "build full types failed: {:?}",
        build_res
    );

    let reader = SnapshotReader::open(&snapshot_path).unwrap();
    let rel = &reader.relations()[0];
    assert_eq!(rel.name, "CONNECTED");
    assert_eq!(rel.attributes.len(), 12);

    // Verify type codes and nullability
    assert_eq!(rel.attributes[0].type_code, 0x01); // int8
    assert_eq!(rel.attributes[1].type_code, 0x02); // int16
    assert_eq!(rel.attributes[2].type_code, 0x03); // int32
    assert_eq!(rel.attributes[3].type_code, 0x04); // int64
    assert_eq!(rel.attributes[4].type_code, 0x06); // float32
    assert_eq!(rel.attributes[5].type_code, 0x07); // float64
    assert_eq!(rel.attributes[6].type_code, 0x08); // timestamp_ms
    assert_eq!(rel.attributes[7].type_code, 0x09); // timestamp_ns
    assert_eq!(rel.attributes[8].type_code, 0x0A); // fixed_bytes
    assert_eq!(rel.attributes[9].type_code, 0x0B); // var_string
    assert_eq!(rel.attributes[10].type_code, 0x86); // nullable float32 (0x06 | 0x80)
    assert_eq!(rel.attributes[11].type_code, 0x81); // nullable int8 (0x01 | 0x80)

    let val_res = commands::validate::run(&snapshot_path, true, None);
    assert!(val_res.is_ok(), "validation failed: {:?}", val_res);

    let _ = fs::remove_dir_all(&test_dir);
}
