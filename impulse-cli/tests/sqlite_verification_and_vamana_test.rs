use impulse_graph::hexio::{
    hexio_write_u16_le, hexio_write_u32_le, hexio_write_u64_le, recompute_header_crc16,
};
use impulse_graph::spec::{
    BaseDataType, KeyType, VamanaMetric, VamanaQuantType, IMP_INDEX_VAMANA_ANN,
};
use impulse_graph::vamana::{brute_force_ann_search, evaluate_ann_quality, VamanaBuildConfig};
use impulse_graph::{SnapshotReader, SnapshotWriter};
use impulse_graph_tooling::commands;
use impulse_graph_tooling::embeddings::{load_embeddings_from_path, write_fvecs, write_npy};
use impulse_graph_tooling::GenerateArgs;
use std::fs;
use std::path::PathBuf;

fn setup_test_workspace(prefix: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("{}_{}", prefix, rand::random::<u32>()));
    let _ = fs::create_dir_all(&dir);
    dir
}

#[test]
fn test_multi_format_embedding_loaders_roundtrip() {
    let dir = setup_test_workspace("impulse_emb_fmt_test");
    let n = 16usize;
    let dim = 8usize;
    let mut expected = Vec::with_capacity(n * dim);
    for i in 0..(n * dim) {
        expected.push(((i as f32) * 0.125) - 3.5);
    }

    // 1. NumPy .npy
    let npy_path = dir.join("vecs.npy");
    write_npy(&npy_path, &expected, n, dim).expect("write_npy failed");
    let loaded_npy = load_embeddings_from_path(&npy_path, None, false).expect("load .npy failed");
    assert_eq!(loaded_npy.num_vectors, n);
    assert_eq!(loaded_npy.dimension, dim);
    for (a, b) in loaded_npy.vectors.iter().zip(expected.iter()) {
        assert!((a - b).abs() < 1e-6);
    }

    // 2. TexMex .fvecs
    let fvecs_path = dir.join("vecs.fvecs");
    write_fvecs(&fvecs_path, &expected, n, dim).expect("write_fvecs failed");
    let loaded_fvecs =
        load_embeddings_from_path(&fvecs_path, None, false).expect("load .fvecs failed");
    assert_eq!(loaded_fvecs.num_vectors, n);
    assert_eq!(loaded_fvecs.dimension, dim);
    for (a, b) in loaded_fvecs.vectors.iter().zip(expected.iter()) {
        assert!((a - b).abs() < 1e-6);
    }

    // 3. JSONL (.jsonl)
    let jsonl_path = dir.join("vecs.jsonl");
    let mut jsonl_lines = String::new();
    for r in 0..n {
        let slice = &expected[r * dim..(r + 1) * dim];
        let vals: Vec<String> = slice.iter().map(|v| format!("{:.6}", v)).collect();
        jsonl_lines.push_str(&format!(
            "{{\"id\":{},\"embedding\":[{}]}}\n",
            r,
            vals.join(",")
        ));
    }
    fs::write(&jsonl_path, jsonl_lines).unwrap();
    let loaded_jsonl = load_embeddings_from_path(&jsonl_path, Some("embedding"), false)
        .expect("load .jsonl failed");
    assert_eq!(loaded_jsonl.num_vectors, n);
    assert_eq!(loaded_jsonl.dimension, dim);
    for (a, b) in loaded_jsonl.vectors.iter().zip(expected.iter()) {
        assert!((a - b).abs() < 1e-4);
    }

    // 4. CSV with bracketed vector column
    let csv_path = dir.join("vecs.csv");
    let mut csv_lines = String::from("id,embedding\n");
    for r in 0..n {
        let slice = &expected[r * dim..(r + 1) * dim];
        let vals: Vec<String> = slice.iter().map(|v| format!("{:.6}", v)).collect();
        csv_lines.push_str(&format!("{},\"[{}]\"\n", r, vals.join(",")));
    }
    fs::write(&csv_path, csv_lines).unwrap();
    let loaded_csv =
        load_embeddings_from_path(&csv_path, Some("embedding"), false).expect("load .csv failed");
    assert_eq!(loaded_csv.num_vectors, n);
    assert_eq!(loaded_csv.dimension, dim);

    // 5. HuggingFace .safetensors -> convert-tensors with Vamana index
    let st_path = dir.join("weights.safetensors");
    let header_json = format!(
        "{{\"weight\":{{\"dtype\":\"F32\",\"shape\":[{},{}],\"data_offsets\":[0,{}]}}}}",
        n,
        dim,
        n * dim * 4
    );
    let header_bytes = header_json.as_bytes();
    let mut st_bytes = Vec::new();
    st_bytes.extend_from_slice(&(header_bytes.len() as u64).to_le_bytes());
    st_bytes.extend_from_slice(header_bytes);
    for &v in &expected {
        st_bytes.extend_from_slice(&v.to_le_bytes());
    }
    fs::write(&st_path, st_bytes).unwrap();

    let st_imps_path = dir.join("weights.imps");
    let vspecs = vec!["ModelWeightNode.weight:R=8:L=24:metric=cosine:quant=sq8".to_string()];
    commands::safetensors::run_with_options(&st_path, &st_imps_path, "ModelWeightNode", &vspecs)
        .expect("safetensors convert-tensors with Vamana failed");

    let reader = SnapshotReader::open(&st_imps_path).unwrap();
    assert_eq!(reader.domains()[0].node_count, n as u64);
    let (st_rows, st_dim, _st_vecs) = reader.get_domain_float32_vectors(0, Some(0), None).unwrap();
    assert_eq!(st_rows, n);
    assert_eq!(st_dim, dim);
    assert!(reader
        .index_entries()
        .iter()
        .any(|e| e.index_type == IMP_INDEX_VAMANA_ANN));

    let _ = fs::remove_dir_all(&dir);
}

#[test]
fn test_generate_optimize_vamana_and_deep_audit() {
    let dir = setup_test_workspace("impulse_vamana_opt_test");
    let base_imps = dir.join("base_vec.imps");
    let opt_rcm_imps = dir.join("opt_rcm_vec.imps");
    let opt_bfs_imps = dir.join("opt_bfs_vec.imps");

    let gen_args = GenerateArgs {
        profile: "social-sbm".to_string(),
        output: base_imps.clone(),
        format: "imps".to_string(),
        scale: 8,
        edge_factor: 8,
        a: 0.57,
        b: 0.19,
        c: 0.19,
        d: 0.05,
        nodes: Some(240),
        edges: None,
        edges_per_node: 6,
        p: None,
        communities: 8,
        p_intra: 0.12,
        p_inter: 0.005,
        dim_x: 16,
        dim_y: 16,
        dim_z: None,
        toroidal: false,
        branching: 3,
        depth: 4,
        star: false,
        src_nodes: None,
        tgt_nodes: None,
        seed: Some(12345),
        undirected: true,
        allow_self_loops: false,
        include_csc: true,
        domain_name: "Doc".to_string(),
        tgt_domain_name: "Resource".to_string(),
        relation_name: "CITES".to_string(),
        attributes: Some("weight:f32".to_string()),
        chunk_size: 100_000,
        embedding_dim: Some(32),
        embedding_attr: "embedding".to_string(),
        vector_index: Some("Doc.embedding:R=16:L=48:metric=cosine:quant=sq8".to_string()),
    };

    commands::generate::run(&gen_args).expect("generate with embeddings and vamana failed");

    // Validate with quick_check, integrity_check, and deep_audit
    commands::validate::run_with_options(
        &base_imps,
        &commands::validate::ValidateOptions {
            strict_alignment: true,
            public_key: None,
            quick_check: true,
            deep_audit: false,
        },
    )
    .expect("quick_check failed");

    commands::validate::run_with_options(
        &base_imps,
        &commands::validate::ValidateOptions {
            strict_alignment: true,
            public_key: None,
            quick_check: false,
            deep_audit: true,
        },
    )
    .expect("deep_audit on base_imps failed");

    // Inspect in both JSON and table modes
    commands::inspect::run(&base_imps, "json", false).expect("inspect json failed");
    commands::inspect::run(&base_imps, "text", false).expect("inspect text failed");

    // Optimize with RCM + CSC
    commands::optimize::run_with_options(
        &base_imps,
        &opt_rcm_imps,
        &commands::optimize::OptimizeOptions {
            rcm: true,
            csc: true,
            ..Default::default()
        },
    )
    .expect("optimize --rcm failed");

    commands::validate::run_with_options(
        &opt_rcm_imps,
        &commands::validate::ValidateOptions {
            strict_alignment: true,
            public_key: None,
            quick_check: false,
            deep_audit: true,
        },
    )
    .expect("deep_audit on opt_rcm_imps failed");

    // Optimize with --vamana-locality + CSC
    commands::optimize::run_with_options(
        &base_imps,
        &opt_bfs_imps,
        &commands::optimize::OptimizeOptions {
            vamana_locality: true,
            csc: true,
            ..Default::default()
        },
    )
    .expect("optimize --vamana-locality failed");

    commands::validate::run_with_options(
        &opt_bfs_imps,
        &commands::validate::ValidateOptions {
            strict_alignment: true,
            public_key: None,
            quick_check: false,
            deep_audit: true,
        },
    )
    .expect("deep_audit on opt_bfs_imps failed");

    // Verify Vamana Recall@5 > 0.85 on the optimized snapshot
    let reader = SnapshotReader::open(&opt_bfs_imps).unwrap();
    let v_entry = reader
        .index_entries()
        .iter()
        .find(|e| e.index_type == IMP_INDEX_VAMANA_ANN)
        .expect("Vamana index entry missing after optimize");
    let (n_nodes, dim, vecs) = reader
        .get_domain_float32_vectors(0, Some(v_entry.attribute_index), None)
        .unwrap();
    assert_eq!(n_nodes, 240);
    assert_eq!(dim, 32);

    let mut recall_sum = 0.0;
    let num_q = 15usize;
    for q_idx in 0..num_q {
        let q = &vecs[q_idx * dim..(q_idx + 1) * dim];
        let truth = brute_force_ann_search(VamanaMetric::Cosine, &vecs, 240, dim, q, 5);
        let approx = reader.vamana_ann_search(v_entry, q, 5, 32).unwrap();
        let (r1, rk, _ndcg, _mrr) = evaluate_ann_quality(&approx, &truth, 5);
        assert!(r1 >= 0.0);
        recall_sum += rk;
    }
    let mean_recall = recall_sum / (num_q as f64);
    assert!(
        mean_recall >= 0.85,
        "Expected mean Recall@5 >= 0.85 after --vamana-locality optimize, got {:.4}",
        mean_recall
    );

    let _ = fs::remove_dir_all(&dir);
}

#[test]
fn test_hexio_ten_point_corruption_matrix_rejected_by_validator() {
    let n = 32usize;
    let dim = 8usize;
    let mut vec_bytes = Vec::with_capacity(n * dim * 4);
    for i in 0..(n * dim) {
        let v = ((i as f32) * 0.17).sin();
        vec_bytes.extend_from_slice(&v.to_le_bytes());
    }

    let mut row_offsets = Vec::with_capacity(n + 1);
    let mut col_indices = Vec::new();
    row_offsets.push(0u32);
    for u in 0..n {
        col_indices.push(((u + 1) % n) as u32);
        col_indices.push(((u + 3) % n) as u32);
        row_offsets.push(col_indices.len() as u32);
    }

    let mut writer = SnapshotWriter::new("__unused_hexio_matrix.imps");
    writer.add_domain(0, KeyType::Int64, "Item");
    writer.set_domain_node_count(0, n as u64);
    writer.add_relation(
        0,
        0,
        n as u64,
        col_indices.len() as u64,
        row_offsets,
        col_indices,
    );
    writer.set_relation_name(0, "LINK");
    writer.set_relation_include_csc(0, true);
    writer.add_attribute_to_domain(
        0,
        "embedding",
        BaseDataType::Float32 as u8,
        dim as u32,
        vec_bytes,
        None,
    );
    let vcfg = VamanaBuildConfig {
        max_degree_r: 8,
        ef_construction: 24,
        alpha: 1.2,
        metric: VamanaMetric::Cosine,
        quant_type: VamanaQuantType::Sq8,
        seed: 7,
    };
    writer
        .add_vamana_index(0, "embedding", "item_vamana", &vcfg)
        .unwrap();

    let clean_bytes = writer.finalize_to_bytes().unwrap();
    let clean_reader = SnapshotReader::from_bytes(clean_bytes.clone()).unwrap();
    let deep_opts = commands::validate::ValidateOptions {
        strict_alignment: false,
        public_key: None,
        quick_check: false,
        deep_audit: true,
    };
    commands::validate::validate_reader(&clean_reader, clean_bytes.len() as u64, &deep_opts)
        .expect("clean snapshot must pass deep_audit");

    let rel0 = &clean_reader.relations()[0];
    let csr_row_off = rel0.csr_row_off_offset as usize;
    let csr_col_off = rel0.csr_col_idx_offset as usize;
    let csc_col_off = rel0.csc_col_idx_offset as usize;
    let idx0 = &clean_reader.index_entries()[0];
    let vh = clean_reader.parse_vamana_header(idx0).unwrap();
    let vamana_hdr_off = idx0.data_offset as usize;
    let vamana_adj_off = vh.adj_slab_offset as usize;

    // 1. Bad header magic
    {
        let mut b = clean_bytes.clone();
        hexio_write_u32_le(&mut b, 0, 0xDEADBEEF).unwrap();
        assert!(SnapshotReader::from_bytes(b).is_err());
    }
    // 2. Bad header version
    {
        let mut b = clean_bytes.clone();
        hexio_write_u16_le(&mut b, 4, 0x0099).unwrap();
        assert!(SnapshotReader::from_bytes(b).is_err());
    }
    // 3. Corrupted header CRC16
    {
        let mut b = clean_bytes.clone();
        b[62] ^= 0xFF;
        assert!(SnapshotReader::from_bytes(b).is_err());
    }
    // 4. Out-of-bounds footer_directory_offset in header (offset 0x1E = 30) with recomputed CRC16
    {
        let mut b = clean_bytes.clone();
        let blen = b.len() as u64;
        hexio_write_u64_le(&mut b, 30, blen + 4096).unwrap();
        recompute_header_crc16(&mut b).unwrap();
        assert!(SnapshotReader::from_bytes(b).is_err());
    }
    // 5. Non-monotonic CSR row_offsets[1] > row_offsets[2]
    {
        let mut b = clean_bytes.clone();
        hexio_write_u32_le(&mut b, csr_row_off + 4, 999).unwrap();
        let len = b.len() as u64;
        let r = SnapshotReader::from_bytes(b).unwrap();
        assert!(commands::validate::validate_reader(&r, len, &deep_opts).is_err());
    }
    // 6. CSR target node ID out of bounds (>= node_count)
    {
        let mut b = clean_bytes.clone();
        hexio_write_u32_le(&mut b, csr_col_off, (n as u32) + 50).unwrap();
        let len = b.len() as u64;
        let r = SnapshotReader::from_bytes(b).unwrap();
        assert!(commands::validate::validate_reader(&r, len, &deep_opts).is_err());
    }
    // 7. CSC transpose multiset mismatch with CSR (caught by --deep-audit)
    {
        let mut b = clean_bytes.clone();
        hexio_write_u32_le(&mut b, csc_col_off, (n as u32) - 1).unwrap();
        let len = b.len() as u64;
        let r = SnapshotReader::from_bytes(b).unwrap();
        assert!(commands::validate::validate_reader(&r, len, &deep_opts).is_err());
    }
    // 8. Corrupted VamanaHeader magic ("VAMA" -> 0x00000000)
    {
        let mut b = clean_bytes.clone();
        hexio_write_u32_le(&mut b, vamana_hdr_off, 0).unwrap();
        let len = b.len() as u64;
        let r = SnapshotReader::from_bytes(b).unwrap();
        assert!(commands::validate::validate_reader(&r, len, &deep_opts).is_err());
    }
    // 9. Vamana medoid_node_id >= node_count
    {
        let mut b = clean_bytes.clone();
        hexio_write_u32_le(&mut b, vamana_hdr_off + 28, (n as u32) + 10).unwrap();
        let len = b.len() as u64;
        let r = SnapshotReader::from_bytes(b).unwrap();
        assert!(commands::validate::validate_reader(&r, len, &deep_opts).is_err());
    }
    // 10. Vamana node degree > max_degree_r in adj_slab
    {
        let mut b = clean_bytes.clone();
        hexio_write_u32_le(&mut b, vamana_adj_off, 999).unwrap();
        let len = b.len() as u64;
        let r = SnapshotReader::from_bytes(b).unwrap();
        assert!(commands::validate::validate_reader(&r, len, &deep_opts).is_err());
    }
}

#[test]
fn test_assemble_disassemble_multilane_and_vector_opcodes() {
    let dir = setup_test_workspace("impulse_asm_multilane_test");
    let impas_path = dir.join("multilane_vec.impas");
    let impb_path = dir.join("multilane_vec.impb");

    let asm_src = r#"; Multi-lane, Stream Shader & Vector ANN assembly test
.domain "Airport" = 0
.relation "FLIGHT" = 0
.attribute "embedding" = 0

OP_LOAD_INLINE_SET R1, 0
OP_CSR_WALK_STATE R2, R1, REL_0
OP_COO_WALK_STREAM R3, R1, REL_0
OP_STREAM_FUNC_BEGIN R0, 0
OP_STREAM_LOAD_SRC R4, 0
OP_STREAM_REDUCE_ARGMIN R5, R4, 0
OP_STREAM_FUNC_END
OP_LANE_INIT_SEEDS R10, R1, 8
OP_LANE_CSR_WALK R11, R10, REL_0
OP_LANE_CSC_WALK R12, R11, REL_0
OP_LANE_FILTER_QUORUM R13, R12, 2
OP_LANE_REDUCE_AND R14, R12
OP_LANE_REDUCE_ATTR R15, R12, ATTR_0, BINARY_OP_ARGMIN
OP_LANE_SET_COVER R16, R12, 4
OP_VECTOR_ANN_SEARCH R20, R1, 0, 10, 32
OP_VECTOR_RERANK_DOT R21, R20, R1, ATTR_0
OP_HALT
"#;
    fs::write(&impas_path, asm_src).unwrap();

    commands::assemble::run(&impas_path, &impb_path).expect("assemble failed");
    let bytes = fs::read(&impb_path).unwrap();
    assert_eq!(bytes.len(), 17 * 8);

    let disasm =
        commands::disassemble::disassemble_bytes(&bytes, None).expect("disassemble failed");
    assert!(disasm.contains("OP_LOAD_INLINE_SET"));
    assert!(disasm.contains("OP_CSR_WALK_STATE"));
    assert!(disasm.contains("OP_COO_WALK_STREAM"));
    assert!(disasm.contains("OP_STREAM_REDUCE_ARGMIN"));
    assert!(disasm.contains("OP_LANE_INIT_SEEDS"));
    assert!(disasm.contains("OP_LANE_CSR_WALK"));
    assert!(disasm.contains("OP_LANE_CSC_WALK"));
    assert!(disasm.contains("OP_LANE_FILTER_QUORUM"));
    assert!(disasm.contains("OP_LANE_REDUCE_AND"));
    assert!(disasm.contains("OP_LANE_REDUCE_ATTR"));
    assert!(disasm.contains("OP_LANE_SET_COVER"));
    assert!(disasm.contains("OP_VECTOR_ANN_SEARCH"));
    assert!(disasm.contains("OP_VECTOR_RERANK_DOT"));

    let _ = fs::remove_dir_all(&dir);
}
