//! @file lib.rs
//! @brief Impulse Graph Engine DSL Compiler & ImpScheme IR Processor Crate.

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
use passes::fusion::OpcodeFusionPass;

#[derive(Debug, Clone, Copy)]
pub struct CompilerOptions {
    pub enable_magic_sets: bool,
    pub enable_opcode_fusion: bool,
    pub enable_partition_elimination: bool,
    pub enable_vector_fusion: bool,
}

impl Default for CompilerOptions {
    fn default() -> Self {
        Self {
            enable_magic_sets: true,
            enable_opcode_fusion: true,
            enable_partition_elimination: true,
            enable_vector_fusion: true,
        }
    }
}

impl CompilerOptions {
    pub fn none() -> Self {
        Self {
            enable_magic_sets: false,
            enable_opcode_fusion: false,
            enable_partition_elimination: false,
            enable_vector_fusion: false,
        }
    }
}

pub fn compile_to_impas(source: &str) -> Result<String, Box<dyn Error>> {
    compile_script_to_impas(source, LanguageTarget::ImpScm)
}

pub fn compile_impk_to_impas(source: &str) -> Result<String, Box<dyn Error>> {
    compile_script_to_impas(source, LanguageTarget::ImpK)
}

pub fn compile_implog_to_impas(source: &str) -> Result<String, Box<dyn Error>> {
    compile_script_to_impas(source, LanguageTarget::ImpLog)
}

pub fn compile_script_to_impas(
    source: &str,
    target: LanguageTarget,
) -> Result<String, Box<dyn Error>> {
    compile_script_with_options(source, target, CompilerOptions::default())
}

pub fn compile_script_with_options(
    source: &str,
    target: LanguageTarget,
    options: CompilerOptions,
) -> Result<String, Box<dyn Error>> {
    let mut exprs = frontends::parse_to_ir(source, target)?;

    if options.enable_vector_fusion {
        exprs = passes::vector_fusion::run(exprs);
    }
    if options.enable_partition_elimination {
        exprs = passes::partition_elimination::run(exprs);
    }

    let prog = regalloc::linear_scan::assign_registers(exprs)?;
    let mut asm = backend::impas_emitter::emit(prog);

    if options.enable_opcode_fusion {
        let asm_lines: Vec<&str> = asm.lines().collect();
        let fused_lines = OpcodeFusionPass::fuse_assembly_instructions(asm_lines);
        asm = fused_lines.join("\n");
    }

    Ok(asm)
}
