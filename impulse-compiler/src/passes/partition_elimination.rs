//! @file partition_elimination.rs
//! @brief Partition Elimination and Dead Branch Pruning Optimization Pass for ImpScheme IR.

use crate::ir::ast::SExpr;

/// @brief Runs Partition Elimination and Dead Branch Pruning Pass over ImpScheme AST expressions.
/// @param exprs AST expression vector.
/// @return Optimized AST expression vector.
pub fn run(exprs: Vec<SExpr>) -> Vec<SExpr> {
    exprs.into_iter().map(optimize_expr).collect()
}

fn optimize_expr(expr: SExpr) -> SExpr {
    match expr {
        SExpr::List(list) => {
            let opt_list: Vec<SExpr> = list.into_iter().map(optimize_expr).collect();
            prune_list(opt_list)
        }
        _ => expr,
    }
}

fn prune_list(list: Vec<SExpr>) -> SExpr {
    if list.is_empty() {
        return SExpr::List(list);
    }

    if let SExpr::Symbol(ref op) = list[0] {
        match op.as_str() {
            // Rule 1: Eliminate CSR Walk on known empty frontier set
            "g:walk-csr" | "g:walk-csr-filtered" => {
                if list.len() >= 3 {
                    if is_empty_set(&list[2]) {
                        return SExpr::List(vec![
                            SExpr::Symbol("bitset:empty".to_string()),
                        ]);
                    }
                }
            }
            // Rule 2: (bitset:and (bitset:empty) x) -> (bitset:empty)
            "bitset:and" | "&" => {
                if list.len() == 3 {
                    if is_empty_set(&list[1]) || is_empty_set(&list[2]) {
                        return SExpr::List(vec![
                            SExpr::Symbol("bitset:empty".to_string()),
                        ]);
                    }
                }
            }
            // Rule 3: (bitset:or (bitset:empty) x) -> x
            "bitset:or" | "|" => {
                if list.len() == 3 {
                    if is_empty_set(&list[1]) {
                        return list[2].clone();
                    }
                    if is_empty_set(&list[2]) {
                        return list[1].clone();
                    }
                }
            }
            // Rule 4: (bitset:and-not (bitset:empty) x) -> (bitset:empty)
            "bitset:and-not" => {
                if list.len() == 3 {
                    if is_empty_set(&list[1]) {
                        return SExpr::List(vec![
                            SExpr::Symbol("bitset:empty".to_string()),
                        ]);
                    }
                    if is_empty_set(&list[2]) {
                        return list[1].clone();
                    }
                }
            }
            _ => {}
        }
    }

    SExpr::List(list)
}

fn is_empty_set(expr: &SExpr) -> bool {
    match expr {
        SExpr::List(list) if !list.is_empty() => {
            if let SExpr::Symbol(ref sym) = list[0] {
                sym == "bitset:empty" || sym == "empty_set"
            } else {
                false
            }
        }
        SExpr::Symbol(sym) => sym == "empty_set" || sym == "nil" || sym == "#f",
        _ => false,
    }
}
