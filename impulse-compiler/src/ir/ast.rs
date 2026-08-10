//! @file ast.rs
//! @brief Homoiconic ImpScheme S-Expression Abstract Syntax Tree (AST) definitions.

use std::fmt;

/// @enum SExpr
/// @brief Represents an S-Expression node in ImpScheme IR.
#[derive(Debug, Clone, PartialEq)]
pub enum SExpr {
    /// Symbol / Identifier (e.g. `define-query`, `OP_CSR_WALK`)
    Symbol(String),
    /// 64-bit Signed Integer Literal
    Int(i64),
    /// 64-bit Floating-Point Literal
    Float(f64),
    /// UTF-8 String Literal
    Str(String),
    /// Boolean Literal (`#t` or `#f`)
    Bool(bool),
    /// S-Expression List / Compound Node
    List(Vec<SExpr>),
}

impl fmt::Display for SExpr {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SExpr::Symbol(s) => write!(f, "{}", s),
            SExpr::Int(n) => write!(f, "{}", n),
            SExpr::Float(val) => write!(f, "{}", val),
            SExpr::Str(s) => write!(f, "{:?}", s),
            SExpr::Bool(b) => write!(f, "{}", if *b { "#t" } else { "#f" }),
            SExpr::List(list) => {
                write!(f, "(")?;
                for (i, elem) in list.iter().enumerate() {
                    if i > 0 {
                        write!(f, " ")?;
                    }
                    write!(f, "{}", elem)?;
                }
                write!(f, ")")
            }
        }
    }
}
