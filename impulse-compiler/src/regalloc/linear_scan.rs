//! @file linear_scan.rs
//! @brief Linear scan register allocation pass mapping virtual IR variables to ImpulseVM registers R0..R63.

use crate::ir::ast::SExpr;
use std::collections::HashMap;

/// @struct AllocatedInstruction
/// @brief Instruction holding assigned physical register operands.
#[derive(Debug, Clone)]
pub struct AllocatedInstruction {
    pub opcode: String,
    pub dst: Option<String>,
    pub args: Vec<String>,
}

/// @struct AllocatedProgram
/// @brief Represents a fully register-allocated ImpulseVM program.
#[derive(Debug, Clone)]
pub struct AllocatedProgram {
    pub function_name: String,
    pub frame_size: usize,
    pub instructions: Vec<AllocatedInstruction>,
}

/// @brief Runs linear scan register allocation on ImpScheme AST expressions.
/// @param exprs Expression vector.
/// @return `Result<AllocatedProgram, String>`
pub fn assign_registers(exprs: Vec<SExpr>) -> Result<AllocatedProgram, String> {
    let mut reg_map: HashMap<String, usize> = HashMap::new();
    let mut next_reg = 2; // R0 = input_val, R1 = scratch
    let mut fn_name = "main_query".to_string();
    let mut instructions = Vec::new();

    // Pre-pass: bind query parameters to R0
    for expr in &exprs {
        if let SExpr::List(ref list) = expr {
            if !list.is_empty() {
                if let SExpr::Symbol(ref op) = list[0] {
                    if op == "define-query" || op == "defn" || op == "define" {
                        if list.len() >= 2 {
                            if let SExpr::List(ref sig) = list[1] {
                                if !sig.is_empty() {
                                    if let SExpr::Symbol(ref name) = sig[0] {
                                        fn_name = name.clone();
                                    }
                                    let mut assigned_input = false;
                                    for arg in &sig[1..] {
                                        if let SExpr::Symbol(ref arg_name) = arg {
                                            if arg_name == "g" || arg_name == "graph" {
                                                continue;
                                            }
                                            if !assigned_input {
                                                reg_map.insert(arg_name.clone(), 0);
                                                assigned_input = true;
                                            } else {
                                                reg_map.insert(arg_name.clone(), next_reg);
                                                next_reg += 1;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    let mut get_reg = |var: &str, is_written: bool| -> String {
        if var.starts_with('R') && var[1..].parse::<usize>().is_ok() {
            return var.to_string();
        }
        if let Some(&r) = reg_map.get(var) {
            format!("R{}", r)
        } else if is_written {
            let r = next_reg;
            next_reg += 1;
            reg_map.insert(var.to_string(), r);
            format!("R{}", r)
        } else {
            let r = next_reg;
            next_reg += 1;
            reg_map.insert(var.to_string(), r);
            format!("R{}", r)
        }
    };

    for expr in exprs {
        match expr {
            SExpr::List(list) if !list.is_empty() => {
                if let SExpr::Symbol(ref op) = list[0] {
                    match op.as_str() {
                        "define-query" | "defn" | "define" => {
                            if list.len() > 2 {
                                for body_expr in &list[2..] {
                                    lower_stmt(body_expr, &mut get_reg, &mut instructions)?;
                                }
                            }
                        }
                        _ => {
                            lower_stmt(&SExpr::List(list), &mut get_reg, &mut instructions)?;
                        }
                    }
                }
            }
            _ => {}
        }
    }

    let frame_size = next_reg.max(16);

    Ok(AllocatedProgram {
        function_name: fn_name,
        frame_size,
        instructions,
    })
}

fn lower_stmt<F>(
    expr: &SExpr,
    get_reg: &mut F,
    instructions: &mut Vec<AllocatedInstruction>,
) -> Result<(), String>
where
    F: FnMut(&str, bool) -> String,
{
    match expr {
        SExpr::List(list) if !list.is_empty() => {
            if let SExpr::Symbol(ref op) = list[0] {
                match op.as_str() {
                    "let" | "let*" => {
                        if list.len() >= 2 {
                            if let SExpr::List(ref bindings) = list[1] {
                                for binding in bindings {
                                    if let SExpr::List(ref pair) = binding {
                                        if pair.len() == 2 {
                                            if let SExpr::Symbol(ref var_name) = pair[0] {
                                                let dst_reg = get_reg(var_name, true);
                                                lower_value(&pair[1], &dst_reg, get_reg, instructions);
                                            }
                                        }
                                    }
                                }
                            }
                            for body_expr in &list[2..] {
                                lower_stmt(body_expr, get_reg, instructions)?;
                            }
                        }
                    }
                    "set!" => {
                        if list.len() == 3 {
                            if let SExpr::Symbol(ref var_name) = list[1] {
                                let dst_reg = get_reg(var_name, true);
                                lower_value(&list[2], &dst_reg, get_reg, instructions);
                            }
                        }
                    }
                    "return" => {
                        if list.len() == 2 {
                            lower_value(&list[1], "R63", get_reg, instructions);
                            instructions.push(AllocatedInstruction {
                                opcode: "OP_LEAVE_FRAME".into(),
                                dst: None,
                                args: vec![],
                            });
                            instructions.push(AllocatedInstruction {
                                opcode: "OP_HALT".into(),
                                dst: None,
                                args: vec![],
                            });
                        }
                    }
                    "bitset:and-not" => {
                        if list.len() == 3 {
                            let dst = get_reg("tmp_and_not", true);
                            let arg1 = eval_expr_to_reg(&list[1], get_reg, instructions);
                            let arg2 = eval_expr_to_reg(&list[2], get_reg, instructions);
                            instructions.push(AllocatedInstruction {
                                opcode: "OP_ROARING_BITMAP_AND_NOT".into(),
                                dst: Some(dst),
                                args: vec![arg1, arg2],
                            });
                        }
                    }
                    "bitset:or" => {
                        if list.len() == 3 {
                            let dst = get_reg("tmp_or", true);
                            let arg1 = eval_expr_to_reg(&list[1], get_reg, instructions);
                            let arg2 = eval_expr_to_reg(&list[2], get_reg, instructions);
                            instructions.push(AllocatedInstruction {
                                opcode: "OP_ROARING_BITMAP_OR".into(),
                                dst: Some(dst),
                                args: vec![arg1, arg2],
                            });
                        }
                    }
                    "g:walk-csr" => {
                        if list.len() == 4 {
                            let dst = get_reg("tmp_walk", true);
                            let g_reg = eval_expr_to_reg(&list[1], get_reg, instructions);
                            let f_reg = eval_expr_to_reg(&list[2], get_reg, instructions);
                            let rel = get_expr_str(&list[3]);
                            instructions.push(AllocatedInstruction {
                                opcode: "OP_CSR_WALK".into(),
                                dst: Some(dst),
                                args: vec![g_reg, f_reg, rel],
                            });
                        }
                    }
                    _ => {}
                }
            }
        }
        _ => {}
    }
    Ok(())
}

fn lower_value<F>(
    expr: &SExpr,
    target_reg: &str,
    get_reg: &mut F,
    instructions: &mut Vec<AllocatedInstruction>,
) where
    F: FnMut(&str, bool) -> String,
{
    match expr {
        SExpr::Int(val) => {
            instructions.push(AllocatedInstruction {
                opcode: "OP_LOAD_CONST_INT".into(),
                dst: Some(target_reg.to_string()),
                args: vec![val.to_string()],
            });
        }
        SExpr::Bool(val) => {
            instructions.push(AllocatedInstruction {
                opcode: "OP_LOAD_CONST_INT".into(),
                dst: Some(target_reg.to_string()),
                args: vec![if *val { "1".into() } else { "0".into() }],
            });
        }
        SExpr::Symbol(sym) => {
            if sym == "#t" || sym == "true" {
                instructions.push(AllocatedInstruction {
                    opcode: "OP_LOAD_CONST_INT".into(),
                    dst: Some(target_reg.to_string()),
                    args: vec!["1".to_string()],
                });
            } else if sym == "#f" || sym == "false" {
                instructions.push(AllocatedInstruction {
                    opcode: "OP_LOAD_CONST_INT".into(),
                    dst: Some(target_reg.to_string()),
                    args: vec!["0".to_string()],
                });
            } else {
                let src_reg = get_reg(sym, false);
                if src_reg != target_reg {
                    instructions.push(AllocatedInstruction {
                        opcode: "OP_MOVE".into(),
                        dst: Some(target_reg.to_string()),
                        args: vec![src_reg],
                    });
                }
            }
        }
        SExpr::List(list) if !list.is_empty() => {
            if let SExpr::Symbol(ref op) = list[0] {
                match op.as_str() {
                    "bitset:from" => {
                        if list.len() == 2 {
                            let src = eval_expr_to_reg(&list[1], get_reg, instructions);
                            instructions.push(AllocatedInstruction {
                                opcode: "OP_COLLECT_BITSET".into(),
                                dst: Some(target_reg.to_string()),
                                args: vec![src],
                            });
                        }
                    }
                    "bitset:and-not" => {
                        if list.len() == 3 {
                            let arg1 = eval_expr_to_reg(&list[1], get_reg, instructions);
                            let arg2 = eval_expr_to_reg(&list[2], get_reg, instructions);
                            instructions.push(AllocatedInstruction {
                                opcode: "OP_ROARING_BITMAP_AND_NOT".into(),
                                dst: Some(target_reg.to_string()),
                                args: vec![arg1, arg2],
                            });
                        }
                    }
                    "bitset:or" => {
                        if list.len() == 3 {
                            let arg1 = eval_expr_to_reg(&list[1], get_reg, instructions);
                            let arg2 = eval_expr_to_reg(&list[2], get_reg, instructions);
                            instructions.push(AllocatedInstruction {
                                opcode: "OP_ROARING_BITMAP_OR".into(),
                                dst: Some(target_reg.to_string()),
                                args: vec![arg1, arg2],
                            });
                        }
                    }
                    "g:walk-csr" => {
                        if list.len() == 4 {
                            let f_reg = eval_expr_to_reg(&list[2], get_reg, instructions);
                            let rel = get_expr_str(&list[3]);
                            instructions.push(AllocatedInstruction {
                                opcode: "OP_CSR_WALK".into(),
                                dst: Some(target_reg.to_string()),
                                args: vec![f_reg, rel],
                            });
                        }
                    }
                    "g:walk-csr-filtered" => {
                        if list.len() == 5 {
                            let f_reg = eval_expr_to_reg(&list[2], get_reg, instructions);
                            let rel = get_expr_str(&list[3]);
                            let mask_reg = eval_expr_to_reg(&list[4], get_reg, instructions);
                            instructions.push(AllocatedInstruction {
                                opcode: "OP_CSR_WALK_FILTERED".into(),
                                dst: Some(target_reg.to_string()),
                                args: vec![f_reg, rel, mask_reg],
                            });
                        }
                    }
                    _ => {}
                }
            }
        }
        _ => {}
    }
}

fn eval_expr_to_reg<F>(
    expr: &SExpr,
    get_reg: &mut F,
    instructions: &mut Vec<AllocatedInstruction>,
) -> String
where
    F: FnMut(&str, bool) -> String,
{
    match expr {
        SExpr::Symbol(s) => get_reg(s, false),
        SExpr::Int(n) => n.to_string(),
        SExpr::List(_) => {
            let tmp_reg = get_reg("tmp_val", true);
            lower_value(expr, &tmp_reg, get_reg, instructions);
            tmp_reg
        }
        _ => "R1".into(),
    }
}

fn get_expr_str(expr: &SExpr) -> String {
    match expr {
        SExpr::Str(s) => format!("REL_{}", s.to_uppercase()),
        SExpr::Symbol(s) => format!("REL_{}", s.to_uppercase()),
        _ => "REL_DEFAULT".into(),
    }
}
