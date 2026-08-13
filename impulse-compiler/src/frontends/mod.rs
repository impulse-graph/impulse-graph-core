//! @file mod.rs
//! @brief Language Frontends Module (ImpK, ImpLog, ImpScheme).

pub mod impk;
pub mod implog;
pub mod cypher;

use super::ir::ast::SExpr;
use std::error::Error;

/// @enum LanguageTarget
/// @brief Source DSL input language for compilation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LanguageTarget {
    /// Homoiconic S-Expression AST IR
    ImpScm,
    /// Matrix & Vector Array Math DSL
    ImpK,
    /// Datalog Logic & ReBAC Rule DSL
    ImpLog,
    /// Simplified openCypher DSL
    Cypher,
}

/// @brief Parses source text of a given language target into ImpScheme IR AST expressions.
/// @param source Source code text.
/// @param target Language target enum variant.
/// @return `Result<Vec<SExpr>, Box<dyn Error>>`
pub fn parse_to_ir(source: &str, target: LanguageTarget) -> Result<Vec<SExpr>, Box<dyn Error>> {
    match target {
        LanguageTarget::ImpScm => {
            let exprs = super::ir::reader::parse(source)?;
            Ok(exprs)
        }
        LanguageTarget::ImpK => {
            let exprs = impk::parse(source)?;
            Ok(exprs)
        }
        LanguageTarget::ImpLog => {
            let exprs = implog::parse(source)?;
            Ok(exprs)
        }
        LanguageTarget::Cypher => {
            let query = cypher::parse_cypher(source)?;
            let ir = cypher::compile_cypher_to_impscm(&query);
            Ok(vec![ir])
        }
    }
}
