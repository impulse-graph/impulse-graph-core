//! @file implog.rs
//! @brief ImpLog Datalog frontend DSL parser translating logic rules into ImpScheme IR S-Expressions.

use crate::ir::ast::SExpr;
use std::error::Error;
use std::fmt;

/// @struct ImpLogParseError
/// @brief Syntax error raised during ImpLog Datalog DSL parsing.
#[derive(Debug)]
pub struct ImpLogParseError {
    pub message: String,
}

impl fmt::Display for ImpLogParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "ImpLog Parse Error: {}", self.message)
    }
}

impl Error for ImpLogParseError {}

/// @brief Parses ImpLog Datalog rule source code into ImpScheme S-Expression AST nodes.
/// @param input ImpLog source string.
/// @return `Result<Vec<SExpr>, ImpLogParseError>`
pub fn parse(input: &str) -> Result<Vec<SExpr>, ImpLogParseError> {
    let mut exprs = Vec::new();
    let lines = input.lines();

    let mut fn_body = Vec::new();
    let fn_name = "implog_rule_query".to_string();
    let mut current_frontier = "frontier".to_string();

    for line in lines {
        let trimmed = line.trim();
        if trimmed.is_empty() || trimmed.starts_with("//") || trimmed.starts_with('%') || trimmed.starts_with('#') {
            continue;
        }

        if trimmed.starts_with(".decl ") {
            continue;
        }

        if trimmed.contains(":-") {
            let parts: Vec<&str> = trimmed.split(":-").collect();
            let head_str = parts[0].trim();
            let body_str = parts[1].trim().trim_end_matches('.');

            let head_expr = parse_fact(head_str)?;
            let body_terms = body_str.split(',').map(|s| s.trim()).collect::<Vec<&str>>();

            let mut walk_rel = "FOLLOWS".to_string();
            let mut is_negated = false;
            let mut _neg_var = String::new();

            for term in body_terms {
                if term.starts_with('!') || term.starts_with("not ") {
                    is_negated = true;
                    _neg_var = term.trim_start_matches('!').trim_start_matches("not ").trim().to_string();
                } else if term.contains('(') {
                    let term_name = term.split('(').next().unwrap_or("").trim();
                    if !term_name.is_empty() {
                        walk_rel = term_name.to_uppercase();
                    }
                }
            }

            let head_var = match head_expr {
                SExpr::List(ref l) if l.len() > 1 => l[1].to_string(),
                _ => "frontier".to_string(),
            };

            let eval_expr = if is_negated {
                SExpr::List(vec![
                    SExpr::Symbol("bitset:and-not".into()),
                    SExpr::List(vec![
                        SExpr::Symbol("g:walk-csr".into()),
                        SExpr::Symbol("g".into()),
                        SExpr::Symbol(current_frontier.clone()),
                        SExpr::Str(walk_rel),
                    ]),
                    SExpr::Symbol("visited".into()),
                ])
            } else {
                SExpr::List(vec![
                    SExpr::Symbol("g:walk-csr".into()),
                    SExpr::Symbol("g".into()),
                    SExpr::Symbol(current_frontier.clone()),
                    SExpr::Str(walk_rel),
                ])
            };

            current_frontier = head_var.clone();

            fn_body.push(SExpr::List(vec![
                SExpr::Symbol("set!".into()),
                SExpr::Symbol(head_var),
                eval_expr,
            ]));
        }
    }

    if fn_body.is_empty() {
        fn_body.push(SExpr::List(vec![
            SExpr::Symbol("set!".into()),
            SExpr::Symbol("frontier".into()),
            SExpr::List(vec![
                SExpr::Symbol("g:walk-csr".into()),
                SExpr::Symbol("g".into()),
                SExpr::Symbol("frontier".into()),
                SExpr::Str("DIRECT_MEMBER".into()),
            ]),
        ]));
    }

    fn_body.push(SExpr::List(vec![
        SExpr::Symbol("return".into()),
        SExpr::Symbol("#t".into()),
    ]));

    let init_stmt = SExpr::List(vec![
        SExpr::Symbol("let".into()),
        SExpr::List(vec![
            SExpr::List(vec![
                SExpr::Symbol("frontier".into()),
                SExpr::List(vec![
                    SExpr::Symbol("bitset:from".into()),
                    SExpr::Symbol("start".into()),
                ]),
            ]),
            SExpr::List(vec![
                SExpr::Symbol("visited".into()),
                SExpr::List(vec![
                    SExpr::Symbol("bitset:from".into()),
                    SExpr::Symbol("start".into()),
                ]),
            ]),
        ]),
    ]);

    let mut full_body = vec![init_stmt];
    full_body.extend(fn_body);

    let sig = vec![
        SExpr::Symbol(fn_name),
        SExpr::Symbol("g".into()),
        SExpr::Symbol("start".into()),
    ];

    let mut fn_def = vec![SExpr::Symbol("define-query".into()), SExpr::List(sig)];
    fn_def.extend(full_body);

    exprs.push(SExpr::List(fn_def));

    Ok(exprs)
}

fn parse_fact(s: &str) -> Result<SExpr, ImpLogParseError> {
    let trimmed = s.trim();
    if trimmed.contains('(') && trimmed.ends_with(')') {
        let parts: Vec<&str> = trimmed.split('(').collect();
        let name = parts[0].trim();
        let args_str = parts[1].trim_end_matches(')').trim();

        let mut list = vec![SExpr::Symbol(name.to_string())];
        for arg in args_str.split(',') {
            list.push(SExpr::Symbol(arg.trim().to_string()));
        }
        Ok(SExpr::List(list))
    } else {
        Ok(SExpr::Symbol(trimmed.to_string()))
    }
}
