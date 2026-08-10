//! @file ffi.rs
//! @brief C-ABI Foreign Function Interface (FFM / FFI) exports for libimpulse_compiler.

use crate::compile_script_to_impas;
use crate::frontends::LanguageTarget;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;

/// C-ABI Status Codes
pub const IMPULSE_COMPILER_OK: i32 = 0;
pub const IMPULSE_COMPILER_ERR_NULL_PTR: i32 = -1;
pub const IMPULSE_COMPILER_ERR_INVALID_UTF8: i32 = -2;
pub const IMPULSE_COMPILER_ERR_PARSE_FAILED: i32 = -3;

/// @brief Returns the packed compiler specification version (0.9.0 = 9).
#[no_mangle]
pub extern "C" fn impulse_compiler_version() -> u32 {
    9
}

/// @brief Compiles ImpK DSL source text into ImpAsm (.impas) assembly text via C-ABI.
/// @param source_ptr Null-terminated C string containing ImpK source code.
/// @param out_asm_ptr Pointer to receive the allocated null-terminated C string output.
/// @return `IMPULSE_COMPILER_OK` (0) on success, or negative error status code.
/// @note Caller must release non-null `*out_asm_ptr` via `impulse_compiler_free_string`.
///
/// # Safety
/// `source_ptr` must be a valid null-terminated C string. `out_asm_ptr` must point to valid memory.
#[no_mangle]
pub unsafe extern "C" fn impulse_compile_impk(
    source_ptr: *const c_char,
    out_asm_ptr: *mut *mut c_char,
) -> i32 {
    compile_c_api(source_ptr, out_asm_ptr, LanguageTarget::ImpK)
}

/// @brief Compiles ImpLog Datalog rule source text into ImpAsm (.impas) assembly text via C-ABI.
/// @param source_ptr Null-terminated C string containing ImpLog source code.
/// @param out_asm_ptr Pointer to receive the allocated null-terminated C string output.
/// @return `IMPULSE_COMPILER_OK` (0) on success, or negative error status code.
/// @note Caller must release non-null `*out_asm_ptr` via `impulse_compiler_free_string`.
///
/// # Safety
/// `source_ptr` must be a valid null-terminated C string. `out_asm_ptr` must point to valid memory.
#[no_mangle]
pub unsafe extern "C" fn impulse_compile_implog(
    source_ptr: *const c_char,
    out_asm_ptr: *mut *mut c_char,
) -> i32 {
    compile_c_api(source_ptr, out_asm_ptr, LanguageTarget::ImpLog)
}

/// @brief Compiles ImpScheme S-Expression IR text into ImpAsm (.impas) assembly text via C-ABI.
/// @param source_ptr Null-terminated C string containing ImpScheme source code.
/// @param out_asm_ptr Pointer to receive the allocated null-terminated C string output.
/// @return `IMPULSE_COMPILER_OK` (0) on success, or negative error status code.
/// @note Caller must release non-null `*out_asm_ptr` via `impulse_compiler_free_string`.
///
/// # Safety
/// `source_ptr` must be a valid null-terminated C string. `out_asm_ptr` must point to valid memory.
#[no_mangle]
pub unsafe extern "C" fn impulse_compile_impscm(
    source_ptr: *const c_char,
    out_asm_ptr: *mut *mut c_char,
) -> i32 {
    compile_c_api(source_ptr, out_asm_ptr, LanguageTarget::ImpScm)
}

/// @brief Releases a null-terminated C string handle previously returned by compiler functions.
/// @param ptr Pointer previously returned by `impulse_compile_*`.
///
/// # Safety
/// `ptr` must be null or a valid pointer previously returned by a compiler C-ABI call.
#[no_mangle]
pub unsafe extern "C" fn impulse_compiler_free_string(ptr: *mut c_char) {
    if !ptr.is_null() {
        let _ = CString::from_raw(ptr);
    }
}

unsafe fn compile_c_api(
    source_ptr: *const c_char,
    out_asm_ptr: *mut *mut c_char,
    target: LanguageTarget,
) -> i32 {
    if source_ptr.is_null() || out_asm_ptr.is_null() {
        return IMPULSE_COMPILER_ERR_NULL_PTR;
    }

    let c_str = match CStr::from_ptr(source_ptr).to_str() {
        Ok(s) => s,
        Err(_) => return IMPULSE_COMPILER_ERR_INVALID_UTF8,
    };

    match compile_script_to_impas(c_str, target) {
        Ok(asm) => match CString::new(asm) {
            Ok(c_asm) => {
                *out_asm_ptr = c_asm.into_raw();
                IMPULSE_COMPILER_OK
            }
            Err(_) => IMPULSE_COMPILER_ERR_PARSE_FAILED,
        },
        Err(_) => IMPULSE_COMPILER_ERR_PARSE_FAILED,
    }
}
