//! @file implog.rs
//! @brief ImpLog Datalog frontend DSL parser translating logic rules into ImpScheme IR S-Expressions.

use crate::ir::ast::SExpr;
use crate::passes::stratification::{validate_stratification, DependencyEdge};
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
    let mut dep_edges = Vec::new();

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

            let mut temporal_expr: Option<SExpr> = None;
            let mut record_expr: Option<SExpr> = None;
            let mut lattice_expr: Option<SExpr> = None;
            let mut vector_sim_expr: Option<SExpr> = None;
            let mut topk_expr: Option<SExpr> = None;

            let head_pred_name = match head_expr {
                SExpr::List(ref l) if !l.is_empty() => l[0].to_string(),
                SExpr::Symbol(ref s) => s.clone(),
                _ => "query".to_string(),
            };

            for term in body_terms {
                if term.starts_with("ValidAt(") || term.starts_with("valid_at(") {
                    // Stage 1: Temporal Datalog Extension
                    let args = extract_args(term);
                    if args.len() >= 3 {
                        temporal_expr = Some(SExpr::List(vec![
                            SExpr::Symbol("time:valid-at".into()),
                            SExpr::Symbol(args[0].clone()),
                            SExpr::Symbol(args[1].clone()),
                            SExpr::Symbol(args[2].clone()),
                        ]));
                    }
                } else if term.starts_with("CosineSim(") || term.starts_with("cosine_sim(") {
                    // Stage 4: Vector Datalog Extension
                    let args = extract_args(term);
                    if args.len() >= 3 {
                        vector_sim_expr = Some(SExpr::List(vec![
                            SExpr::Symbol("vector:cosine-sim".into()),
                            SExpr::Symbol(args[0].clone()),
                            SExpr::Symbol(args[1].clone()),
                            SExpr::Symbol(args[2].clone()),
                        ]));
                    }
                } else if term.starts_with("Choice(") || term.starts_with("top<") {
                    // Stage 5: Subsumption / Top-K Pruning Extension
                    let args = extract_args(term);
                    if !args.is_empty() {
                        topk_expr = Some(SExpr::List(vec![
                            SExpr::Symbol("topk:filter".into()),
                            SExpr::Symbol(args[0].clone()),
                        ]));
                    }
                } else if term.starts_with('!') || term.starts_with("not ") {
                    is_negated = true;
                    let raw_neg = term.trim_start_matches('!').trim_start_matches("not ").trim();
                    _neg_var = raw_neg.split('(').next().unwrap_or(raw_neg).trim().to_string();
                    dep_edges.push(DependencyEdge {
                        from: head_pred_name.clone(),
                        to: _neg_var.clone(),
                        is_negated: true,
                    });
                } else if term.contains('(') {
                    let term_name = term.split('(').next().unwrap_or("").trim();
                    if !term_name.is_empty() {
                        walk_rel = term_name.to_uppercase();
                        dep_edges.push(DependencyEdge {
                            from: head_pred_name.clone(),
                            to: term_name.to_string(),
                            is_negated: false,
                        });
                    }
                }
            }

            // Stage 2: Structural Datalog Extension (Record constructors in head)
            if head_str.contains('{') && head_str.contains('}') {
                let struct_str = head_str.split('{').nth(1).unwrap_or("").split('}').next().unwrap_or("").trim();
                record_expr = Some(SExpr::List(vec![
                    SExpr::Symbol("record:create".into()),
                    SExpr::Str(struct_str.to_string()),
                ]));
            }

            // Stage 3: Lattice Datalog Head Reduction
            if head_str.contains("min<") || head_str.contains("max<") || head_str.contains("sum<") {
                let op = if head_str.contains("min<") { "min" } else if head_str.contains("max<") { "max" } else { "sum" };
                lattice_expr = Some(SExpr::List(vec![
                    SExpr::Symbol("lattice:reduce".into()),
                    SExpr::Symbol(op.into()),
                ]));
            }

            let head_var = match head_expr {
                SExpr::List(ref l) if l.len() > 1 => l[1].to_string(),
                _ => "frontier".to_string(),
            };

            let mut eval_expr = if is_negated {
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

            // Wrap with Stage 1 Temporal Filter if present
            if let Some(temp) = temporal_expr {
                eval_expr = SExpr::List(vec![
                    SExpr::Symbol("temporal:filter".into()),
                    eval_expr,
                    temp,
                ]);
            }

            // Wrap with Stage 4 Vector Sim Filter if present
            if let Some(vsim) = vector_sim_expr {
                eval_expr = SExpr::List(vec![
                    SExpr::Symbol("vector:filter".into()),
                    eval_expr,
                    vsim,
                ]);
            }

            // Wrap with Stage 5 Top-K Filter if present
            if let Some(topk) = topk_expr {
                eval_expr = SExpr::List(vec![
                    SExpr::Symbol("topk:prune".into()),
                    eval_expr,
                    topk,
                ]);
            }

            // Wrap with Stage 3 Lattice Reduction if present
            if let Some(lat) = lattice_expr {
                eval_expr = SExpr::List(vec![
                    SExpr::Symbol("lattice:fold".into()),
                    eval_expr,
                    lat,
                ]);
            }

            // Wrap with Stage 2 Record Construction if present
            if let Some(rec) = record_expr {
                eval_expr = SExpr::List(vec![
                    SExpr::Symbol("record:wrap".into()),
                    eval_expr,
                    rec,
                ]);
            }

            current_frontier = head_var.clone();

            fn_body.push(SExpr::List(vec![
                SExpr::Symbol("set!".into()),
                SExpr::Symbol(head_var),
                eval_expr,
            ]));
        }
    }

    // Validate Stratified Negation (no negative cycles)
    if let Err(err_msg) = validate_stratification(&dep_edges) {
        return Err(ImpLogParseError { message: err_msg });
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

fn extract_args(term: &str) -> Vec<String> {
    if let Some(inside) = term.split('(').nth(1) {
        let clean = inside.trim_end_matches(')').trim();
        clean.split(',').map(|s| s.trim().to_string()).collect()
    } else {
        Vec::new()
    }
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
