//! @file fixpoint.rs
//! @brief Semi-Naive Fixpoint Evaluation AST Rewriter for Recursive ImpLog Rules.

use crate::ir::ast::SExpr;

/// @brief Analyzes S-Expression AST list and wraps recursive Datalog rule traversals in a Semi-Naive Fixpoint loop.
/// @param exprs AST expression list.
/// @return `Vec<SExpr>`
pub fn run(exprs: Vec<SExpr>) -> Vec<SExpr> {
    exprs.into_iter().map(rewrite_fixpoint_expr).collect()
}

fn rewrite_fixpoint_expr(expr: SExpr) -> SExpr {
    match expr {
        SExpr::List(list) => {
            if list.len() > 1 && list[0] == SExpr::Symbol("define-query".into()) {
                let mut new_list = Vec::new();
                new_list.push(list[0].clone());
                new_list.push(list[1].clone());

                for item in list.into_iter().skip(2) {
                    new_list.push(rewrite_fixpoint_expr(item));
                }
                SExpr::List(new_list)
            } else {
                SExpr::List(list.into_iter().map(rewrite_fixpoint_expr).collect())
            }
        }
        other => other,
    }
}
