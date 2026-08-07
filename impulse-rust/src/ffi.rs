//! Spec v2.4 C-ABI FFI Exports

use crate::reader::SnapshotReader;
use crate::spec::*;
use std::ffi::CStr;
use std::os::raw::c_char;

#[no_mangle]
pub extern "C" fn impulse_rust_version() -> u32 {
    IMPULSE_VERSION_PACKED as u32
}

#[no_mangle]
pub extern "C" fn impulse_snapshot_open_rs(
    file_path: *const c_char,
    out_status: *mut ImpulseError,
) -> *mut SnapshotReader {
    if file_path.is_null() {
        if !out_status.is_null() {
            unsafe { *out_status = ImpulseError::InvalidArgument };
        }
        return std::ptr::null_mut();
    }

    let c_str = unsafe { CStr::from_ptr(file_path) };
    let path_str = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => {
            if !out_status.is_null() {
                unsafe { *out_status = ImpulseError::InvalidArgument };
            }
            return std::ptr::null_mut();
        }
    };

    match SnapshotReader::open(path_str) {
        Ok(reader) => {
            if !out_status.is_null() {
                unsafe { *out_status = ImpulseError::Ok };
            }
            Box::into_raw(Box::new(reader))
        }
        Err(err) => {
            if !out_status.is_null() {
                unsafe { *out_status = err };
            }
            std::ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn impulse_snapshot_close_rs(reader: *mut SnapshotReader) {
    if !reader.is_null() {
        unsafe {
            let _ = Box::from_raw(reader);
        }
    }
}

#[no_mangle]
pub extern "C" fn impulse_snapshot_is_adjacent_rs(
    reader: *const SnapshotReader,
    relation_index: usize,
    src_id: u64,
    tgt_id: u64,
) -> bool {
    if reader.is_null() {
        return false;
    }
    let reader_ref = unsafe { &*reader };
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
}
