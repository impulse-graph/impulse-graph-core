//! @file printer.rs
//! @brief Formatter for emitting canonical ImpScheme S-Expression IR text.

use super::ast::SExpr;

/// @brief Formats a list of top-level S-Expressions into formatted ImpScheme text.
/// @param exprs Slice of S-Expression AST nodes.
/// @return Formatted `.impscm` text.
pub fn print_ir(exprs: &[SExpr]) -> String {
    let mut out = String::new();
    for (i, expr) in exprs.iter().enumerate() {
        if i > 0 {
            out.push('\n');
        }
        out.push_str(&format!("{}", expr));
    }
    out
}
