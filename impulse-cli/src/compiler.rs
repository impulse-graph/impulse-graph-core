//! Impulse Graph Compiler Bridge (C-ABI)
//!
//! Lowering of high-level graph DSLs (ImpLog, ImpK, Cypher, ImpScheme) into
//! register-allocated impOps bytecode and ImpAsm assembly via the in-kernel C-ABI engine.

use std::error::Error;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LanguageTarget {
    ImpScm,
    ImpK,
    ImpLog,
    Cypher,
    Cel,
}

pub fn compile_script_to_impas(
    source: &str,
    target: LanguageTarget,
) -> Result<String, Box<dyn Error>> {
    let lang = match target {
        LanguageTarget::ImpScm => impulse_graph::ffi::IMPULSE_LANG_IMPSCM,
        LanguageTarget::ImpK => impulse_graph::ffi::IMPULSE_LANG_IMPK,
        LanguageTarget::ImpLog => impulse_graph::ffi::IMPULSE_LANG_IMPLOG,
        LanguageTarget::Cypher => impulse_graph::ffi::IMPULSE_LANG_CYPHER,
        LanguageTarget::Cel => impulse_graph::ffi::IMPULSE_LANG_CEL,
    };

    let c_source = CString::new(source)?;
    let mut buffer = vec![0u8; 128 * 1024];
    let mut written = 0;

    let rc = unsafe {
        impulse_graph::ffi::impulse_compile_to_impas(
            std::ptr::null(),
            c_source.as_ptr(),
            lang,
            buffer.as_mut_ptr() as *mut c_char,
            buffer.len(),
            &mut written,
        )
    };

    if rc == 0 && written > 0 {
        let s = unsafe { CStr::from_ptr(buffer.as_ptr() as *const c_char) };
        Ok(s.to_string_lossy().into_owned())
    } else {
        Err(format!("Compilation failed with status code {}", rc).into())
    }
}

pub fn compile_to_impas(source: &str) -> Result<String, Box<dyn Error>> {
    compile_script_to_impas(source, LanguageTarget::ImpScm)
}
