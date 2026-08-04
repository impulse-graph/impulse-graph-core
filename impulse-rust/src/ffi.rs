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
