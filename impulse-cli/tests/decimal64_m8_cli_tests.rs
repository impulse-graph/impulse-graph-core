use arrow_array::{Array, Decimal64Array};
use arrow_schema::DataType;
use impulse_graph::spec::{IMPULSE_NULLABLE_FLAG, IMPULSE_TYPE_MASK};
use impulse_graph::{SnapshotReader, SnapshotWriter};
use impulse_graph_tooling::commands;
use impulse_graph_tooling::commands::transmute::TransmuteArgs;
use parquet::arrow::arrow_reader::ParquetRecordBatchReaderBuilder;
use std::fs::{self, File};
use std::path::{Path, PathBuf};

fn make_temp_dir(tag: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!(
        "impulse_m8a_dec64_{}_{}",
        tag,
        rand::random::<u32>()
    ));
    let _ = fs::create_dir_all(&dir);
    dir
}

fn default_transmute_args(input: PathBuf, output: PathBuf) -> TransmuteArgs {
    TransmuteArgs {
        input,
        output,
        compact_widths: false,
        add_csc: None,
        drop_csc: None,
        infer_multiplicity: false,
        add_virtual_relation: None,
        rename_domain: None,
        rename_relation: None,
        rename_attr: None,
        drop_domain: None,
        drop_relation: None,
        drop_attr: None,
        set_meta: None,
        delete_meta: None,
        clear_meta: false,
        cast: vec![],
        rounding: "half-up".to_string(),
        on_overflow: "fail".to_string(),
    }
}

fn pack_i64(vals: &[i64]) -> Vec<u8> {
    let mut buf = Vec::with_capacity(vals.len() * 8);
    for &v in vals {
        buf.extend_from_slice(&v.to_le_bytes());
    }
    buf
}

fn pack_f64(vals: &[f64]) -> Vec<u8> {
    let mut buf = Vec::with_capacity(vals.len() * 8);
    for &v in vals {
        buf.extend_from_slice(&v.to_le_bytes());
    }
    buf
}

fn pack_nullable_i64(vals: &[Option<i64>]) -> Vec<u8> {
    let words = vals.len().div_ceil(64);
    let raw_bm = words * 8;
    let padded_bm = if raw_bm == 0 {
        0
    } else {
        (raw_bm + 127) & !127
    };
    let mut buf = vec![0u8; padded_bm];
    for (i, opt) in vals.iter().enumerate() {
        if let Some(v) = opt {
            buf[i / 8] |= 1u8 << (i % 8);
            buf.extend_from_slice(&v.to_le_bytes());
        } else {
            buf.extend_from_slice(&0i64.to_le_bytes());
        }
    }
    buf
}

fn unpack_i64(bytes: &[u8]) -> Vec<i64> {
    bytes
        .chunks_exact(8)
        .map(|c| i64::from_le_bytes(c.try_into().unwrap()))
        .collect()
}

fn unpack_f32(bytes: &[u8]) -> Vec<f32> {
    bytes
        .chunks_exact(4)
        .map(|c| f32::from_le_bytes(c.try_into().unwrap()))
        .collect()
}

fn unpack_f64(bytes: &[u8]) -> Vec<f64> {
    bytes
        .chunks_exact(8)
        .map(|c| f64::from_le_bytes(c.try_into().unwrap()))
        .collect()
}

fn unpack_nullable_i64(bytes: &[u8], row_count: usize) -> Vec<Option<i64>> {
    let raw_bm = row_count.div_ceil(64) * 8;
    let padded_bm = if raw_bm == 0 {
        0
    } else {
        (raw_bm + 127) & !127
    };
    let bm = &bytes[..raw_bm];
    let payload = &bytes[padded_bm..];
    (0..row_count)
        .map(|i| {
            let valid = (bm[i / 8] & (1u8 << (i % 8))) != 0;
            if valid {
                Some(i64::from_le_bytes(
                    payload[i * 8..(i + 1) * 8].try_into().unwrap(),
                ))
            } else {
                None
            }
        })
        .collect()
}

fn build_decimal_test_snapshot(path: &Path) {
    let mut w = SnapshotWriter::new(path.to_str().unwrap());
    w.add_domain(0, impulse_graph::spec::KeyType::Int32, "Account");
    w.set_domain_node_count(0, 6);

    // DECIMAL64(15, 4): 1.2345, -1.2345, 1.2355, -1.2355, 1.2349, -1.2349
    let bal_d4 = [12345i64, -12345, 12355, -12355, 12349, -12349];
    w.add_attribute_to_domain_with_meta(0, "balance", 0x15, 1, 15, 4, pack_i64(&bal_d4), None);

    // INT64 column
    let counts = [10i64, -25, 0, 999, 42, -1];
    w.add_attribute_to_domain(0, "count_i64", 0x04, 1, pack_i64(&counts), None);

    // FLOAT64 column (finite)
    let prices = [12.345f64, -12.345, 12.355, 0.0, 99.99, -0.005];
    w.add_attribute_to_domain(0, "legacy_price", 0x07, 1, pack_f64(&prices), None);

    // FLOAT64 column with NaN and Inf for nullable overflow tests
    let dirty = [
        1.5f64,
        f64::NAN,
        f64::INFINITY,
        -f64::INFINITY,
        999999.0,
        -2.5,
    ];
    w.add_attribute_to_domain(0, "dirty_f64", 0x07, 1, pack_f64(&dirty), None);

    // VAR_STRING column with exact high-precision base-10 strings (> 2^53)
    let strs = [
        "9007199254740993.01",
        "-9007199254740993.01",
        "0.00",
        "123.45",
        "-0.05",
        "42",
    ];
    let mut str_data = Vec::new();
    let mut str_offs = vec![0u32];
    for s in &strs {
        str_data.extend_from_slice(s.as_bytes());
        str_offs.push(str_data.len() as u32);
    }
    w.add_attribute_to_domain(0, "raw_amount", 0x0B, 1, str_data, Some(str_offs));

    // Relation TRANSFER with DECIMAL64(10, 2) edge attribute
    let row_offsets = vec![0u32, 2, 3, 3, 3, 3, 3];
    let col_indices = vec![1u32, 2, 3];
    w.add_relation(0, 0, 6, 3, row_offsets, col_indices);
    w.set_relation_name(0, "TRANSFER");
    let tx_amts = [10050i64, -2500, 99999];
    w.add_attribute_to_relation_with_meta(0, "amount", 0x15, 1, 10, 2, pack_i64(&tx_amts), None);

    w.finalize().expect("write test decimal snapshot");
}

#[test]
fn test_transmute_cast_decimal64_rounding_modes_and_matrix() {
    let dir = make_temp_dir("cast_matrix");
    let src = dir.join("src.imps");
    build_decimal_test_snapshot(&src);

    // 1. DECIMAL64(15, 4) -> DECIMAL64(16, 3) across all 3 rounding modes
    let cases = [
        ("half-up", [1235i64, -1235, 1236, -1236, 1235, -1235]),
        ("half-even", [1234i64, -1234, 1236, -1236, 1235, -1235]),
        ("trunc", [1234i64, -1234, 1235, -1235, 1234, -1234]),
    ];
    for (mode, expected) in &cases {
        let out = dir.join(format!("rescale_{}.imps", mode));
        let mut args = default_transmute_args(src.clone(), out.clone());
        args.cast = vec!["Account.balance:decimal64(16,3)".to_string()];
        args.rounding = mode.to_string();
        commands::transmute::run(&args).expect("rescale transmute failed");

        let r = SnapshotReader::open(&out).expect("open rescaled");
        let attr = r.domains()[0]
            .attributes
            .iter()
            .find(|a| a.name == "balance")
            .unwrap();
        assert_eq!(attr.type_code, 0x15);
        assert_eq!(attr.precision, 16);
        assert_eq!(attr.scale, 3);
        let raw = r.get_domain_attribute_data(0, "balance").unwrap();
        assert_eq!(unpack_i64(raw), expected);

        // Ensure untouched DECIMAL64 edge attribute kept its (10, 2) metadata!
        let rel_attr = &r.relations()[0].attributes[0];
        assert_eq!(rel_attr.type_code, 0x15);
        assert_eq!(rel_attr.precision, 10);
        assert_eq!(rel_attr.scale, 2);
    }

    // 2. Full mix/match cast matrix in one transmute pass:
    //    - Account.balance (DECIMAL64(15,4)) -> string (exact base-10)
    //    - Account.count_i64 (INT64) -> decimal64(10, 2)
    //    - Account.legacy_price (FLOAT64) -> decimal64(10, 2) (half-up)
    //    - Account.raw_amount (VAR_STRING > 2^53) -> decimal64(18, 2) (zero float drift!)
    //    - TRANSFER.amount (DECIMAL64(10, 2)) -> f32
    let out_mix = dir.join("mix.imps");
    let mut args = default_transmute_args(src.clone(), out_mix.clone());
    args.cast = vec![
        "Account.balance:string".to_string(),
        "Account.count_i64:decimal64(10,2)".to_string(),
        "Account.legacy_price:decimal64(10,2)".to_string(),
        "Account.raw_amount:decimal64(18,2)".to_string(),
        "TRANSFER.amount:f32".to_string(),
    ];
    args.rounding = "half-up".to_string();
    commands::transmute::run(&args).expect("mix cast failed");

    let r = SnapshotReader::open(&out_mix).expect("open mix");
    // balance -> VAR_STRING
    let bal_data = r.get_domain_attribute_data(0, "balance").unwrap();
    let bal_offs = r
        .get_domain_attribute_offsets(0, "balance")
        .unwrap()
        .unwrap();
    let bal_strs: Vec<&str> = (0..6)
        .map(|i| {
            std::str::from_utf8(&bal_data[bal_offs[i] as usize..bal_offs[i + 1] as usize]).unwrap()
        })
        .collect();
    assert_eq!(
        bal_strs,
        vec!["1.2345", "-1.2345", "1.2355", "-1.2355", "1.2349", "-1.2349"]
    );

    // count_i64 -> DECIMAL64(10, 2)
    let cnt_raw = r.get_domain_attribute_data(0, "count_i64").unwrap();
    assert_eq!(unpack_i64(cnt_raw), vec![1000, -2500, 0, 99900, 4200, -100]);

    // legacy_price -> DECIMAL64(10, 2) (half-up)
    let price_raw = r.get_domain_attribute_data(0, "legacy_price").unwrap();
    assert_eq!(unpack_i64(price_raw), vec![1235, -1235, 1236, 0, 9999, -1]);

    // raw_amount (exact > 2^53 string) -> DECIMAL64(18, 2)
    let raw_amt = r.get_domain_attribute_data(0, "raw_amount").unwrap();
    assert_eq!(
        unpack_i64(raw_amt),
        vec![
            900719925474099301i64,
            -900719925474099301i64,
            0,
            12345,
            -5,
            4200
        ]
    );

    // TRANSFER.amount -> f32
    let tx_f32 = unpack_f32(r.get_attribute_data(0, 0).unwrap());
    assert_eq!(tx_f32, vec![100.5f32, -25.0, 999.99]);

    // 3. DECIMAL64 -> f64 & DECIMAL64 -> i64
    let out_num = dir.join("dec_to_num.imps");
    let mut args_num = default_transmute_args(src.clone(), out_num.clone());
    args_num.cast = vec![
        "Account.balance:i64".to_string(),
        "TRANSFER.amount:f64".to_string(),
    ];
    commands::transmute::run(&args_num).expect("dec_to_num failed");
    let r_num = SnapshotReader::open(&out_num).unwrap();
    assert_eq!(
        unpack_i64(r_num.get_domain_attribute_data(0, "balance").unwrap()),
        vec![1, -1, 1, -1, 1, -1]
    );
    assert_eq!(
        unpack_f64(r_num.get_attribute_data(0, 0).unwrap()),
        vec![100.5, -25.0, 999.99]
    );

    // 4. Overflow & NaN handling:
    //    - Non-nullable target with --on-overflow fail -> Err
    let out_fail = dir.join("overflow_fail.imps");
    let mut args_fail = default_transmute_args(src.clone(), out_fail.clone());
    args_fail.cast = vec!["Account.dirty_f64:decimal64(6,2)".to_string()];
    args_fail.on_overflow = "fail".to_string();
    assert!(commands::transmute::run(&args_fail).is_err());

    //    - Non-nullable target with --on-overflow null -> Err (cannot null non-nullable)
    let mut args_nn_null = default_transmute_args(src.clone(), out_fail);
    args_nn_null.cast = vec!["Account.dirty_f64:decimal64(6,2)".to_string()];
    args_nn_null.on_overflow = "null".to_string();
    assert!(commands::transmute::run(&args_nn_null).is_err());

    //    - Nullable target with --on-overflow null -> OK, NaN / +/-Inf / 999999.0 become NULL!
    let out_null = dir.join("overflow_null.imps");
    let mut args_null = default_transmute_args(src.clone(), out_null.clone());
    args_null.cast = vec!["Account.dirty_f64:decimal64(6,2)?".to_string()];
    args_null.on_overflow = "null".to_string();
    commands::transmute::run(&args_null).expect("nullable overflow null failed");

    let r_null = SnapshotReader::open(&out_null).unwrap();
    let d_attr = r_null.domains()[0]
        .attributes
        .iter()
        .find(|a| a.name == "dirty_f64")
        .unwrap();
    assert_eq!(d_attr.type_code, 0x15 | IMPULSE_NULLABLE_FLAG);
    assert_eq!(d_attr.precision, 6);
    assert_eq!(d_attr.scale, 2);
    let raw_null = r_null.get_domain_attribute_data(0, "dirty_f64").unwrap();
    assert_eq!(
        unpack_nullable_i64(raw_null, 6),
        vec![Some(150), None, None, None, None, Some(-250)]
    );

    // Validate the nullable DECIMAL64 snapshot
    commands::validate::run(&out_null, true, None).expect("validate nullable dec64 failed");

    let _ = fs::remove_dir_all(&dir);
}

#[test]
fn test_decimal64_index_export_validate_diff_merge() {
    let dir = make_temp_dir("cli_ops");
    let snap_a = dir.join("a.imps");
    let snap_b = dir.join("b.imps");
    let snap_indexed = dir.join("indexed.imps");
    let snap_merged = dir.join("merged.imps");

    // Build snapshot A with nullable and non-nullable DECIMAL64(10, 2)
    {
        let mut w = SnapshotWriter::new(snap_a.to_str().unwrap());
        w.add_domain(0, impulse_graph::spec::KeyType::Int32, "Account");
        w.set_domain_node_count(0, 4);
        w.add_attribute_to_domain_with_meta(
            0,
            "balance",
            0x15,
            1,
            10,
            2,
            pack_i64(&[10050, -500, 0, 250099]),
            None,
        );
        w.add_attribute_to_domain_with_meta(
            0,
            "opt_fee",
            0x15 | IMPULSE_NULLABLE_FLAG,
            1,
            8,
            2,
            pack_nullable_i64(&[Some(125), None, Some(-50), Some(0)]),
            None,
        );
        w.add_relation(0, 0, 4, 2, vec![0, 1, 2, 2, 2], vec![1, 2]);
        w.set_relation_name(0, "PAYS");
        w.add_attribute_to_relation_with_meta(
            0,
            "amt",
            0x15,
            1,
            12,
            3,
            pack_i64(&[5000, -125]),
            None,
        );
        w.finalize().unwrap();
    }

    // Build snapshot B with a second domain & DECIMAL64(16, 4) for diff + merge
    {
        let mut w = SnapshotWriter::new(snap_b.to_str().unwrap());
        w.add_domain(0, impulse_graph::spec::KeyType::Int32, "Account");
        w.set_domain_node_count(0, 4);
        w.add_attribute_to_domain_with_meta(
            0,
            "balance",
            0x15,
            1,
            16,
            4,
            pack_i64(&[1005000, -50000, 0, 25009900]),
            None,
        );
        w.add_domain(1, impulse_graph::spec::KeyType::Int32, "Merchant");
        w.set_domain_node_count(1, 2);
        w.add_attribute_to_domain_with_meta(
            1,
            "volume",
            0x15,
            1,
            14,
            2,
            pack_i64(&[999900, 12345]),
            None,
        );
        w.add_relation(0, 0, 4, 1, vec![0, 0, 0, 1, 1], vec![3]);
        w.set_relation_name(0, "PAYS");
        w.finalize().unwrap();
    }

    // 1. Validate valid snapshot A
    commands::validate::run(&snap_a, true, None).expect("validate snap_a failed");

    // 2. Validate malformed DECIMAL64 (P=0, S>P, P=19) rejected cleanly
    for (label, p, s) in [("p0", 0u16, 0u8), ("p19", 19, 2), ("s_gt_p", 5, 6)] {
        let bad = dir.join(format!("bad_{}.imps", label));
        let mut w = SnapshotWriter::new(bad.to_str().unwrap());
        w.add_domain(0, impulse_graph::spec::KeyType::Int32, "Bad");
        w.set_domain_node_count(0, 1);
        w.add_attribute_to_domain_with_meta(0, "x", 0x15, 1, p, s, pack_i64(&[1]), None);
        w.finalize().unwrap();
        assert!(
            commands::validate::run(&bad, true, None).is_err(),
            "Expected malformed DECIMAL64({}, {}) to fail validate",
            p,
            s
        );
    }

    // 3. Validate malformed payload size (data_bytes != row_count * dim * 8) rejected cleanly
    {
        let bad_sz = dir.join("bad_size.imps");
        let mut w = SnapshotWriter::new(bad_sz.to_str().unwrap());
        w.add_domain(0, impulse_graph::spec::KeyType::Int32, "BadSz");
        w.set_domain_node_count(0, 3);
        w.add_attribute_to_domain_with_meta(0, "x", 0x15, 1, 10, 2, pack_i64(&[1, 2]), None);
        w.finalize().unwrap();
        assert!(commands::validate::run(&bad_sz, true, None).is_err());
    }

    // 4. Index add (ZoneMap + Permutation) on DECIMAL64(10, 2)
    commands::index::add(
        &snap_a,
        &snap_indexed,
        &[
            "Account.balance:zonemap".to_string(),
            "Account.balance:permutation".to_string(),
        ],
    )
    .expect("index add on DECIMAL64 failed");
    commands::validate::run(&snap_indexed, true, None).expect("validate indexed failed");
    let r_idx = SnapshotReader::open(&snap_indexed).unwrap();
    assert_eq!(r_idx.index_entries().len(), 2);
    let bal_attr = r_idx.domains()[0]
        .attributes
        .iter()
        .find(|a| a.name == "balance")
        .unwrap();
    assert_eq!(bal_attr.precision, 10);
    assert_eq!(bal_attr.scale, 2);

    // 5. Export CSV & verify exact base-10 formatting
    let csv_dir = dir.join("export_csv");
    commands::export::run(&snap_a, &csv_dir, "csv").expect("export csv failed");
    let dom_csv = fs::read_to_string(csv_dir.join("domains/Account.csv")).unwrap();
    let lines: Vec<&str> = dom_csv.lines().collect();
    assert_eq!(lines[0], "id,balance,opt_fee");
    assert_eq!(lines[1], "0,100.50,1.25");
    assert_eq!(lines[2], "1,-5.00,");
    assert_eq!(lines[3], "2,0.00,-0.50");
    assert_eq!(lines[4], "3,2500.99,0.00");

    let rel_csv = fs::read_to_string(csv_dir.join("relations/PAYS.csv")).unwrap();
    let rlines: Vec<&str> = rel_csv.lines().collect();
    assert_eq!(rlines[0], "src,tgt,amt");
    assert_eq!(rlines[1], "0,1,5.000");
    assert_eq!(rlines[2], "1,2,-0.125");

    // 6. Export Parquet & verify Arrow Decimal64(P, S) schema + exact values + NULLs
    let pq_dir = dir.join("export_pq");
    commands::export::run(&snap_a, &pq_dir, "parquet").expect("export parquet failed");
    let pq_file = File::open(pq_dir.join("domains/Account.parquet")).unwrap();
    let mut pq_reader = ParquetRecordBatchReaderBuilder::try_new(pq_file)
        .unwrap()
        .build()
        .unwrap();
    let batch = pq_reader.next().unwrap().unwrap();
    assert_eq!(
        batch.schema().field(1).data_type(),
        &DataType::Decimal64(10, 2)
    );
    assert_eq!(
        batch.schema().field(2).data_type(),
        &DataType::Decimal64(8, 2)
    );
    let bal_arr = batch
        .column(1)
        .as_any()
        .downcast_ref::<Decimal64Array>()
        .unwrap();
    assert_eq!(
        (0..4).map(|i| bal_arr.value(i)).collect::<Vec<_>>(),
        vec![10050, -500, 0, 250099]
    );
    let fee_arr = batch
        .column(2)
        .as_any()
        .downcast_ref::<Decimal64Array>()
        .unwrap();
    assert!(!fee_arr.is_null(0) && fee_arr.value(0) == 125);
    assert!(fee_arr.is_null(1));
    assert!(!fee_arr.is_null(2) && fee_arr.value(2) == -50);
    assert!(!fee_arr.is_null(3) && fee_arr.value(3) == 0);

    // 7. Diff (text & json) on DECIMAL64 snapshots
    commands::diff::run(&snap_a, &snap_b).expect("diff text failed");
    commands::diff::run_with_format(&snap_a, &snap_b, "json").expect("diff json failed");

    // 8. Merge snapshots and verify DECIMAL64(P, S) metadata preserved for base & delta domains
    commands::merge::run(&snap_a, &[&snap_b], &snap_merged).expect("merge failed");
    commands::validate::run(&snap_merged, true, None).expect("validate merged failed");
    let r_m = SnapshotReader::open(&snap_merged).unwrap();
    assert_eq!(r_m.domain_count(), 2);
    let acc_bal = r_m.domains()[0]
        .attributes
        .iter()
        .find(|a| a.name == "balance")
        .unwrap();
    assert_eq!(acc_bal.type_code & IMPULSE_TYPE_MASK, 0x15);
    assert_eq!(acc_bal.precision, 10);
    assert_eq!(acc_bal.scale, 2);
    let merch_vol = r_m.domains()[1]
        .attributes
        .iter()
        .find(|a| a.name == "volume")
        .unwrap();
    assert_eq!(merch_vol.type_code & IMPULSE_TYPE_MASK, 0x15);
    assert_eq!(merch_vol.precision, 14);
    assert_eq!(merch_vol.scale, 2);

    let _ = fs::remove_dir_all(&dir);
}

#[test]
fn test_m8a_audit_regressions_exact_f64_dim0_varstr_and_nullable_index_rejection() {
    let dir = make_temp_dir("audit_regressions");
    let src = dir.join("audit_src.imps");

    // Build snapshot with:
    //  - f64 values testing > 2^53 scaling (900719925474099.0 -> decimal64(18, 3)),
    //    exact tie without double-rounding (0.005 -> decimal64(10, 2)), and sci-notation (1e-5)
    //  - VAR_STRING with dimension == 0
    //  - Nullable DECIMAL64(10, 2) (0x95) for permutation/zonemap rejection
    {
        let mut w = SnapshotWriter::new(src.to_str().unwrap());
        w.add_domain(0, impulse_graph::spec::KeyType::Int32, "Audit");
        w.set_domain_node_count(0, 3);

        w.add_attribute_to_domain(
            0,
            "big_f64",
            0x07,
            1,
            pack_f64(&[900719925474099.0_f64, -900719925474099.0_f64, 1e-3_f64]),
            None,
        );
        w.add_attribute_to_domain(
            0,
            "tie_f64",
            0x07,
            1,
            pack_f64(&[0.005_f64, -0.005_f64, 0.015_f64]),
            None,
        );
        let mut sdata = Vec::new();
        let mut soffs = vec![0u32];
        for s in ["12.34", "-0.50", "1e-2"] {
            sdata.extend_from_slice(s.as_bytes());
            soffs.push(sdata.len() as u32);
        }
        // dimension == 0 for VAR_STRING
        w.add_attribute_to_domain(0, "dim0_str", 0x0B, 0, sdata, Some(soffs));

        w.add_attribute_to_domain_with_meta(
            0,
            "null_dec",
            0x15 | IMPULSE_NULLABLE_FLAG,
            1,
            10,
            2,
            pack_nullable_i64(&[Some(100), None, Some(200)]),
            None,
        );
        w.finalize().unwrap();
    }

    // Regression #1a: 900719925474099.0_f64 -> decimal64(18, 3) yields exact 900719925474099000 (0 drift)
    // and Regression #3: VAR_STRING with dimension == 0 casts cleanly to decimal64(10, 2)
    let out_big = dir.join("out_big.imps");
    let mut args_big = default_transmute_args(src.clone(), out_big.clone());
    args_big.cast = vec![
        "Audit.big_f64:decimal64(18,3)".to_string(),
        "Audit.dim0_str:decimal64(10,2)".to_string(),
    ];
    commands::transmute::run(&args_big).expect("exact f64 & dim0 varstr cast failed");
    let r_big = SnapshotReader::open(&out_big).unwrap();
    assert_eq!(
        unpack_i64(r_big.get_domain_attribute_data(0, "big_f64").unwrap()),
        vec![900719925474099000i64, -900719925474099000i64, 1i64]
    );
    assert_eq!(
        unpack_i64(r_big.get_domain_attribute_data(0, "dim0_str").unwrap()),
        vec![1234i64, -50i64, 1i64]
    );

    // Regression #1b: 0.005_f64 -> decimal64(10, 2) has zero double-rounding across all 3 rounding modes
    for (mode, expected) in [
        ("half-up", vec![1i64, -1, 2]),
        ("half-even", vec![0i64, 0, 2]),
        ("trunc", vec![0i64, 0, 1]),
    ] {
        let out_tie = dir.join(format!("out_tie_{}.imps", mode));
        let mut args_tie = default_transmute_args(src.clone(), out_tie.clone());
        args_tie.cast = vec!["Audit.tie_f64:decimal64(10,2)".to_string()];
        args_tie.rounding = mode.to_string();
        commands::transmute::run(&args_tie).expect("tie f64 cast failed");
        let r_tie = SnapshotReader::open(&out_tie).unwrap();
        assert_eq!(
            unpack_i64(r_tie.get_domain_attribute_data(0, "tie_f64").unwrap()),
            expected,
            "mismatch for rounding mode {}",
            mode
        );
    }

    // Regression #2: Nullable 0x95 DECIMAL64 rejected cleanly on permutation and zonemap index add
    let out_idx = dir.join("out_idx_fail.imps");
    assert!(
        commands::index::add(&src, &out_idx, &["Audit.null_dec:permutation".to_string()]).is_err()
    );
    assert!(commands::index::add(&src, &out_idx, &["Audit.null_dec:zonemap".to_string()]).is_err());

    let _ = fs::remove_dir_all(&dir);
}
