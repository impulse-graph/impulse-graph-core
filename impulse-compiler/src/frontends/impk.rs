//! @file impk.rs
//! @brief ImpK frontend DSL parser translating array/vector expressions into ImpScheme IR S-Expressions.

use crate::ir::ast::SExpr;
use std::error::Error;
use std::fmt;

/// @struct ImpKParseError
/// @brief Syntax error raised during ImpK DSL parsing.
#[derive(Debug)]
pub struct ImpKParseError {
    pub message: String,
}

impl fmt::Display for ImpKParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "ImpK Parse Error: {}", self.message)
    }
}

impl Error for ImpKParseError {}

/// @brief Parses ImpK DSL source code into ImpScheme S-Expression AST nodes.
/// @param input ImpK source code string.
/// @return `Result<Vec<SExpr>, ImpKParseError>`
pub fn parse(input: &str) -> Result<Vec<SExpr>, ImpKParseError> {
    let mut exprs = Vec::new();
    let lines = input.lines();

    let mut current_fn_name = "impk_query".to_string();
    let mut current_args = Vec::new();
    let mut fn_body = Vec::new();
    let mut in_fn = false;

    for line in lines {
        let trimmed = line.trim();
        if trimmed.is_empty() || trimmed.starts_with('/') || trimmed.starts_with(';') {
            continue;
        }

        if (trimmed.starts_with("fn ") || trimmed.starts_with("query ")) && trimmed.contains('(') {
            in_fn = true;
            let parts: Vec<&str> = trimmed.split('(').collect();
            let name_part = parts[0].trim();
            current_fn_name = name_part
                .trim_start_matches("fn ")
                .trim_start_matches("query ")
                .trim()
                .to_string();

            if parts.len() > 1 {
                let arg_part = parts[1].split(')').next().unwrap_or("");
                current_args = arg_part
                    .split(',')
                    .map(|s| s.trim())
                    .filter(|s| !s.is_empty())
                    .map(|s| SExpr::Symbol(s.to_string()))
                    .collect();
            }
            continue;
        }

        if trimmed == "}" {
            if in_fn {
                let has_return = fn_body.iter().any(|e| match e {
                    SExpr::List(l) if !l.is_empty() => l[0] == SExpr::Symbol("return".into()),
                    _ => false,
                });
                if !has_return {
                    fn_body.push(SExpr::List(vec![
                        SExpr::Symbol("return".into()),
                        SExpr::Int(0),
                    ]));
                }

                let mut sig = vec![SExpr::Symbol(current_fn_name.clone())];
                sig.extend(current_args.clone());

                let mut fn_def = vec![SExpr::Symbol("define-query".into()), SExpr::List(sig)];
                fn_def.extend(fn_body.clone());

                exprs.push(SExpr::List(fn_def));
                fn_body.clear();
                current_args.clear();
                in_fn = false;
            }
            continue;
        }

        if trimmed.starts_with("return ") {
            let val_str = trimmed.trim_start_matches("return ").trim_end_matches(';').trim();
            fn_body.push(SExpr::List(vec![
                SExpr::Symbol("return".into()),
                parse_impk_expr(val_str)?,
            ]));
            continue;
        }

        if trimmed.contains(':') && !trimmed.starts_with("//") {
            let parts: Vec<&str> = trimmed.splitn(2, ':').collect();
            let var_name = parts[0].trim().trim_start_matches("let ").trim();
            let expr_str = parts[1].trim_end_matches(';').trim();

            let value_expr = parse_impk_expr(expr_str)?;
            let stmt = SExpr::List(vec![
                SExpr::Symbol("set!".into()),
                SExpr::Symbol(var_name.to_string()),
                value_expr,
            ]);

            if in_fn {
                fn_body.push(stmt);
            } else {
                exprs.push(stmt);
            }
            continue;
        } else if trimmed.contains(" = ") {
            let parts: Vec<&str> = trimmed.splitn(2, '=').collect();
            let var_name = parts[0].trim().trim_start_matches("let ").trim();
            let expr_str = parts[1].trim_end_matches(';').trim();

            let value_expr = parse_impk_expr(expr_str)?;
            let stmt = SExpr::List(vec![
                SExpr::Symbol("set!".into()),
                SExpr::Symbol(var_name.to_string()),
                value_expr,
            ]);

            if in_fn {
                fn_body.push(stmt);
            } else {
                exprs.push(stmt);
            }
            continue;
        }
    }

    if in_fn && !fn_body.is_empty() {
        let mut sig = vec![SExpr::Symbol(current_fn_name)];
        sig.extend(current_args);

        let mut fn_def = vec![SExpr::Symbol("define-query".into()), SExpr::List(sig)];
        fn_def.extend(fn_body);
        exprs.push(SExpr::List(fn_def));
    }

    Ok(exprs)
}

fn parse_impk_expr(s: &str) -> Result<SExpr, ImpKParseError> {
    let trimmed = s.trim();

    // 1. BitSet And-Not
    if trimmed.contains("&~") {
        let parts: Vec<&str> = trimmed.splitn(2, "&~").collect();
        return Ok(SExpr::List(vec![
            SExpr::Symbol("bitset:and-not".into()),
            parse_impk_expr(parts[0])?,
            parse_impk_expr(parts[1])?,
        ]));
    }

    // 2. Vector Monoid Reductions (e.g. sum(v))
    if trimmed.starts_with("sum(") && trimmed.ends_with(')') {
        let inner = &trimmed[4..trimmed.len() - 1];
        return Ok(SExpr::List(vec![
            SExpr::Symbol("vector:reduce-sum".into()),
            parse_impk_expr(inner)?,
        ]));
    }

    // 3. Topology Walk Operators (@ and @adaptive)
    if trimmed.contains("@adaptive[") {
        let parts: Vec<&str> = trimmed.splitn(2, "@adaptive[").collect();
        let g_expr = parse_impk_expr(parts[0])?;
        let rhs = parts[1].split(']').next().unwrap_or("");
        let sub_parts: Vec<&str> = rhs.split(';').collect();
        let f_expr = parse_impk_expr(sub_parts[0])?;
        let rel_str = if sub_parts.len() > 1 {
            sub_parts[1].trim().trim_matches('`').trim_matches('"')
        } else {
            "FOLLOWS"
        };
        return Ok(SExpr::List(vec![
            SExpr::Symbol("g:walk-adaptive".into()),
            g_expr,
            f_expr,
            SExpr::Str(rel_str.to_string()),
        ]));
    }

    if trimmed.contains('@') {
        let parts: Vec<&str> = trimmed.splitn(2, '@').collect();
        let g_expr = parse_impk_expr(parts[0])?;
        let rhs = parts[1].trim();

        if rhs.starts_with('[') && rhs.contains(']') {
            let inner = rhs.trim_start_matches('[').split(']').next().unwrap_or("");
            let sub_parts: Vec<&str> = inner.split(';').collect();
            let f_expr = parse_impk_expr(sub_parts[0])?;
            let rel_str = if sub_parts.len() > 1 {
                sub_parts[1].trim().trim_matches('`').trim_matches('"')
            } else {
                "FOLLOWS"
            };

            return Ok(SExpr::List(vec![
                SExpr::Symbol("g:walk-csr".into()),
                g_expr,
                f_expr,
                SExpr::Str(rel_str.to_string()),
            ]));
        } else {
            let f_expr = parse_impk_expr(rhs)?;
            return Ok(SExpr::List(vec![
                SExpr::Symbol("g:walk-csr".into()),
                g_expr,
                f_expr,
                SExpr::Str("FOLLOWS".to_string()),
            ]));
        }
    }

    // 4. Matrix Vector Multiplication (e.g. A * x)
    if trimmed.contains(" * ") {
        let parts: Vec<&str> = trimmed.splitn(2, " * ").collect();
        return Ok(SExpr::List(vec![
            SExpr::Symbol("g:mxv".into()),
            parse_impk_expr(parts[0])?,
            parse_impk_expr(parts[1])?,
        ]));
    }

    // 5. Element-Wise Vector Arithmetic (+, -, /)
    if trimmed.contains(" + ") {
        let parts: Vec<&str> = trimmed.splitn(2, " + ").collect();
        return Ok(SExpr::List(vec![
            SExpr::Symbol("vector:ewise-add".into()),
            parse_impk_expr(parts[0])?,
            parse_impk_expr(parts[1])?,
        ]));
    }

    // 6. Vector BitSet Masking (e.g. v <mask_bitset>)
    if trimmed.contains(" <") && trimmed.contains('>') {
        let parts: Vec<&str> = trimmed.splitn(2, " <").collect();
        let mask_part = parts[1].split('>').next().unwrap_or("");
        return Ok(SExpr::List(vec![
            SExpr::Symbol("vector:filter".into()),
            parse_impk_expr(parts[0])?,
            parse_impk_expr(mask_part)?,
        ]));
    }

    if trimmed.contains('|') && !trimmed.contains("||") {
        let parts: Vec<&str> = trimmed.splitn(2, '|').collect();
        return Ok(SExpr::List(vec![
            SExpr::Symbol("bitset:or".into()),
            parse_impk_expr(parts[0])?,
            parse_impk_expr(parts[1])?,
        ]));
    }

    if trimmed.starts_with("bitset ") {
        let node_str = trimmed.trim_start_matches("bitset ").trim();
        return Ok(SExpr::List(vec![
            SExpr::Symbol("bitset:from".into()),
            parse_impk_expr(node_str)?,
        ]));
    }

    if trimmed == "#t" || trimmed == "true" {
        return Ok(SExpr::Bool(true));
    }
    if trimmed == "#f" || trimmed == "false" {
        return Ok(SExpr::Bool(false));
    }
    if let Ok(val) = trimmed.parse::<i64>() {
        return Ok(SExpr::Int(val));
    }
    if let Ok(val) = trimmed.parse::<f64>() {
        return Ok(SExpr::Float(val));
    }

    Ok(SExpr::Symbol(trimmed.to_string()))
}
