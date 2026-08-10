//! @file reader.rs
//! @brief Recursive descent parser for ImpScheme S-Expression IR (`.impscm`).

use super::ast::SExpr;
use std::error::Error;
use std::fmt;

/// @struct ParseError
/// @brief Error type raised during ImpScheme IR syntax parsing.
#[derive(Debug)]
pub struct ParseError {
    pub message: String,
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "SExpr Parse Error: {}", self.message)
    }
}

impl Error for ParseError {}

/// @brief Parses an ImpScheme S-Expression text string into an AST list.
/// @param input ImpScheme source string (`.impscm`).
/// @return Vector of parsed top-level `SExpr` nodes.
pub fn parse(input: &str) -> Result<Vec<SExpr>, ParseError> {
    let tokens = tokenize(input)?;
    let mut iter = tokens.into_iter().peekable();
    let mut exprs = Vec::new();

    while iter.peek().is_some() {
        exprs.push(parse_expr(&mut iter)?);
    }

    Ok(exprs)
}

#[derive(Debug, PartialEq, Clone)]
enum Token {
    LParen,
    RParen,
    Atom(String),
}

fn tokenize(input: &str) -> Result<Vec<Token>, ParseError> {
    let mut tokens = Vec::new();
    let mut chars = input.chars().peekable();

    while let Some(&ch) = chars.peek() {
        match ch {
            ' ' | '\t' | '\r' | '\n' => {
                chars.next();
            }
            ';' => {
                while let Some(&c) = chars.peek() {
                    chars.next();
                    if c == '\n' {
                        break;
                    }
                }
            }
            '(' => {
                tokens.push(Token::LParen);
                chars.next();
            }
            ')' => {
                tokens.push(Token::RParen);
                chars.next();
            }
            '"' => {
                chars.next();
                let mut s = String::new();
                let mut escaped = false;
                loop {
                    match chars.next() {
                        Some('\\') if !escaped => {
                            escaped = true;
                        }
                        Some('"') if !escaped => {
                            break;
                        }
                        Some(c) => {
                            escaped = false;
                            s.push(c);
                        }
                        None => {
                            return Err(ParseError {
                                message: "Unterminated string literal".into(),
                            });
                        }
                    }
                }
                tokens.push(Token::Atom(format!("\"{}\"", s)));
            }
            _ => {
                let mut atom = String::new();
                while let Some(&c) = chars.peek() {
                    if c.is_whitespace() || c == '(' || c == ')' || c == ';' {
                        break;
                    }
                    atom.push(c);
                    chars.next();
                }
                if !atom.is_empty() {
                    tokens.push(Token::Atom(atom));
                }
            }
        }
    }

    Ok(tokens)
}

fn parse_expr<I>(tokens: &mut std::iter::Peekable<I>) -> Result<SExpr, ParseError>
where
    I: Iterator<Item = Token>,
{
    match tokens.next() {
        Some(Token::LParen) => {
            let mut list = Vec::new();
            loop {
                match tokens.peek() {
                    Some(Token::RParen) => {
                        tokens.next();
                        break;
                    }
                    Some(_) => {
                        list.push(parse_expr(tokens)?);
                    }
                    None => {
                        return Err(ParseError {
                            message: "Unexpected EOF while reading list".into(),
                        });
                    }
                }
            }
            Ok(SExpr::List(list))
        }
        Some(Token::RParen) => Err(ParseError {
            message: "Unexpected ')'".into(),
        }),
        Some(Token::Atom(atom)) => Ok(parse_atom(&atom)),
        None => Err(ParseError {
            message: "Unexpected EOF".into(),
        }),
    }
}

fn parse_atom(s: &str) -> SExpr {
    if s == "#t" || s == "true" {
        return SExpr::Bool(true);
    }
    if s == "#f" || s == "false" {
        return SExpr::Bool(false);
    }
    if s.starts_with('"') && s.ends_with('"') && s.len() >= 2 {
        return SExpr::Str(s[1..s.len() - 1].to_string());
    }
    if let Ok(n) = s.parse::<i64>() {
        return SExpr::Int(n);
    }
    if let Ok(f) = s.parse::<f64>() {
        return SExpr::Float(f);
    }
    SExpr::Symbol(s.to_string())
}
