package impulse

/*
#include "impulse_graph.h"
#include <stdlib.h>
*/
import "C"

import "unsafe"

// SIMDGetTargetName returns the active SIMD instruction set target compiled into the native core kernel (e.g. AVX-512, AVX2, NEON, SCALAR).
func SIMDGetTargetName() string {
	cStr := C.impulse_simd_get_target_name()
	if cStr == nil {
		return "UNKNOWN"
	}
	return C.GoString(cStr)
}

// SIMDDotProductF32 computes the vectorized dot product between two float32 slices.
func SIMDDotProductF32(a, b []float32) float32 {
	if len(a) != len(b) || len(a) == 0 {
		return 0.0
	}

	res := C.impulse_simd_dot_product_f32(
		(*C.float)(unsafe.Pointer(&a[0])),
		(*C.float)(unsafe.Pointer(&b[0])),
		C.size_t(len(a)),
	)
	return float32(res)
}

// SIMDVectorSumF32 computes element-wise sum of two float32 slices using SIMD instructions.
func SIMDVectorSumF32(a, b []float32) ([]float32, error) {
	if len(a) != len(b) {
		return nil, ErrInvalidArgument
	}
	if len(a) == 0 {
		return []float32{}, nil
	}

	out := make([]float32, len(a))
	status := C.impulse_simd_vector_sum_f32(
		(*C.float)(unsafe.Pointer(&a[0])),
		(*C.float)(unsafe.Pointer(&b[0])),
		(*C.float)(unsafe.Pointer(&out[0])),
		C.size_t(len(a)),
	)

	if status != C.IMPULSE_OK {
		return nil, Status(status)
	}

	return out, nil
}

// SIMDIntersectSortedU32 performs high-speed vectorized intersection of two sorted uint32 arrays.
func SIMDIntersectSortedU32(a, b []uint32) []uint32 {
	if len(a) == 0 || len(b) == 0 {
		return []uint32{}
	}

	maxCap := len(a)
	if len(b) < maxCap {
		maxCap = len(b)
	}
	out := make([]uint32, maxCap)

	var outCount C.size_t
	status := C.impulse_simd_intersect_sorted_u32(
		(*C.uint32_t)(unsafe.Pointer(&a[0])),
		C.size_t(len(a)),
		(*C.uint32_t)(unsafe.Pointer(&b[0])),
		C.size_t(len(b)),
		(*C.uint32_t)(unsafe.Pointer(&out[0])),
		&outCount,
	)

	if status != C.IMPULSE_OK {
		return []uint32{}
	}

	return out[:int(outCount)]
}
