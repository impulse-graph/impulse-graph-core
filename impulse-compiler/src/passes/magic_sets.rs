use crate::ir::ast::SExpr;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Adornment {
    Bound,
    Free,
}

#[derive(Debug, Clone)]
pub struct MagicRule {
    pub head_predicate: String,
    pub bound_value: String,
    pub body: Vec<SExpr>,
}

pub struct MagicSetTransformation;

impl MagicSetTransformation {
    pub fn transform_query(predicate: &str, bound_val: &str) -> (String, SExpr) {
        let magic_pred_name = format!("m_{}_b", predicate);
        let magic_ast = SExpr::List(vec![
            SExpr::Symbol("magic:seed".to_string()),
            SExpr::Symbol(magic_pred_name.clone()),
            SExpr::Str(bound_val.to_string()),
        ]);
        (magic_pred_name, magic_ast)
    }
}
