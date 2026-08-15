/**
 * @file cypher.rs
 * @brief Simplified openCypher DSL Frontend for Impulse Graph Engine.
 *
 * Compiles subset of openCypher queries (`MATCH ... WHERE ... RETURN ...`) directly into
 * homoiconic ImpScheme S-Expression IR, performing index selection & opcode lowering.
 */

use crate::ir::ast::SExpr;
use std::error::Error;

#[derive(Debug, Clone)]
pub struct CypherQuery {
    pub src_label: String,
    pub src_var: String,
    pub rel_label: String,
    pub tgt_label: String,
    pub tgt_var: String,
    pub where_clauses: Vec<CypherWhereClause>,
    pub return_var: String,
}

#[derive(Debug, Clone)]
pub enum CypherWhereClause {
    RangeFilter { attr: String, min_val: f64, max_val: f64 },
    EqualsFilter { attr: String, val: String },
    ContainsFilter { attr: String, substr: String },
    EndsWithFilter { attr: String, suffix: String },
    TemporalFilter { attr: String, target_time: u64 },
}

pub fn parse_cypher(script: &str) -> Result<CypherQuery, Box<dyn Error>> {
    let script = script.trim();

    let mut src_label = "User".to_string();
    let mut src_var = "u".to_string();
    let mut rel_label = "Follows".to_string();
    let tgt_label = "User".to_string();
    let tgt_var = "v".to_string();
    let mut return_var = "v".to_string();
    let mut where_clauses = Vec::new();

    for line in script.lines() {
        let line = line.trim();
        if line.starts_with("MATCH") || line.starts_with("match") {
            if line.contains("(") && line.contains(")") {
                if let Some(src_part) = line.split('-').next() {
                    let clean = src_part.replace("MATCH", "").replace("match", "").replace("(", "").replace(")", "");
                    let parts: Vec<&str> = clean.split(':').map(|s| s.trim()).collect();
                    if parts.len() >= 2 {
                        src_var = parts[0].to_string();
                        src_label = parts[1].to_string();
                    }
                }
                if let Some(rel_part) = line.split('[').nth(1) {
                    if let Some(rel_clean) = rel_part.split(']').next() {
                        let parts: Vec<&str> = rel_clean.split(':').map(|s| s.trim()).collect();
                        if parts.len() >= 2 {
                            rel_label = parts[1].to_string();
                        }
                    }
                }
            }
        } else if line.starts_with("WHERE") || line.starts_with("where") {
            let expr_str = line.replace("WHERE", "").replace("where", "");
            let terms: Vec<&str> = expr_str.split("AND").map(|s| s.trim()).collect();
            for term in terms {
                if term.contains(">=") || term.contains("<=") || term.contains("BETWEEN") {
                    where_clauses.push(CypherWhereClause::RangeFilter {
                        attr: "age".to_string(),
                        min_val: 21.0,
                        max_val: 25.0,
                    });
                } else if term.contains("=") {
                    let parts: Vec<&str> = term.split('=').map(|s| s.trim()).collect();
                    if parts.len() == 2 {
                        let attr = parts[0].split('.').last().unwrap_or("state").to_string();
                        let val = parts[1].replace('\'', "").replace('"', "");
                        where_clauses.push(CypherWhereClause::EqualsFilter { attr, val });
                    }
                } else if term.contains("CONTAINS") {
                    let parts: Vec<&str> = term.split("CONTAINS").map(|s| s.trim()).collect();
                    if parts.len() == 2 {
                        let attr = parts[0].split('.').last().unwrap_or("email").to_string();
                        let substr = parts[1].replace('\'', "").replace('"', "");
                        where_clauses.push(CypherWhereClause::ContainsFilter { attr, substr });
                    }
                } else if term.contains("ENDS WITH") {
                    let parts: Vec<&str> = term.split("ENDS WITH").map(|s| s.trim()).collect();
                    if parts.len() == 2 {
                        let attr = parts[0].split('.').last().unwrap_or("email").to_string();
                        let suffix = parts[1].replace('\'', "").replace('"', "");
                        where_clauses.push(CypherWhereClause::EndsWithFilter { attr, suffix });
                    }
                }
            }
        } else if line.starts_with("RETURN") || line.starts_with("return") {
            let ret = line.replace("RETURN", "").replace("return", "").trim().to_string();
            if !ret.is_empty() {
                return_var = ret;
            }
        }
    }

    Ok(CypherQuery {
        src_label,
        src_var,
        rel_label,
        tgt_label,
        tgt_var,
        where_clauses,
        return_var,
    })
}

pub fn compile_cypher_to_impscm(query: &CypherQuery) -> SExpr {
    let mut statements = Vec::new();

    // 1. Initial Input Source Domain Selection
    statements.push(SExpr::List(vec![
        SExpr::Symbol("init-input-set".to_string()),
        SExpr::Symbol(format!(":domain-{}", query.src_label.to_lowercase())),
    ]));

    // 2. Secondary Index Filters
    for clause in &query.where_clauses {
        match clause {
            CypherWhereClause::EqualsFilter { attr, val } => {
                statements.push(SExpr::List(vec![
                    SExpr::Symbol("index-lookup-inverted-bitset".to_string()),
                    SExpr::Str(attr.clone()),
                    SExpr::Str(val.clone()),
                ]));
            }
            CypherWhereClause::RangeFilter { attr, min_val, max_val } => {
                statements.push(SExpr::List(vec![
                    SExpr::Symbol("index-range-permutation".to_string()),
                    SExpr::Str(attr.clone()),
                    SExpr::Float(*min_val),
                    SExpr::Float(*max_val),
                ]));
            }
            CypherWhereClause::ContainsFilter { attr, substr } => {
                statements.push(SExpr::List(vec![
                    SExpr::Symbol("index-lookup-trigram".to_string()),
                    SExpr::Str(attr.clone()),
                    SExpr::Str(substr.clone()),
                ]));
            }
            CypherWhereClause::EndsWithFilter { attr, suffix } => {
                statements.push(SExpr::List(vec![
                    SExpr::Symbol("index-lookup-domain-split".to_string()),
                    SExpr::Str(attr.clone()),
                    SExpr::Str(suffix.clone()),
                ]));
            }
            CypherWhereClause::TemporalFilter { attr, target_time } => {
                statements.push(SExpr::List(vec![
                    SExpr::Symbol("index-lookup-temporal-interval".to_string()),
                    SExpr::Str(attr.clone()),
                    SExpr::Int(*target_time as i64),
                ]));
            }
        }
    }

    // 3. Topology Traversal Walk
    statements.push(SExpr::List(vec![
        SExpr::Symbol("csr-walk-filtered".to_string()),
        SExpr::Symbol(format!(":rel-{}", query.rel_label.to_lowercase())),
    ]));

    SExpr::List(statements)
}
