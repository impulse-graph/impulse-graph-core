//! Spec v0.9.0 C-ABI FFI Exports

use crate::reader::SnapshotReader;
use crate::spec::*;
use std::ffi::CStr;
use std::os::raw::c_char;

#[no_mangle]
pub extern "C" fn impulse_rust_version() -> u32 {
    IMPULSE_VERSION_PACKED as u32
}

/// Opens an Impulse snapshot file via C-ABI FFI.
///
/// # Safety
/// Caller must ensure `file_path` is a valid null-terminated C string pointer and `out_status` points to valid memory or is null.
#[no_mangle]
pub unsafe extern "C" fn impulse_snapshot_open_rs(
    file_path: *const c_char,
    out_status: *mut ImpulseError,
) -> *mut SnapshotReader {
    if file_path.is_null() {
        if !out_status.is_null() {
            *out_status = ImpulseError::InvalidArgument;
        }
        return std::ptr::null_mut();
    }

    let c_str = CStr::from_ptr(file_path);
    let path_str = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => {
            if !out_status.is_null() {
                *out_status = ImpulseError::InvalidArgument;
            }
            return std::ptr::null_mut();
        }
    };

    match SnapshotReader::open(path_str) {
        Ok(reader) => {
            if !out_status.is_null() {
                *out_status = ImpulseError::Ok;
            }
            Box::into_raw(Box::new(reader))
        }
        Err(err) => {
            if !out_status.is_null() {
                *out_status = err;
            }
            std::ptr::null_mut()
        }
    }
}

/// Closes a SnapshotReader handle previously returned by `impulse_snapshot_open_rs`.
///
/// # Safety
/// Caller must ensure `reader` was created by `impulse_snapshot_open_rs` and is not dereferenced after closing.
#[no_mangle]
pub unsafe extern "C" fn impulse_snapshot_close_rs(reader: *mut SnapshotReader) {
    if !reader.is_null() {
        let _ = Box::from_raw(reader);
    }
}

/// Queries whether an edge exists between `src_id` and `tgt_id` in the specified relation.
///
/// # Safety
/// Caller must ensure `reader` points to a valid `SnapshotReader` instance or is null.
#[no_mangle]
pub unsafe extern "C" fn impulse_snapshot_is_adjacent_rs(
    reader: *const SnapshotReader,
    relation_index: usize,
    src_id: u64,
    tgt_id: u64,
) -> bool {
    if reader.is_null() {
        return false;
    }
    let reader_ref = &*reader;
    reader_ref.is_adjacent(relation_index as u16, src_id, tgt_id).unwrap_or(false)
}

#[repr(C)]
pub struct impulse_snapshot_t {
    _private: [u8; 0],
}

#[repr(C)]
pub struct impulse_vm_context_t {
    _private: [u8; 0],
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct impulse_instruction_t {
    pub opcode: u8,
    pub flags: u8,
    pub dst_reg: u16,
    pub payload: u32,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct impulse_vm_state_t {
    pub pc: u32,
    pub reserved: u32,
    pub flags: u64,
    pub registers: [u64; 64],
    pub register_types: [u8; 64],
    pub query_context: *mut impulse_vm_context_t,
    pub call_stack: [u32; 8],
    pub call_stack_depth: u32,
    pub reserved_padding2: u32,
}

#[repr(C)]
pub struct impulse_stmt_t {
    _private: [u8; 0],
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct impulse_column_desc_t {
    pub name: [u8; 32],
    pub type_code: u8,
    pub element_size: u8,
    pub is_nullable: bool,
    pub reserved: u8,
    pub dimension: u32,
    pub byte_offset_in_buf: usize,
    pub data_ptr: *const std::ffi::c_void,
    pub null_bitmap: *const u64,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct impulse_execution_result_t {
    pub status: i32,
    pub row_count: usize,
    pub column_count: u32,
    pub reserved: u32,
    pub total_bytes_written: usize,
    pub data_ptr: *const std::ffi::c_void,
    pub scalar_value: u64,
}

pub const IMPULSE_LANG_IMPSCM: i32 = 0;
pub const IMPULSE_LANG_IMPK: i32 = 1;
pub const IMPULSE_LANG_IMPLOG: i32 = 2;
pub const IMPULSE_LANG_CYPHER: i32 = 3;
pub const IMPULSE_LANG_CEL: i32 = 4;

extern "C" {
    pub fn impulse_snapshot_open(
        file_path: *const std::os::raw::c_char,
        out_status: *mut i32,
    ) -> *mut impulse_snapshot_t;
    pub fn impulse_snapshot_close(snapshot: *mut impulse_snapshot_t);
    pub fn impulse_snapshot_max_node_count(snapshot: *const impulse_snapshot_t) -> u64;

    pub fn impulse_vm_context_create(
        snapshot: *const impulse_snapshot_t,
    ) -> *mut impulse_vm_context_t;
    pub fn impulse_vm_context_destroy(ctx: *mut impulse_vm_context_t);
    pub fn impulse_vm_context_get_vector_size(ctx: *const impulse_vm_context_t) -> usize;
    pub fn impulse_vm_context_get_float_vector(
        ctx: *const impulse_vm_context_t,
        handle: usize,
    ) -> *const f32;

    pub fn impulse_vm_context_acquire_bitset(ctx: *mut impulse_vm_context_t) -> i32;
    pub fn impulse_vm_context_release_bitset(ctx: *mut impulse_vm_context_t, handle: usize);
    pub fn impulse_vm_context_bitset_add(
        ctx: *mut impulse_vm_context_t,
        handle: usize,
        node_id: u64,
    );
    pub fn impulse_vm_context_bitset_test(
        ctx: *const impulse_vm_context_t,
        handle: usize,
        node_id: u64,
    ) -> bool;
    pub fn impulse_vm_context_bitset_get_word(
        ctx: *const impulse_vm_context_t,
        handle: usize,
        word_idx: usize,
    ) -> u64;

    pub fn impulse_vm_execute(
        bytecode: *const impulse_instruction_t,
        instruction_count: usize,
        vm_state: *mut impulse_vm_state_t,
        input_param: u64,
    ) -> i32;

    // Compiler Functions
    pub fn impulse_compile_query(
        snapshot: *const impulse_snapshot_t,
        script: *const c_char,
        lang: i32,
        out_instructions: *mut impulse_instruction_t,
        out_capacity: usize,
        out_count: *mut usize,
    ) -> i32;

    pub fn impulse_compile_to_impas(
        snapshot: *const impulse_snapshot_t,
        script: *const c_char,
        lang: i32,
        out_impas_buffer: *mut c_char,
        out_capacity: usize,
        out_bytes_written: *mut usize,
    ) -> i32;

    pub fn impulse_compile_and_execute(
        snapshot: *const impulse_snapshot_t,
        script: *const c_char,
        lang: i32,
        state: *mut impulse_vm_state_t,
        input_seed: u64,
    ) -> i32;

    // SQLite-Style Statement Lifecycle
    pub fn impulse_stmt_prepare(
        snapshot: *const impulse_snapshot_t,
        query_text: *const c_char,
        out_stmt: *mut *mut impulse_stmt_t,
    ) -> i32;
    pub fn impulse_stmt_buffer_size(stmt: *const impulse_stmt_t) -> usize;
    pub fn impulse_stmt_finalize(stmt: *mut impulse_stmt_t);

    pub fn impulse_stmt_bind_node(stmt: *mut impulse_stmt_t, param: *const c_char, node_id: u64) -> i32;
    pub fn impulse_stmt_bind_nodes(stmt: *mut impulse_stmt_t, param: *const c_char, node_ids: *const u64, count: usize) -> i32;
    pub fn impulse_stmt_bind_bitset(stmt: *mut impulse_stmt_t, param: *const c_char, words: *const u64, word_count: usize) -> i32;
    pub fn impulse_stmt_bind_roaring(stmt: *mut impulse_stmt_t, param: *const c_char, bytes: *const u8, len: usize) -> i32;
    pub fn impulse_stmt_bind_int(stmt: *mut impulse_stmt_t, param: *const c_char, val: i64) -> i32;
    pub fn impulse_stmt_bind_uint(stmt: *mut impulse_stmt_t, param: *const c_char, val: u64) -> i32;
    pub fn impulse_stmt_bind_float(stmt: *mut impulse_stmt_t, param: *const c_char, val: f64) -> i32;
    pub fn impulse_stmt_bind_str(stmt: *mut impulse_stmt_t, param: *const c_char, str: *const c_char) -> i32;
    pub fn impulse_stmt_bind_uuid(stmt: *mut impulse_stmt_t, param: *const c_char, uuid_bytes: *const u8) -> i32;
    pub fn impulse_stmt_bind_vector(stmt: *mut impulse_stmt_t, param: *const c_char, data: *const f32, dim: usize) -> i32;

    pub fn impulse_stmt_execute(stmt: *mut impulse_stmt_t, buffer: *mut std::ffi::c_void, buffer_size: usize) -> i32;
    pub fn impulse_stmt_row_count(stmt: *const impulse_stmt_t) -> usize;
    pub fn impulse_stmt_column_count(stmt: *const impulse_stmt_t) -> u32;
    pub fn impulse_stmt_column_name(stmt: *const impulse_stmt_t, col_idx: u32) -> *const c_char;
    pub fn impulse_stmt_column_type(stmt: *const impulse_stmt_t, col_idx: u32) -> u8;
    pub fn impulse_stmt_column_dim(stmt: *const impulse_stmt_t, col_idx: u32) -> u32;
    pub fn impulse_stmt_column_data(stmt: *const impulse_stmt_t, col_idx: u32) -> *const std::ffi::c_void;
    pub fn impulse_stmt_column_is_null(stmt: *const impulse_stmt_t, col_idx: u32, row_idx: usize) -> bool;

    pub fn impulse_exec(
        snapshot: *const impulse_snapshot_t,
        query_text: *const c_char,
        seed_node: u64,
        out_result: *mut impulse_execution_result_t,
    ) -> i32;
}
