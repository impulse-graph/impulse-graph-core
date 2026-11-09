use crate::compiler::{self, LanguageTarget};
use std::error::Error;
use std::fs;
use std::path::Path;

pub fn run(input: &Path, output: Option<&Path>, _emit_ir: bool) -> Result<(), Box<dyn Error>> {
    let source = fs::read_to_string(input)?;

    let target = match input.extension().and_then(|ext| ext.to_str()) {
        Some("impk") => LanguageTarget::ImpK,
        Some("implog") => LanguageTarget::ImpLog,
        Some("cypher") | Some("cql") => LanguageTarget::Cypher,
        _ => LanguageTarget::ImpScm,
    };

    let asm_text = compiler::compile_script_to_impas(&source, target)?;

    if let Some(out_path) = output {
        fs::write(out_path, &asm_text)?;
        println!("Successfully compiled {:?} to {:?}", input, out_path);
    } else {
        println!("{}", asm_text);
    }

    Ok(())
}
