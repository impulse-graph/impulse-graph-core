use impulse_graph::SnapshotReader;
use impulse_graph_tooling::commands;
use impulse_graph_tooling::GenerateArgs;
use std::fs;
use std::path::PathBuf;

fn setup_test_workspace(sub: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!(
        "impulse_transmute_test_{}_{}",
        sub,
        rand::random::<u32>()
    ));
    let _ = fs::create_dir_all(&dir);
    dir
}

#[test]
fn test_transmute_compact_csc_meta_rename_virtual() {
    let test_dir = setup_test_workspace("full");
    let orig_imps = test_dir.join("orig.imps");
    let transmuted_imps = test_dir.join("transmuted.imps");

    // 1. Generate base snapshot with CSC disabled
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
        nodes: Some(50),
        edges: Some(100),
        edges_per_node: 2,
        p: Some(0.05),
        communities: 2,
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
        include_csc: false,
        domain_name: "Node".to_string(),
        tgt_domain_name: "Node".to_string(),
        relation_name: "CONNECTS".to_string(),
        attributes: None,
        chunk_size: 1_000_000,
        embedding_dim: None,
        embedding_attr: "embedding".to_string(),
        vector_index: None,
    };
    commands::generate::run(&args).expect("generate failed");

    let r_orig = SnapshotReader::open(&orig_imps).expect("open orig failed");
    assert_eq!(r_orig.domain_count(), 1);
    assert_eq!(r_orig.relation_count(), 1);
    assert!(!r_orig.relations()[0].has_csc());
    assert_eq!(r_orig.relations()[0].node_id_width, 4);

    // 2. Transmute: compact widths, add CSC, infer multiplicity, rename, add virtual relation, set metadata
    let t_args = commands::transmute::TransmuteArgs {
        input: orig_imps.clone(),
        output: transmuted_imps.clone(),
        compact_widths: true,
        add_csc: Some(vec!["all".to_string()]),
        drop_csc: None,
        infer_multiplicity: true,
        add_virtual_relation: Some(vec!["TWO_HOP=LINKS,LINKS".to_string()]),
        rename_domain: Some(vec!["Node=Vertex".to_string()]),
        rename_relation: Some(vec!["CONNECTS=LINKS".to_string()]),
        rename_attr: None,
        drop_domain: None,
        drop_relation: None,
        drop_attr: None,
        set_meta: Some(vec![
            "author=impulse_team".to_string(),
            "dataset=test_net".to_string(),
        ]),
        delete_meta: None,
        clear_meta: false,
    };
    commands::transmute::run(&t_args).expect("transmute failed");

    // 3. Verify transmuted snapshot
    let r_trans = SnapshotReader::open(&transmuted_imps).expect("open transmuted failed");
    assert_eq!(r_trans.domain_count(), 1);
    assert_eq!(r_trans.domains()[0].name, "Vertex");

    // We should have LINKS (renamed from CONNECTS) and TWO_HOP (virtual)
    assert_eq!(r_trans.relation_count(), 2);
    let rel_links = &r_trans.relations()[0];
    assert_eq!(rel_links.name, "LINKS");
    assert!(rel_links.has_csc(), "CSC should be enabled");
    assert_eq!(
        rel_links.node_id_width, 2,
        "node_id_width should be compacted to u16"
    );

    let rel_two_hop = &r_trans.relations()[1];
    assert_eq!(rel_two_hop.name, "TWO_HOP");
    assert_eq!(rel_two_hop.node_count, 50);

    // Verify metadata
    let meta = r_trans.get_metadata().expect("get metadata failed");
    assert_eq!(meta.get("author").map(|s| s.as_str()), Some("impulse_team"));
    assert_eq!(meta.get("dataset").map(|s| s.as_str()), Some("test_net"));
    assert!(
        meta.contains_key("multiplicity.LINKS"),
        "Multiplicity for LINKS must be set in metadata"
    );

    // 4. Validate transmuted snapshot with deep audit
    let val_opts = commands::validate::ValidateOptions {
        strict_alignment: true,
        deep_audit: true,
        public_key: None,
        quick_check: false,
    };
    commands::validate::run_with_options(&transmuted_imps, &val_opts).expect("validate failed");

    // 5. Test Sample on transmuted snapshot
    commands::sample::run(&transmuted_imps, Some("Vertex"), None, 5, 0, "text")
        .expect("sample text failed");
    commands::sample::run(&transmuted_imps, None, Some("LINKS"), 5, 0, "json")
        .expect("sample json failed");

    let _ = fs::remove_dir_all(&test_dir);
}
