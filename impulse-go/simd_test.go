package impulse_test

import (
	"math"
	"testing"

	"github.com/impulse-graph/impulse-graph/go"
)

func TestSIMDOperations(t *testing.T) {
	target := impulse.SIMDGetTargetName()
	t.Logf("Compiled SIMD Target: %s", target)
	if target == "" {
		t.Errorf("expected non-empty SIMD target name")
	}

	// Test SIMDDotProductF32
	vecA := []float32{1.0, 2.0, 3.0, 4.0}
	vecB := []float32{2.0, 0.5, 1.0, 2.0}
	// Expected: (1*2) + (2*0.5) + (3*1) + (4*2) = 2 + 1 + 3 + 8 = 14.0
	dotProduct := impulse.SIMDDotProductF32(vecA, vecB)
	if math.Abs(float64(dotProduct-14.0)) > 1e-5 {
		t.Errorf("expected dot product 14.0, got %f", dotProduct)
	}

	// Test SIMDVectorSumF32
	sum, err := impulse.SIMDVectorSumF32(vecA, vecB)
	if err != nil {
		t.Fatalf("failed to compute SIMD vector sum: %v", err)
	}
	expectedSum := []float32{3.0, 2.5, 4.0, 6.0}
	for i, val := range sum {
		if math.Abs(float64(val-expectedSum[i])) > 1e-5 {
			t.Errorf("index %d: expected sum %f, got %f", i, expectedSum[i], val)
		}
	}

	// Test SIMDIntersectSortedU32
	arr1 := []uint32{1, 3, 5, 7, 9, 11, 15}
	arr2 := []uint32{2, 3, 6, 7, 10, 11, 12, 15}
	// Intersection: [3, 7, 11, 15]
	inter := impulse.SIMDIntersectSortedU32(arr1, arr2)
	expectedInter := []uint32{3, 7, 11, 15}
	if len(inter) != len(expectedInter) {
		t.Fatalf("expected intersection length %d, got %d", len(expectedInter), len(inter))
	}
	for i, val := range inter {
		if val != expectedInter[i] {
			t.Errorf("index %d: expected %d, got %d", i, expectedInter[i], val)
		}
	}
}
