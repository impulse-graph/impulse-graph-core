use arrow_array::{ArrayRef, Int32Array, RecordBatch, StringArray};
use arrow_schema::{DataType, Field, Schema};
use impulse_graph_tooling::commands;
use parquet::arrow::ArrowWriter;
use std::fs::{self, File};
use std::path::PathBuf;
use std::sync::Arc;

fn setup_test_workspace() -> PathBuf {
    let dir = std::env::temp_dir().join(format!("impulse_parquet_test_{}", rand::random::<u32>()));
    let _ = fs::create_dir_all(&dir);
    dir
}

fn write_parquet_file(file_path: &PathBuf, schema: Arc<Schema>, columns: Vec<ArrayRef>) {
    let file = File::create(file_path).unwrap();
    let batch = RecordBatch::try_new(schema.clone(), columns).unwrap();
    let mut writer = ArrowWriter::try_new(file, schema, None).unwrap();
    writer.write(&batch).unwrap();
    writer.close().unwrap();
}

#[test]
fn test_parquet_compile_nodes_edges_attributes() {
    let test_dir = setup_test_workspace();

    let node_file = test_dir.join("users.parquet");
    let edge_file = test_dir.join("follows.parquet");
    let manifest_path = test_dir.join("manifest.json");
    let output_snapshot = test_dir.join("snapshot.imps");

    // 1. Write users.parquet (nodes & attributes)
    let node_schema = Arc::new(Schema::new(vec![
        Field::new("user_id", DataType::Utf8, false),
        Field::new("age", DataType::Int32, false),
    ]));
    let user_ids: ArrayRef = Arc::new(StringArray::from(vec!["u1", "u2", "u3"]));
    let ages: ArrayRef = Arc::new(Int32Array::from(vec![25, 30, 35]));
    write_parquet_file(&node_file, node_schema, vec![user_ids, ages]);

    // 2. Write follows.parquet (edges & attributes)
    let edge_schema = Arc::new(Schema::new(vec![
        Field::new("src_user", DataType::Utf8, false),
        Field::new("tgt_user", DataType::Utf8, false),
        Field::new("weight", DataType::Int32, false),
    ]));
    let src_users: ArrayRef = Arc::new(StringArray::from(vec!["u1", "u1", "u2"]));
    let tgt_users: ArrayRef = Arc::new(StringArray::from(vec!["u2", "u3", "u3"]));
    let weights: ArrayRef = Arc::new(Int32Array::from(vec![10, 20, 30]));
    write_parquet_file(&edge_file, edge_schema, vec![src_users, tgt_users, weights]);

    // 3. Write manifest.json
    let manifest_json = r#"{
        "version": "0.9.0",
        "domains": [
            {
                "id": 0,
                "name": "User",
                "key_type": "string",
                "file": "users.parquet",
                "id_column": "user_id",
                "attributes": [
                    { "name": "age", "column": "age", "type": "int32" }
                ]
            }
        ],
        "relations": [
            {
                "src_domain": 0,
                "tgt_domain": 0,
                "file": "follows.parquet",
                "src_column": "src_user",
                "tgt_column": "tgt_user",
                "attributes": [
                    { "name": "weight", "column": "weight", "type": "int32" }
                ]
            }
        ]
    }"#;
    fs::write(&manifest_path, manifest_json).unwrap();

    // 4. Compile to snapshot file
    let res = commands::compile::run(&manifest_path, &output_snapshot);
    assert!(res.is_ok(), "compile from Parquet failed: {:?}", res);

    // 5. Inspect and validate compiled snapshot
    let reader = impulse_graph::SnapshotReader::open(&output_snapshot).unwrap();
    assert_eq!(reader.header().domain_count(), 1);
    assert_eq!(reader.relation_count(), 2);

    let rel_meta = reader
        .relations()
        .iter()
        .find(|r| r.name == "follows")
        .expect("relation follows should exist");
    assert_eq!(rel_meta.edge_count, 3);
    assert_eq!(rel_meta.attributes.len(), 1);

    let node_rel_meta = reader
        .relations()
        .iter()
        .find(|r| r.name == "User_nodes")
        .expect("relation User_nodes should exist");
    assert_eq!(node_rel_meta.edge_count, 0);
    assert_eq!(node_rel_meta.attributes.len(), 1);

    let age_data = reader
        .get_domain_attribute_data(0, "age")
        .expect("domain attribute 'age' should be present");
    assert_eq!(age_data.len(), 12);
    let val0 = i32::from_le_bytes(age_data[0..4].try_into().unwrap());
    let val1 = i32::from_le_bytes(age_data[4..8].try_into().unwrap());
    let val2 = i32::from_le_bytes(age_data[8..12].try_into().unwrap());
    assert_eq!(val0, 25);
    assert_eq!(val1, 30);
    assert_eq!(val2, 35);

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_compile_to_stdout() {
    let test_dir = setup_test_workspace();

    let edge_file = test_dir.join("edges.parquet");
    let manifest_path = test_dir.join("manifest.json");

    let edge_schema = Arc::new(Schema::new(vec![
        Field::new("src", DataType::Utf8, false),
        Field::new("tgt", DataType::Utf8, false),
    ]));
    let src: ArrayRef = Arc::new(StringArray::from(vec!["nodeA", "nodeB"]));
    let tgt: ArrayRef = Arc::new(StringArray::from(vec!["nodeB", "nodeC"]));
    write_parquet_file(&edge_file, edge_schema, vec![src, tgt]);

    let manifest_json = r#"{
        "domains": [
            { "id": 0, "name": "Node", "key_type": "string" }
        ],
        "relations": [
            { "src_domain": 0, "tgt_domain": 0, "file": "edges.parquet" }
        ]
    }"#;
    fs::write(&manifest_path, manifest_json).unwrap();

    // Test output path "-" (stdout)
    let stdout_path = PathBuf::from("-");
    let res = commands::compile::run(&manifest_path, &stdout_path);
    assert!(res.is_ok(), "compile to stdout failed: {:?}", res);

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_parquet_compile_full_spectrum_vectors_and_inspect() {
    use arrow_array::FixedSizeListArray;
    use arrow_array::Float32Array;

    let test_dir = setup_test_workspace();

    let node_file = test_dir.join("nodes.parquet");
    let edge_file = test_dir.join("edges.parquet");
    let manifest_path = test_dir.join("manifest.json");
    let output_snapshot = test_dir.join("full_spectrum.imps");

    // 1. Write nodes.parquet
    let item_field = Arc::new(Field::new("item", DataType::Float32, false));
    let emb_values = Arc::new(Float32Array::from(vec![
        0.1f32, 0.2, 0.3, 1.1, 1.2, 1.3, 2.1, 2.2, 2.3,
    ]));
    let emb_list = FixedSizeListArray::new(item_field.clone(), 3, emb_values, None);

    let node_schema = Arc::new(Schema::new(vec![
        Field::new("user_id", DataType::Utf8, false),
        Field::new("opt_age", DataType::Int32, true),
        Field::new("embedding", DataType::FixedSizeList(item_field, 3), false),
    ]));

    let user_ids: ArrayRef = Arc::new(StringArray::from(vec!["u1", "u2", "u3"]));
    let opt_ages: ArrayRef = Arc::new(Int32Array::from(vec![Some(25), None, Some(35)]));
    let embeddings: ArrayRef = Arc::new(emb_list);

    write_parquet_file(
        &node_file,
        node_schema,
        vec![user_ids, opt_ages, embeddings],
    );

    // 2. Write edges.parquet
    let edge_schema = Arc::new(Schema::new(vec![
        Field::new("src", DataType::Utf8, false),
        Field::new("tgt", DataType::Utf8, false),
        Field::new("opt_weight", DataType::Float32, true),
    ]));
    let srcs: ArrayRef = Arc::new(StringArray::from(vec!["u1", "u2"]));
    let tgts: ArrayRef = Arc::new(StringArray::from(vec!["u2", "u3"]));
    let weights: ArrayRef = Arc::new(Float32Array::from(vec![None, Some(4.5f32)]));

    write_parquet_file(&edge_file, edge_schema, vec![srcs, tgts, weights]);

    // 3. Write manifest.json
    let manifest_json = r#"{
        "version": "0.9.0",
        "domains": [
            {
                "id": 0,
                "name": "User",
                "key_type": "string",
                "file": "nodes.parquet",
                "id_column": "user_id",
                "attributes": [
                    { "name": "opt_age", "column": "opt_age", "type": "int32?" },
                    { "name": "embedding", "column": "embedding", "type": "float32", "dimension": 3 }
                ]
            }
        ],
        "relations": [
            {
                "src_domain": 0,
                "tgt_domain": 0,
                "file": "edges.parquet",
                "name": "FOLLOWS",
                "src_column": "src",
                "tgt_column": "tgt",
                "attributes": [
                    { "name": "opt_weight", "column": "opt_weight", "type": "float32?" }
                ]
            }
        ]
    }"#;
    fs::write(&manifest_path, manifest_json).unwrap();

    // 4. Compile snapshot
    let res = commands::compile::run(&manifest_path, &output_snapshot);
    assert!(res.is_ok(), "compile failed: {:?}", res);

    // 5. Inspect snapshot using inspect command
    let insp_res = commands::inspect::run(&output_snapshot, "table", false);
    assert!(insp_res.is_ok(), "inspect command failed: {:?}", insp_res);

    // 6. Validate snapshot using validate command
    let val_res = commands::validate::run(&output_snapshot, true, None);
    assert!(val_res.is_ok(), "validate command failed: {:?}", val_res);

    // 7. Verify binary contents with SnapshotReader
    let reader = impulse_graph::SnapshotReader::open(&output_snapshot).unwrap();
    assert_eq!(reader.domain_count(), 1);

    // Domain attributes: opt_age (0x83) and embedding (0x06, dim=3)
    let age_data = reader.get_domain_attribute_data(0, "opt_age").unwrap();
    assert_eq!(age_data.len(), 128 + 12);
    // Bitmap: bit 0 = 1, bit 1 = 0, bit 2 = 1 => 0b101 = 5
    let age_word0 = u64::from_le_bytes(age_data[0..8].try_into().unwrap());
    assert_eq!(age_word0 & 0x07, 0b101);

    let emb_data = reader.get_domain_attribute_data(0, "embedding").unwrap();
    // 3 nodes * 3 floats * 4 bytes = 36 bytes
    assert_eq!(emb_data.len(), 36);
    let val_e0 = f32::from_le_bytes(emb_data[0..4].try_into().unwrap());
    let val_e8 = f32::from_le_bytes(emb_data[32..36].try_into().unwrap());
    assert_eq!(val_e0, 0.1f32);
    assert_eq!(val_e8, 2.3f32);

    // Relation attribute: opt_weight (0x86)
    let w_data = reader.get_attribute_data(0, 0).unwrap();
    // 128 bytes bitmap + 2 edges * 4 bytes = 136 bytes
    assert_eq!(w_data.len(), 128 + 8);

    let _ = fs::remove_dir_all(&test_dir);
}
