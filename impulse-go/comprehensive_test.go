package impulse

import (
	"testing"
)

func TestComprehensiveHetionet(t *testing.T) {
	snapPath := "/Users/jesse/impulse/datasets/hetionet/hetionet.v09.imps"

	snap, err := OpenSnapshot(snapPath)
	if err != nil {
		t.Fatalf("Failed to open snapshot: %v", err)
	}
	defer snap.Close()

	ctx := NewVmContext(snap)
	defer ctx.Destroy()

	vmState := NewVmState()
	defer vmState.Destroy()

	// 1. Bitset query
	qb := NewQueryBuilder()
	qb.InputNode(0).WalkEdge(0).CollectBitset()
	compiled := qb.Compile()
	defer compiled.Destroy()

	res := compiled.ExecuteWithContext(ctx, vmState, 0)
	if !res.IsOk() {
		t.Fatalf("VM execution failed: status %d", res.Status)
	}

	if res.ResultType != 4 { // TYPE_BITSET_HANDLE
		t.Fatalf("Expected bitset handle, got %d", res.ResultType)
	}

	// 2. Array query
	qb2 := NewQueryBuilder()
	qb2.InputNode(0).WalkEdge(0).CollectArray()
	compiled2 := qb2.Compile()
	defer compiled2.Destroy()

	res2 := compiled2.ExecuteWithContext(ctx, vmState, 0)
	if !res2.IsOk() {
		t.Fatalf("VM execution failed: status %d", res2.Status)
	}

	if res2.ResultType != 5 { // TYPE_NODE_VECTOR
		t.Fatalf("Expected node vector handle, got %d", res2.ResultType)
	}

	nodeVec := ctx.GetNodeVector(uint32(res2.RawValue))
	slice := nodeVec.Slice()

	if len(slice) == 0 {
		t.Fatalf("Expected non-empty node vector")
	}

	if slice[0] != 14877 { // Expected from Node.js
		t.Fatalf("Expected first element to be 14877, got %d", slice[0])
	}
}
