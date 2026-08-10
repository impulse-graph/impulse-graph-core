//! @file vector_fusion.rs
//! @brief Vector Fusion optimization pass for fusing bitset operators and CSR walks in ImpScheme IR.

use crate::ir::ast::SExpr;

/// @brief Runs Vector Fusion optimization pass over ImpScheme AST expressions.
/// @param exprs AST expression vector.
/// @return Optimized AST expression vector.
pub fn run(exprs: Vec<SExpr>) -> Vec<SExpr> {
    exprs.into_iter().map(optimize_expr).collect()
}

fn optimize_expr(expr: SExpr) -> SExpr {
    match expr {
        SExpr::List(list) => {
            let opt_list: Vec<SExpr> = list.into_iter().map(optimize_expr).collect();
            fuse_list(opt_list)
        }
        _ => expr,
    }
}

fn fuse_list(list: Vec<SExpr>) -> SExpr {
    if list.len() == 3 {
        if let SExpr::Symbol(ref op) = list[0] {
            if op == "bitset:and" || op == "bitset_and" || op == "&" {
                let left = &list[1];
                let right = &list[2];

                // Rule 1: (bitset:and a (bitset:not b)) -> (bitset:and-not a b)
                if let SExpr::List(ref r_sub) = right {
                    if r_sub.len() == 2 {
                        if let SExpr::Symbol(ref r_op) = r_sub[0] {
                            if r_op == "bitset:not" || r_op == "bitset_not" || r_op == "~" {
                                return SExpr::List(vec![
                                    SExpr::Symbol("bitset:and-not".to_string()),
                                    left.clone(),
                                    r_sub[1].clone(),
                                ]);
                            }
                        }
                    }
                }

                // Rule 2: Commutative (bitset:and (bitset:not b) a) -> (bitset:and-not a b)
                if let SExpr::List(ref l_sub) = left {
                    if l_sub.len() == 2 {
                        if let SExpr::Symbol(ref l_op) = l_sub[0] {
                            if l_op == "bitset:not" || l_op == "bitset_not" || l_op == "~" {
                                return SExpr::List(vec![
                                    SExpr::Symbol("bitset:and-not".to_string()),
                                    right.clone(),
                                    l_sub[1].clone(),
                                ]);
                            }
                        }
                    }
                }

                // Rule 3: Fuse CSR Walk + Filter Bitset (bitset:and (g:walk-csr g f rel) mask)
                if let SExpr::List(ref l_sub) = left {
                    if l_sub.len() == 4 {
                        if let SExpr::Symbol(ref l_op) = l_sub[0] {
                            if l_op == "g:walk-csr" || l_op == "walk_csr" {
                                return SExpr::List(vec![
                                    SExpr::Symbol("g:walk-csr-filtered".to_string()),
                                    l_sub[1].clone(),
                                    l_sub[2].clone(),
                                    l_sub[3].clone(),
                                    right.clone(),
                                ]);
                            }
                        }
                    }
                }
            }
        }
    }

    SExpr::List(list)
}
