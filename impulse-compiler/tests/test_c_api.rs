use impulse_compiler::*;
use std::ffi::{CStr, CString};
use std::ptr;

#[test]
fn test_c_api_version() {
    assert_eq!(impulse_compiler_version(), 9);
}

#[test]
fn test_c_api_compile_impk() {
    let source = CString::new("fn query(g, s) { return s; }").unwrap();
    let mut out_ptr: *mut std::os::raw::c_char = ptr::null_mut();

    unsafe {
        let status = impulse_compile_impk(source.as_ptr(), &mut out_ptr);
        assert_eq!(status, IMPULSE_COMPILER_OK);
        assert!(!out_ptr.is_null());

        let asm = CStr::from_ptr(out_ptr).to_str().unwrap();
        assert!(asm.contains("OP_ENTER_FRAME"));

        impulse_compiler_free_string(out_ptr);
    }
}

#[test]
fn test_c_api_null_arg_error() {
    unsafe {
        let status = impulse_compile_impk(ptr::null(), ptr::null_mut());
        assert_eq!(status, IMPULSE_COMPILER_ERR_NULL_PTR);
    }
}
