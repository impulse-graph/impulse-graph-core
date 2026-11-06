use impulse_graph::SnapshotReader;
use impulse_graph_tooling::commands;
use impulse_graph_tooling::GenerateArgs;
use std::fs;
use std::path::PathBuf;

fn setup_test_workspace() -> PathBuf {
    let dir = std::env::temp_dir().join(format!("impulse_gen_test_{}", rand::random::<u32>()));
    let _ = fs::create_dir_all(&dir);
    dir
}

#[test]
fn test_generate_graph500_rmat() {
    let test_dir = setup_test_workspace();
    let snapshot_path = test_dir.join("graph500_s8.imps");

    let args = GenerateArgs {
        profile: "graph500".to_string(),
        output: snapshot_path.clone(),
        format: "imps".to_string(),
        scale: 8,        // 256 vertices
        edge_factor: 16, // 256 * 16 = 4096 edges
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
        seed: Some(0x12345678),
        undirected: false,
        allow_self_loops: false,
        include_csc: false,
        domain_name: "Vertex".to_string(),
        tgt_domain_name: "Resource".to_string(),
        relation_name: "GRAPH500_EDGE".to_string(),
        attributes: None,
        chunk_size: 1_000_000,
        embedding_dim: None,
        embedding_attr: "embedding".to_string(),
        vector_index: None,
    };

    let res = commands::generate::run(&args);
    assert!(res.is_ok(), "Graph500 generation failed: {:?}", res);
    assert!(snapshot_path.exists(), "Snapshot file not generated");

    // Validate with SnapshotReader
    let reader = SnapshotReader::open(&snapshot_path).expect("Failed to open generated snapshot");
    assert_eq!(reader.header().domain_count(), 1);
    assert_eq!(reader.relation_count(), 1);

    let domain = &reader.domains()[0];
    assert_eq!(domain.name, "Vertex");
    assert_eq!(domain.node_count, 256);

    let rel = &reader.relations()[0];
    assert_eq!(rel.name, "GRAPH500_EDGE");
    assert_eq!(rel.node_count, 256);
    assert!(rel.edge_count > 0, "Edge count should be > 0");

    // Validate with toolchain validator
    let val_res = commands::validate::run(&snapshot_path, true, None);
    assert!(val_res.is_ok(), "Validation failed: {:?}", val_res);

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_generate_social_barabasi_albert() {
    let test_dir = setup_test_workspace();
    let snapshot_path = test_dir.join("social_ba.imps");

    let args = GenerateArgs {
        profile: "social".to_string(),
        output: snapshot_path.clone(),
        format: "imps".to_string(),
        scale: 10,
        edge_factor: 16,
        a: 0.57,
        b: 0.19,
        c: 0.19,
        d: 0.05,
        nodes: Some(500),
        edges: None,
        edges_per_node: 6,
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
        undirected: true,
        allow_self_loops: false,
        include_csc: false,
        domain_name: "User".to_string(),
        tgt_domain_name: "Resource".to_string(),
        relation_name: "FRIENDS_WITH".to_string(),
        attributes: None,
        chunk_size: 1_000_000,
        embedding_dim: None,
        embedding_attr: "embedding".to_string(),
        vector_index: None,
    };

    let res = commands::generate::run(&args);
    assert!(res.is_ok(), "Social BA generation failed: {:?}", res);

    let reader = SnapshotReader::open(&snapshot_path).expect("Failed to open snapshot");
    assert_eq!(reader.domains()[0].node_count, 500);
    assert_eq!(reader.relations()[0].name, "FRIENDS_WITH");

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_generate_social_sbm_communities() {
    let test_dir = setup_test_workspace();
    let snapshot_path = test_dir.join("social_sbm.imps");

    let args = GenerateArgs {
        profile: "social-sbm".to_string(),
        output: snapshot_path.clone(),
        format: "imps".to_string(),
        scale: 10,
        edge_factor: 16,
        a: 0.57,
        b: 0.19,
        c: 0.19,
        d: 0.05,
        nodes: Some(300),
        edges: None,
        edges_per_node: 8,
        p: None,
        communities: 5,
        p_intra: 0.05,
        p_inter: 0.001,
        dim_x: 32,
        dim_y: 32,
        dim_z: None,
        toroidal: false,
        branching: 3,
        depth: 6,
        star: false,
        src_nodes: None,
        tgt_nodes: None,
        seed: Some(999),
        undirected: true,
        allow_self_loops: false,
        include_csc: false,
        domain_name: "Member".to_string(),
        tgt_domain_name: "Resource".to_string(),
        relation_name: "COMMUNITY_EDGE".to_string(),
        attributes: None,
        chunk_size: 1_000_000,
        embedding_dim: None,
        embedding_attr: "embedding".to_string(),
        vector_index: None,
    };

    let res = commands::generate::run(&args);
    assert!(res.is_ok(), "Social SBM generation failed: {:?}", res);

    let reader = SnapshotReader::open(&snapshot_path).expect("Failed to open snapshot");
    assert_eq!(reader.domains()[0].node_count, 300);

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_generate_erdos_renyi() {
    let test_dir = setup_test_workspace();
    let snapshot_path = test_dir.join("erdos_renyi.imps");

    let args = GenerateArgs {
        profile: "erdos-renyi".to_string(),
        output: snapshot_path.clone(),
        format: "imps".to_string(),
        scale: 10,
        edge_factor: 16,
        a: 0.57,
        b: 0.19,
        c: 0.19,
        d: 0.05,
        nodes: Some(400),
        edges: Some(2500),
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
        seed: Some(777),
        undirected: false,
        allow_self_loops: false,
        include_csc: false,
        domain_name: "Node".to_string(),
        tgt_domain_name: "Resource".to_string(),
        relation_name: "RANDOM_EDGE".to_string(),
        attributes: None,
        chunk_size: 1_000_000,
        embedding_dim: None,
        embedding_attr: "embedding".to_string(),
        vector_index: None,
    };

    let res = commands::generate::run(&args);
    assert!(res.is_ok(), "Erdos-Renyi generation failed: {:?}", res);

    let reader = SnapshotReader::open(&snapshot_path).expect("Failed to open snapshot");
    assert_eq!(reader.domains()[0].node_count, 400);
    assert_eq!(reader.relations()[0].edge_count, 2500);

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_generate_grid_2d_and_3d() {
    let test_dir = setup_test_workspace();
    let grid2d_path = test_dir.join("grid_2d.imps");
    let grid3d_path = test_dir.join("grid_3d.imps");

    // 2D Grid: 20 x 20 = 400 nodes
    let args2d = GenerateArgs {
        profile: "grid".to_string(),
        output: grid2d_path.clone(),
        format: "imps".to_string(),
        scale: 10,
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
        dim_x: 20,
        dim_y: 20,
        dim_z: None,
        toroidal: true,
        branching: 3,
        depth: 6,
        star: false,
        src_nodes: None,
        tgt_nodes: None,
        seed: None,
        undirected: true,
        allow_self_loops: false,
        include_csc: false,
        domain_name: "GridNode".to_string(),
        tgt_domain_name: "Resource".to_string(),
        relation_name: "GRID_LINK".to_string(),
        attributes: None,
        chunk_size: 1_000_000,
        embedding_dim: None,
        embedding_attr: "embedding".to_string(),
        vector_index: None,
    };

    let res2d = commands::generate::run(&args2d);
    assert!(res2d.is_ok(), "2D Grid generation failed: {:?}", res2d);

    let reader2d = SnapshotReader::open(&grid2d_path).expect("Failed to open 2D grid snapshot");
    assert_eq!(reader2d.domains()[0].node_count, 400);

    // 3D Grid: 5 x 5 x 5 = 125 nodes
    let mut args3d = args2d.clone();
    args3d.output = grid3d_path.clone();
    args3d.dim_x = 5;
    args3d.dim_y = 5;
    args3d.dim_z = Some(5);
    args3d.toroidal = false;

    let res3d = commands::generate::run(&args3d);
    assert!(res3d.is_ok(), "3D Grid generation failed: {:?}", res3d);

    let reader3d = SnapshotReader::open(&grid3d_path).expect("Failed to open 3D grid snapshot");
    assert_eq!(reader3d.domains()[0].node_count, 125);

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_generate_tree_and_star() {
    let test_dir = setup_test_workspace();
    let tree_path = test_dir.join("tree.imps");
    let star_path = test_dir.join("star.imps");

    // Balanced 3-ary tree, depth 4 = (3^5 - 1) / 2 = 121 nodes
    let args_tree = GenerateArgs {
        profile: "tree".to_string(),
        output: tree_path.clone(),
        format: "imps".to_string(),
        scale: 10,
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
        depth: 4,
        star: false,
        src_nodes: None,
        tgt_nodes: None,
        seed: None,
        undirected: false,
        allow_self_loops: false,
        include_csc: false,
        domain_name: "TreeNode".to_string(),
        tgt_domain_name: "Resource".to_string(),
        relation_name: "PARENT_OF".to_string(),
        attributes: None,
        chunk_size: 1_000_000,
        embedding_dim: None,
        embedding_attr: "embedding".to_string(),
        vector_index: None,
    };

    let res_tree = commands::generate::run(&args_tree);
    assert!(res_tree.is_ok(), "Tree generation failed: {:?}", res_tree);

    let reader_tree = SnapshotReader::open(&tree_path).expect("Failed to open tree snapshot");
    assert_eq!(reader_tree.domains()[0].node_count, 121);
    assert_eq!(reader_tree.relations()[0].edge_count, 120);

    // Star topology: 100 nodes
    let mut args_star = args_tree.clone();
    args_star.output = star_path.clone();
    args_star.star = true;
    args_star.nodes = Some(100);

    let res_star = commands::generate::run(&args_star);
    assert!(res_star.is_ok(), "Star generation failed: {:?}", res_star);

    let reader_star = SnapshotReader::open(&star_path).expect("Failed to open star snapshot");
    assert_eq!(reader_star.domains()[0].node_count, 100);
    assert_eq!(reader_star.relations()[0].edge_count, 99);

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_generate_bipartite_multi_domain() {
    let test_dir = setup_test_workspace();
    let snapshot_path = test_dir.join("bipartite.imps");

    let args = GenerateArgs {
        profile: "bipartite".to_string(),
        output: snapshot_path.clone(),
        format: "imps".to_string(),
        scale: 10,
        edge_factor: 16,
        a: 0.57,
        b: 0.19,
        c: 0.19,
        d: 0.05,
        nodes: None,
        edges: Some(1500),
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
        src_nodes: Some(250),
        tgt_nodes: Some(350),
        seed: Some(101),
        undirected: false,
        allow_self_loops: false,
        include_csc: false,
        domain_name: "Account".to_string(),
        tgt_domain_name: "Device".to_string(),
        relation_name: "LOGGED_INTO".to_string(),
        attributes: None,
        chunk_size: 1_000_000,
        embedding_dim: None,
        embedding_attr: "embedding".to_string(),
        vector_index: None,
    };

    let res = commands::generate::run(&args);
    assert!(res.is_ok(), "Bipartite generation failed: {:?}", res);

    let reader = SnapshotReader::open(&snapshot_path).expect("Failed to open bipartite snapshot");
    assert_eq!(
        reader.header().domain_count(),
        2,
        "Bipartite graph MUST have 2 distinct domains"
    );
    assert_eq!(reader.domains()[0].name, "Account");
    assert_eq!(reader.domains()[0].node_count, 250);
    assert_eq!(reader.domains()[1].name, "Device");
    assert_eq!(reader.domains()[1].node_count, 350);
    assert_eq!(reader.relations()[0].src_domain_id, 0);
    assert_eq!(reader.relations()[0].tgt_domain_id, 1);
    assert_eq!(reader.relations()[0].edge_count, 1500);

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_generate_with_synthetic_attributes() {
    let test_dir = setup_test_workspace();
    let snapshot_path = test_dir.join("attrs_graph.imps");

    let args = GenerateArgs {
        profile: "erdos-renyi".to_string(),
        output: snapshot_path.clone(),
        format: "imps".to_string(),
        scale: 10,
        edge_factor: 16,
        a: 0.57,
        b: 0.19,
        c: 0.19,
        d: 0.05,
        nodes: Some(100),
        edges: Some(500),
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
        seed: Some(555),
        undirected: false,
        allow_self_loops: false,
        include_csc: false,
        domain_name: "Node".to_string(),
        tgt_domain_name: "Resource".to_string(),
        relation_name: "WEIGHTED_EDGE".to_string(),
        attributes: Some("weight:f32,timestamp:i64,type:i32".to_string()),
        chunk_size: 1_000_000,
        embedding_dim: None,
        embedding_attr: "embedding".to_string(),
        vector_index: None,
    };

    let res = commands::generate::run(&args);
    assert!(res.is_ok(), "Attributes generation failed: {:?}", res);

    let reader = SnapshotReader::open(&snapshot_path).expect("Failed to open snapshot");
    let rel = &reader.relations()[0];
    assert_eq!(rel.attributes.len(), 3, "Must have 3 attributes generated");
    assert_eq!(rel.attributes[0].name, "weight");
    assert_eq!(rel.attributes[0].type_code, 3); // f32
    assert_eq!(rel.attributes[1].name, "timestamp");
    assert_eq!(rel.attributes[1].type_code, 2); // i64
    assert_eq!(rel.attributes[2].name, "type");
    assert_eq!(rel.attributes[2].type_code, 1); // i32

    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_generate_delimited_tsv_csv_export() {
    let test_dir = setup_test_workspace();
    let tsv_path = test_dir.join("graph.tsv");
    let csv_path = test_dir.join("graph.csv");

    let mut args = GenerateArgs {
        profile: "erdos-renyi".to_string(),
        output: tsv_path.clone(),
        format: "tsv".to_string(),
        scale: 10,
        edge_factor: 16,
        a: 0.57,
        b: 0.19,
        c: 0.19,
        d: 0.05,
        nodes: Some(50),
        edges: Some(100),
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
        seed: Some(123),
        undirected: false,
        allow_self_loops: false,
        include_csc: false,
        domain_name: "Node".to_string(),
        tgt_domain_name: "Resource".to_string(),
        relation_name: "EDGE".to_string(),
        attributes: Some("weight:f32".to_string()),
        chunk_size: 1_000_000,
        embedding_dim: None,
        embedding_attr: "embedding".to_string(),
        vector_index: None,
    };

    let res_tsv = commands::generate::run(&args);
    assert!(res_tsv.is_ok(), "TSV generation failed: {:?}", res_tsv);
    assert!(tsv_path.exists());

    let tsv_content = fs::read_to_string(&tsv_path).expect("Failed to read TSV");
    assert!(tsv_content.starts_with("src\ttgt\tweight\n"));

    args.output = csv_path.clone();
    args.format = "csv".to_string();

    let res_csv = commands::generate::run(&args);
    assert!(res_csv.is_ok(), "CSV generation failed: {:?}", res_csv);
    assert!(csv_path.exists());

    let csv_content = fs::read_to_string(&csv_path).expect("Failed to read CSV");
    assert!(csv_content.starts_with("src,tgt,weight\n"));

    let _ = fs::remove_dir_all(&test_dir);
}
