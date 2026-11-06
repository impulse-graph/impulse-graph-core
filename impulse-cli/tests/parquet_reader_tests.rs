use arrow_array::{
    ArrayRef, Date32Array, FixedSizeListArray, Float32Array, Float64Array, Int32Array, RecordBatch,
    StringArray, TimestampMillisecondArray,
};
use arrow_schema::{DataType, Field, Schema, TimeUnit};
use impulse_graph_tooling::parquet_reader::{read_parquet_column_data, read_text_column_data};
use parquet::arrow::ArrowWriter;
use std::fs::{self, File};
use std::path::PathBuf;
use std::sync::Arc;

fn setup_test_workspace() -> PathBuf {
    let dir =
        std::env::temp_dir().join(format!("impulse_pq_reader_test_{}", rand::random::<u32>()));
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
fn test_read_parquet_primitives_non_null() {
    let test_dir = setup_test_workspace();
    let pq_path = test_dir.join("primitives.parquet");

    let schema = Arc::new(Schema::new(vec![
        Field::new("i32_col", DataType::Int32, false),
        Field::new("f32_col", DataType::Float32, false),
        Field::new("f64_col", DataType::Float64, false),
        Field::new("str_col", DataType::Utf8, false),
    ]));

    let col_i32: ArrayRef = Arc::new(Int32Array::from(vec![10, 20, 30]));
    let col_f32: ArrayRef = Arc::new(Float32Array::from(vec![1.5f32, 2.5, 3.5]));
    let col_f64: ArrayRef = Arc::new(Float64Array::from(vec![100.25f64, 200.5, 300.75]));
    let col_str: ArrayRef = Arc::new(StringArray::from(vec!["alpha", "beta", "gamma"]));

    write_parquet_file(&pq_path, schema, vec![col_i32, col_f32, col_f64, col_str]);

    let reqs = vec![
        ("i32_col", Some("int32"), None),
        ("f32_col", Some("float32"), None),
        ("f64_col", Some("float64"), None),
        ("str_col", Some("string"), None),
    ];

    let cols = read_parquet_column_data(&pq_path, &reqs).unwrap();

    // Check Int32
    let i32_col = &cols["i32_col"];
    assert_eq!(i32_col.type_code, 0x03); // INT32 non-null
    assert_eq!(i32_col.dimension, 1);
    assert_eq!(i32_col.null_count, 0);
    assert_eq!(i32_col.data.len(), 12);
    let val0 = i32::from_le_bytes(i32_col.data[0..4].try_into().unwrap());
    let val1 = i32::from_le_bytes(i32_col.data[4..8].try_into().unwrap());
    let val2 = i32::from_le_bytes(i32_col.data[8..12].try_into().unwrap());
    assert_eq!(val0, 10);
    assert_eq!(val1, 20);
    assert_eq!(val2, 30);

    // Check Float32
    let f32_col = &cols["f32_col"];
    assert_eq!(f32_col.type_code, 0x06); // FLOAT32 non-null
    assert_eq!(f32_col.data.len(), 12);
    let fval0 = f32::from_le_bytes(f32_col.data[0..4].try_into().unwrap());
    assert_eq!(fval0, 1.5);

    // Check Float64
    let f64_col = &cols["f64_col"];
    assert_eq!(f64_col.type_code, 0x07); // FLOAT64 non-null
    assert_eq!(f64_col.data.len(), 24);

    // Check String
    let str_col = &cols["str_col"];
    assert_eq!(str_col.type_code, 0x0B); // VAR_STRING non-null
    assert!(str_col.offsets.is_some());
    let offs = str_col.offsets.as_ref().unwrap();
    assert_eq!(offs.len(), 4); // 0, 5, 9, 14
    assert_eq!(offs[0], 0);

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_read_parquet_nullable_with_validity_bitmap() {
    let test_dir = setup_test_workspace();
    let pq_path = test_dir.join("nullable.parquet");

    let schema = Arc::new(Schema::new(vec![Field::new(
        "opt_i32",
        DataType::Int32,
        true,
    )]));

    let col_i32: ArrayRef = Arc::new(Int32Array::from(vec![Some(100), None, Some(300)]));
    write_parquet_file(&pq_path, schema, vec![col_i32]);

    let reqs = vec![("opt_i32", Some("int32?"), None)];
    let cols = read_parquet_column_data(&pq_path, &reqs).unwrap();

    let col = &cols["opt_i32"];
    // 0x03 | 0x80 = 0x83 (Nullable INT32)
    assert_eq!(col.type_code, 0x83);
    assert_eq!(col.null_count, 1);
    assert_eq!(col.row_count, 3);

    // Validity bitmap is padded to 128 bytes
    // Data is: 128 bytes validity bitmap + 3 * 4 bytes primitive data = 140 bytes
    assert_eq!(col.data.len(), 128 + 12);

    // Check bitmap bits: item 0 = 1, item 1 = 0, item 2 = 1 => 0b101 = 5
    let bitmap_word0 = u64::from_le_bytes(col.data[0..8].try_into().unwrap());
    assert_eq!(bitmap_word0 & 0x07, 0b101);

    // Check primitive data right after 128 bytes
    let payload = &col.data[128..];
    let val0 = i32::from_le_bytes(payload[0..4].try_into().unwrap());
    let val2 = i32::from_le_bytes(payload[8..12].try_into().unwrap());
    assert_eq!(val0, 100);
    assert_eq!(val2, 300);

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_read_parquet_fixed_size_list_vector_embeddings() {
    let test_dir = setup_test_workspace();
    let pq_path = test_dir.join("embeddings.parquet");

    // Dimension = 4 floats per row, 2 rows
    let values = Arc::new(Float32Array::from(vec![
        0.1f32, 0.2, 0.3, 0.4, 1.1, 1.2, 1.3, 1.4,
    ]));
    let item_field = Arc::new(Field::new("item", DataType::Float32, false));
    let list_array = FixedSizeListArray::new(item_field.clone(), 4, values, None);

    let schema = Arc::new(Schema::new(vec![Field::new(
        "embedding",
        DataType::FixedSizeList(item_field, 4),
        false,
    )]));

    write_parquet_file(&pq_path, schema, vec![Arc::new(list_array)]);

    let reqs = vec![("embedding", Some("float32"), Some(4))];
    let cols = read_parquet_column_data(&pq_path, &reqs).unwrap();

    let col = &cols["embedding"];
    assert_eq!(col.type_code, 0x06); // FLOAT32
    assert_eq!(col.dimension, 4);
    assert_eq!(col.row_count, 2);
    // 2 rows * 4 floats * 4 bytes = 32 bytes
    assert_eq!(col.data.len(), 32);

    let f0 = f32::from_le_bytes(col.data[0..4].try_into().unwrap());
    let f7 = f32::from_le_bytes(col.data[28..32].try_into().unwrap());
    assert_eq!(f0, 0.1f32);
    assert_eq!(f7, 1.4f32);

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_read_parquet_timestamps_and_dates() {
    let test_dir = setup_test_workspace();
    let pq_path = test_dir.join("temporal.parquet");

    let schema = Arc::new(Schema::new(vec![
        Field::new(
            "ts_ms",
            DataType::Timestamp(TimeUnit::Millisecond, None),
            false,
        ),
        Field::new("date_d", DataType::Date32, false),
    ]));

    let col_ts: ArrayRef = Arc::new(TimestampMillisecondArray::from(vec![
        1700000000000i64,
        1700000001000,
    ]));
    let col_dt: ArrayRef = Arc::new(Date32Array::from(vec![19000i32, 19001]));

    write_parquet_file(&pq_path, schema, vec![col_ts, col_dt]);

    let reqs = vec![
        ("ts_ms", Some("timestamp_ms"), None),
        ("date_d", Some("date32"), None),
    ];
    let cols = read_parquet_column_data(&pq_path, &reqs).unwrap();

    let ts_col = &cols["ts_ms"];
    assert_eq!(ts_col.type_code, 0x08); // TIMESTAMP_MS
    assert_eq!(ts_col.data.len(), 16);
    let t0 = i64::from_le_bytes(ts_col.data[0..8].try_into().unwrap());
    assert_eq!(t0, 1700000000000);

    let dt_col = &cols["date_d"];
    assert_eq!(dt_col.type_code, 0x03); // INT32 (Date32 is days since epoch stored as int32)
    assert_eq!(dt_col.data.len(), 8);

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_read_csv_dtrt_fallback() {
    let test_dir = setup_test_workspace();
    let csv_path = test_dir.join("data.csv");

    let csv_content = "id,score,name\n1,98.5,Alice\n2,87.0,Bob\n3,,Charlie\n";
    fs::write(&csv_path, csv_content).unwrap();

    let reqs = vec![
        ("id", Some("int32"), None),
        ("score", Some("float32?"), None),
        ("name", Some("string"), None),
    ];

    let cols = read_text_column_data(&csv_path, &reqs).unwrap();

    let id_col = &cols["id"];
    assert_eq!(id_col.type_code, 0x03);
    assert_eq!(id_col.row_count, 3);

    let score_col = &cols["score"];
    assert_eq!(score_col.type_code, 0x86); // Nullable FLOAT32
    assert_eq!(score_col.null_count, 1);
    assert_eq!(score_col.row_count, 3);

    let name_col = &cols["name"];
    assert_eq!(name_col.type_code, 0x0B); // String
    assert_eq!(name_col.row_count, 3);

    let _ = fs::remove_dir_all(&test_dir);
}
