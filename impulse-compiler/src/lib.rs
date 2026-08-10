//! @file lib.rs
//! @brief Impulse Graph Engine DSL Compiler & ImpScheme IR Processor Crate.
//!
//! Provides parsing, optimization passes, linear scan register allocation,
//! and ImpAsm (.impas) text code generation for ImpK, ImpLog, and ImpScheme DSLs.

pub mod backend;
pub mod ffi;
pub mod frontends;
pub mod ir;
pub mod passes;
pub mod regalloc;

pub use ffi::*;
pub use frontends::LanguageTarget;
pub use ir::ast::SExpr;
use std::error::Error;

/// @brief Compiles ImpScheme S-Expression IR text directly to ImpAsm (.impas) text.
/// @param source ImpScheme IR text.
/// @return `Result<String, Box<dyn Error>>`
pub fn compile_to_impas(source: &str) -> Result<String, Box<dyn Error>> {
    compile_script_to_impas(source, LanguageTarget::ImpScm)
}

/// @brief Compiles ImpK array DSL text directly to ImpAsm (.impas) text.
/// @param source ImpK source code.
/// @return `Result<String, Box<dyn Error>>`
pub fn compile_impk_to_impas(source: &str) -> Result<String, Box<dyn Error>> {
    compile_script_to_impas(source, LanguageTarget::ImpK)
}

/// @brief Compiles ImpLog Datalog rule text directly to ImpAsm (.impas) text.
/// @param source ImpLog source code.
/// @return `Result<String, Box<dyn Error>>`
pub fn compile_implog_to_impas(source: &str) -> Result<String, Box<dyn Error>> {
    compile_script_to_impas(source, LanguageTarget::ImpLog)
}

/// @brief Compiles any supported DSL source text to ImpAsm (.impas) text.
/// @param source Source code text.
/// @param target Source language target enum variant.
/// @return `Result<String, Box<dyn Error>>`
pub fn compile_script_to_impas(
    source: &str,
    target: LanguageTarget,
) -> Result<String, Box<dyn Error>> {
    // 1. Parse Source Language into ImpScm IR AST
    let exprs = frontends::parse_to_ir(source, target)?;

    // 2. Run AST Optimization, Vector Fusion & Partition Elimination Passes
    let opt_exprs = passes::vector_fusion::run(exprs);
    let opt_exprs = passes::partition_elimination::run(opt_exprs);

    // 3. Perform Linear Scan Register Allocation
    let prog = regalloc::linear_scan::assign_registers(opt_exprs)?;

    // 4. Emit Canonical .impas Text
    let asm = backend::impas_emitter::emit(prog);

    Ok(asm)
}
