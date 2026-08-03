//! Architecture-Independent SIMD module for Rust (Google Highway Integration & Fallbacks)

use std::os::raw::c_char;
use std::ffi::CStr;

extern "C" {
    fn impulse_simd_get_target_name() -> *const c_char;
    fn impulse_simd_dot_product_f32(a: *const f32, b: *const f32, len: usize) -> f32;
    fn impulse_simd_vector_sum_f32(a: *const f32, b: *const f32, out: *mut f32, len: usize) -> i32;
    fn impulse_simd_intersect_sorted_u32(
        a: *const u32, len_a: usize,
        b: *const u32, len_b: usize,
        out_intersection: *mut u32,
        out_count: *mut usize
    ) -> i32;
}

/// Returns active Highway dynamic target SIMD execution mode.
pub fn current_target_name() -> &'static str {
    unsafe {
        let ptr = impulse_simd_get_target_name();
        if ptr.is_null() {
            return "Rust-Fallback";
        }
        CStr::from_ptr(ptr).to_str().unwrap_or("Unknown")
    }
}

/// Computes dynamic architecture-independent SIMD dot product of two float arrays.
pub fn dot_product_f32(a: &[f32], b: &[f32]) -> f32 {
    let len = a.len().min(b.len());
    if len == 0 {
        return 0.0;
    }
    unsafe {
        impulse_simd_dot_product_f32(a.as_ptr(), b.as_ptr(), len)
    }
}

/// Computes dynamic architecture-independent SIMD elementwise vector addition.
pub fn vector_sum_f32(a: &[f32], b: &[f32]) -> Vec<f32> {
    let len = a.len().min(b.len());
    let mut out = vec![0.0f32; len];
    if len == 0 {
        return out;
    }
    unsafe {
        let status = impulse_simd_vector_sum_f32(a.as_ptr(), b.as_ptr(), out.as_mut_ptr(), len);
        if status != 0 {
            // Pure Rust fallback in case of FFI error
            for i in 0..len {
                out[i] = a[i] + b[i];
            }
        }
    }
    out
}

/// Computes dynamic architecture-independent SIMD sorted array intersection.
pub fn intersect_sorted_u32(a: &[u32], b: &[u32]) -> Vec<u32> {
    let max_len = a.len().min(b.len());
    let mut out = vec![0u32; max_len];
    let mut out_count: usize = 0;

    unsafe {
        let status = impulse_simd_intersect_sorted_u32(
            a.as_ptr(), a.len(),
            b.as_ptr(), b.len(),
            out.as_mut_ptr(), &mut out_count
        );
        if status == 0 {
            out.truncate(out_count);
            return out;
        }
    }

    // Pure Rust fallback
    let mut i = 0;
    let mut j = 0;
    let mut result = Vec::new();
    while i < a.len() && j < b.len() {
        if a[i] == b[j] {
            result.push(a[i]);
            i += 1;
            j += 1;
        } else if a[i] < b[j] {
            i += 1;
        } else {
            j += 1;
        }
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_rust_simd_dot_product() {
        let a = vec![1.0f32, 2.0, 3.0, 4.0];
        let b = vec![2.0f32, 0.5, 1.0, 2.0];
        // 1*2 + 2*0.5 + 3*1 + 4*2 = 2 + 1 + 3 + 8 = 14.0
        let res = dot_product_f32(&a, &b);
        assert_eq!(res, 14.0);
    }

    #[test]
    fn test_rust_simd_vector_sum() {
        let a = vec![10.0f32, 20.0, 30.0];
        let b = vec![1.0f32, 2.0, 3.0];
        let res = vector_sum_f32(&a, &b);
        assert_eq!(res, vec![11.0f32, 22.0, 33.0]);
    }

    #[test]
    fn test_rust_simd_sorted_intersection() {
        let a = vec![2, 4, 6, 8, 10];
        let b = vec![1, 4, 5, 6, 9, 10, 12];
        let res = intersect_sorted_u32(&a, &b);
        assert_eq!(res, vec![4, 6, 10]);
    }
}
