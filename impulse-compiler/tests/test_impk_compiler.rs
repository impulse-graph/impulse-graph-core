use impulse_compiler::{compile_impk_to_impas, compile_script_to_impas, LanguageTarget};

#[test]
fn test_compile_impk_query_basic() {
    let source = r#"
        fn reachability(g, start) {
            frontier: bitset start;
            visited: bitset start;
            next: g @[frontier; "FOLLOWS"];
            return next;
        }
    "#;

    let result = compile_impk_to_impas(source);
    assert!(result.is_ok(), "ImpK compilation failed: {:?}", result);

    let asm = result.unwrap();
    assert!(asm.contains("reachability:"));
    assert!(asm.contains("OP_ENTER_FRAME"));
    assert!(asm.contains("OP_CSR_WALK"));
    assert!(asm.contains("OP_HALT"));
}

#[test]
fn test_compile_impk_with_bitset_fusion() {
    let source = r#"
        fn filtered_search(g, start) {
            frontier: bitset start;
            mask: bitset start;
            next: (g @[frontier; "FOLLOWS"]) &~ mask;
            return next;
        }
    "#;

    let result = compile_script_to_impas(source, LanguageTarget::ImpK);
    assert!(result.is_ok());

    let asm = result.unwrap();
    assert!(asm.contains("filtered_search:"));
    assert!(asm.contains("OP_ENTER_FRAME"));
}

#[test]
fn test_compile_impk_matrix_vector_product() {
    let source = r#"
        fn mxv_step(A, x) {
            y = A * x;
            return y;
        }
    "#;

    let result = compile_impk_to_impas(source);
    assert!(result.is_ok(), "ImpK SpMV compilation failed: {:?}", result);
}

#[test]
fn test_compile_impk_ewise_math_and_reduction() {
    let source = r#"
        fn ewise_reduce(v1, v2) {
            v3 = v1 + v2;
            total = sum(v3);
            return total;
        }
    "#;

    let result = compile_impk_to_impas(source);
    assert!(result.is_ok(), "ImpK EWISE math & reduction failed: {:?}", result);
}

#[test]
fn test_compile_impk_adaptive_walk() {
    let source = r#"
        fn adaptive_step(g, frontier) {
            next: g @adaptive[frontier; "FOLLOWS"];
            return next;
        }
    "#;

    let result = compile_impk_to_impas(source);
    assert!(result.is_ok(), "ImpK adaptive walk failed: {:?}", result);
}

#[test]
fn test_compile_impk_pagerank_full_script() {
    let source = r#"
        fn pagerank_step(A, p, active_frontier) {
            p_next = A * p;
            p_filtered = p_next <active_frontier>;
            loss = sum(p_filtered);
            return loss;
        }
    "#;

    let result = compile_impk_to_impas(source);
    assert!(result.is_ok(), "Full PageRank ImpK script compilation failed: {:?}", result);
}

#[test]
fn test_compile_impk_gapbs_bfs() {
    let source = r#"
        fn gapbs_bfs(g, start, visited) {
            frontier: bitset start;
            next: (g @adaptive[frontier; "FOLLOWS"]) &~ visited;
            return next;
        }
    "#;
    let result = compile_impk_to_impas(source);
    assert!(result.is_ok());
    assert!(result.unwrap().contains("gapbs_bfs:"));
}

#[test]
fn test_compile_impk_gapbs_pagerank() {
    let source = r#"
        fn gapbs_pagerank(A, p_prev, active_nodes) {
            p_next = A * p_prev;
            p_masked = p_next <active_nodes>;
            loss = sum(p_masked);
            return loss;
        }
    "#;
    let result = compile_impk_to_impas(source);
    assert!(result.is_ok());
    assert!(result.unwrap().contains("gapbs_pagerank:"));
}

#[test]
fn test_compile_impk_gapbs_cc() {
    let source = r#"
        fn gapbs_connected_components(A, parent) {
            parent_next = A * parent;
            return parent_next;
        }
    "#;
    let result = compile_impk_to_impas(source);
    assert!(result.is_ok());
    assert!(result.unwrap().contains("gapbs_connected_components:"));
}

#[test]
fn test_compile_impk_gapbs_sssp() {
    let source = r#"
        fn gapbs_sssp(A, dist) {
            dist_next = A * dist;
            return dist_next;
        }
    "#;
    let result = compile_impk_to_impas(source);
    assert!(result.is_ok());
    assert!(result.unwrap().contains("gapbs_sssp:"));
}

#[test]
fn test_compile_impk_gapbs_bc() {
    let source = r#"
        fn gapbs_betweenness_centrality(A, delta, frontier) {
            delta_next = (A * delta) <frontier>;
            return delta_next;
        }
    "#;
    let result = compile_impk_to_impas(source);
    assert!(result.is_ok());
    assert!(result.unwrap().contains("gapbs_betweenness_centrality:"));
}

#[test]
fn test_compile_impk_gapbs_tc() {
    let source = r#"
        fn gapbs_triangle_counting(A_upper, A_lower) {
            prod = A_upper * A_lower;
            triangles = sum(prod);
            return triangles;
        }
    "#;
    let result = compile_impk_to_impas(source);
    assert!(result.is_ok());
    assert!(result.unwrap().contains("gapbs_triangle_counting:"));
}
