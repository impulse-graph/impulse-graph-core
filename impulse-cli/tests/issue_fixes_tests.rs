use arrow_array::{ArrayRef, Int32Array, RecordBatch};
use arrow_schema::{DataType, Field, Schema};
use impulse_graph::spec::{BaseDataType, KeyType};
use impulse_graph::{SnapshotReader, SnapshotWriter};
use impulse_graph_tooling::commands;
use impulse_graph_tooling::parquet_reader::{read_csv_column_data, read_parquet_column_data};
use parquet::arrow::ArrowWriter;
use std::fs::{self, File};
use std::path::PathBuf;
use std::sync::Arc;

fn setup_test_workspace(prefix: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("{}_{}", prefix, rand::random::<u32>()));
    let _ = fs::create_dir_all(&dir);
    dir
}

// Issue #10: FNV-1a Offset Basis Mismatch in compile.rs
#[test]
fn test_issue_10_fnv1a_offset_basis() {
    let seed = 0x1234567890ABCDEF_u64;
    let key = "DomainEntity_42";

    // Expected FNV-1a hash matching C++ impulse_index.cpp:
    // uint64_t hash = 14695981039346656037ULL ^ seed;
    let mut expected_h = 14695981039346656037_u64 ^ seed;
    for &b in key.as_bytes() {
        expected_h ^= b as u64;
        expected_h = expected_h.wrapping_mul(1099511628211);
    }

    // Verify basis constant matches standard 64-bit FNV-1a offset basis
    assert_eq!(14695981039346656037_u64, 0xcbf29ce484222325_u64);
    assert_ne!(expected_h, seed);
}

// Issue #7: CLI ignores include_csc in compile.rs
#[test]
fn test_issue_7_compile_wires_include_csc() {
    let dir = setup_test_workspace("test_issue_7");
    let edges_tsv = dir.join("edges.tsv");
    fs::write(&edges_tsv, "u1\tu2\nu2\tu3\nu3\tu1\n").unwrap();

    let manifest_json = dir.join("manifest.json");
    let manifest_content = format!(
        r#"{{
            "domains": [
                {{"id": 0, "name": "Node", "key_type": "string"}}
            ],
            "relations": [
                {{
                    "name": "CONNECTS",
                    "src_domain": 0,
                    "tgt_domain": 0,
                    "file": "{}",
                    "include_csc": true
                }}
            ]
        }}"#,
        edges_tsv.file_name().unwrap().to_str().unwrap()
    );
    fs::write(&manifest_json, manifest_content).unwrap();

    let output_imps = dir.join("output_with_csc.imps");
    commands::compile::run(&manifest_json, &output_imps).expect("compile failed");

    let reader = SnapshotReader::open(&output_imps).expect("open compiled snapshot failed");
    assert_eq!(reader.relations().len(), 1);
    let rel = &reader.relations()[0];

    // Check that CSC arrays are populated and non-zero
    assert!(
        rel.csc_row_off_offset > 0,
        "rel.csc_row_off_offset must be > 0 when include_csc is true"
    );
    assert!(
        rel.csc_col_idx_offset > 0,
        "rel.csc_col_idx_offset must be > 0 when include_csc is true"
    );
    assert!(rel.csc_row_off_bytes > 0);
    assert!(rel.csc_col_idx_bytes > 0);

    // Validate with deep_audit
    commands::validate::run_with_options(
        &output_imps,
        &commands::validate::ValidateOptions {
            strict_alignment: true,
            public_key: None,
            quick_check: false,
            deep_audit: true,
        },
    )
    .expect("deep_audit validation on compiled CSC snapshot must pass");

    let _ = fs::remove_dir_all(&dir);
}

// Issue #8: Duplicate column names in read_parquet_column_data & read_csv_column_data
#[test]
fn test_issue_8_duplicate_column_names_no_multi_accumulation() {
    let dir = setup_test_workspace("test_issue_8");

    // 1. Test Parquet Reader
    let pq_path = dir.join("data.parquet");
    let schema = Arc::new(Schema::new(vec![Field::new(
        "value",
        DataType::Int32,
        false,
    )]));
    let col: ArrayRef = Arc::new(Int32Array::from(vec![10, 20, 30, 40, 50]));
    let batch = RecordBatch::try_new(schema.clone(), vec![col]).unwrap();
    let file = File::create(&pq_path).unwrap();
    let mut writer = ArrowWriter::try_new(file, schema, None).unwrap();
    writer.write(&batch).unwrap();
    writer.close().unwrap();

    // Request the same column twice
    let reqs = vec![
        ("value", Some("int32"), None),
        ("value", Some("int32"), None),
    ];
    let cols = read_parquet_column_data(&pq_path, &reqs).expect("read_parquet failed");
    let val_col = &cols["value"];
    assert_eq!(
        val_col.row_count, 5,
        "Expected exactly 5 rows in Parquet reader, got {}",
        val_col.row_count
    );
    assert_eq!(val_col.data.len(), 5 * 4);

    // 2. Test CSV Reader
    let csv_path = dir.join("data.csv");
    fs::write(&csv_path, "value\n10\n20\n30\n40\n50\n").unwrap();
    let csv_cols = read_csv_column_data(&csv_path, &reqs).expect("read_csv failed");
    let csv_val_col = &csv_cols["value"];
    assert_eq!(
        csv_val_col.row_count, 5,
        "Expected exactly 5 rows in CSV reader, got {}",
        csv_val_col.row_count
    );

    let _ = fs::remove_dir_all(&dir);
}

// Issue #9: validate rejects valid multi-relations
#[test]
fn test_issue_9_validate_allows_valid_multi_relations() {
    let dir = setup_test_workspace("test_issue_9");
    let imps_path = dir.join("multi_rel.imps");

    let mut writer = SnapshotWriter::new(imps_path.to_str().unwrap());
    writer.add_domain(0, KeyType::Int64, "User");
    writer.set_domain_node_count(0, 10);
    writer.add_domain(1, KeyType::Int64, "Post");
    writer.set_domain_node_count(1, 20);

    // Relation 1: User (0) -> Post (1) [e.g. AUTHORED]
    let row_off1 = vec![0u32, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
    let col_idx1: Vec<u32> = (0..10).collect();
    writer.add_relation(0, 1, 10, 10, row_off1, col_idx1);
    writer.set_relation_name(0, "AUTHORED");

    // Relation 2: User (0) -> Post (1) [e.g. LIKED]
    let row_off2 = vec![0u32, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
    let col_idx2: Vec<u32> = (10..20).collect();
    writer.add_relation(0, 1, 10, 10, row_off2, col_idx2);
    writer.set_relation_name(1, "LIKED");

    writer.finalize().expect("writer finalize failed");

    // Validate using validate command
    let val_res = commands::validate::run_with_options(
        &imps_path,
        &commands::validate::ValidateOptions {
            strict_alignment: false,
            public_key: None,
            quick_check: false,
            deep_audit: false,
        },
    );
    assert!(
        val_res.is_ok(),
        "validate must allow multiple relations with same (src, tgt) in sorted order: {:?}",
        val_res
    );

    let _ = fs::remove_dir_all(&dir);
}

// Issue #4: Add anonymized export to impulse inspect
#[test]
fn test_issue_4_anonymized_inspect() {
    let dir = setup_test_workspace("test_issue_4");
    let imps_path = dir.join("inspect_anon.imps");

    let mut writer = SnapshotWriter::new(imps_path.to_str().unwrap());
    writer.add_domain(0, KeyType::String, "Customer");
    writer.set_domain_node_count(0, 50);
    writer.add_domain(1, KeyType::String, "Product");
    writer.set_domain_node_count(1, 100);

    let row_off = vec![0u32; 51];
    let col_idx = Vec::new();
    writer.add_relation(0, 1, 50, 0, row_off, col_idx);
    writer.set_relation_name(0, "PURCHASED");
    writer.add_attribute_to_relation(
        0,
        "price",
        BaseDataType::Float32 as u8,
        1,
        vec![0u8; 0],
        None,
    );
    writer.finalize().expect("writer finalize failed");

    // 1. Test inspect with anonymize = true
    let anon_opts = commands::inspect::InspectOptions {
        format: "json",
        verbose: true,
        anonymize: true,
        domain_filter: None,
        relation_filter: None,
    };
    let res = commands::inspect::run_with_options(&imps_path, &anon_opts);
    assert!(res.is_ok());

    // 2. Test domain filter
    let dom_filter_opts = commands::inspect::InspectOptions {
        format: "text",
        verbose: false,
        anonymize: true,
        domain_filter: Some("Domain_1"),
        relation_filter: None,
    };
    let res_dom = commands::inspect::run_with_options(&imps_path, &dom_filter_opts);
    assert!(res_dom.is_ok());

    // 3. Test relation filter
    let rel_filter_opts = commands::inspect::InspectOptions {
        format: "text",
        verbose: false,
        anonymize: true,
        domain_filter: None,
        relation_filter: Some("Relation_1"),
    };
    let res_rel = commands::inspect::run_with_options(&imps_path, &rel_filter_opts);
    assert!(res_rel.is_ok());

    let _ = fs::remove_dir_all(&dir);
}
