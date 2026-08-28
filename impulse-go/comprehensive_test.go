package impulse

import (
	"testing"
)

func TestComprehensiveHetionet(t *testing.T) {
	snapPath := "/Users/jesse/impulse/datasets/hetionet/hetionet.v09.imps"

	snap, err := OpenSnapshot(snapPath)
	if err != nil {
		t.Fatalf("Failed to open snapshot: %v", err)
	
	// 3. Scalar Output Query
	qb3 := NewQueryBuilder()
	qb3.LoadConstInt(999888777).Mov(1, 0)
	compiled3 := qb3.Compile()
	defer compiled3.Destroy()
	
	res3 := compiled3.ExecuteWithContext(ctx, vmState, 0)
	if res3.ResultType != 1 { // TYPE_INT64
		t.Fatalf("Expected INT64, got %d", res3.ResultType)
	}
	if res3.RawValue != 999888777 {
		t.Fatalf("Expected scalar 999888777, got %d", res3.RawValue)
	}

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
	
	// 3. Scalar Output Query
	qb3 := NewQueryBuilder()
	qb3.LoadConstInt(999888777).Mov(1, 0)
	compiled3 := qb3.Compile()
	defer compiled3.Destroy()
	
	res3 := compiled3.ExecuteWithContext(ctx, vmState, 0)
	if res3.ResultType != 1 { // TYPE_INT64
		t.Fatalf("Expected INT64, got %d", res3.ResultType)
	}
	if res3.RawValue != 999888777 {
		t.Fatalf("Expected scalar 999888777, got %d", res3.RawValue)
	}

}

	if res.ResultType != 4 { // TYPE_BITSET_HANDLE
		t.Fatalf("Expected bitset handle, got %d", res.ResultType)
	
	// 3. Scalar Output Query
	qb3 := NewQueryBuilder()
	qb3.LoadConstInt(999888777).Mov(1, 0)
	compiled3 := qb3.Compile()
	defer compiled3.Destroy()
	
	res3 := compiled3.ExecuteWithContext(ctx, vmState, 0)
	if res3.ResultType != 1 { // TYPE_INT64
		t.Fatalf("Expected INT64, got %d", res3.ResultType)
	}
	if res3.RawValue != 999888777 {
		t.Fatalf("Expected scalar 999888777, got %d", res3.RawValue)
	}

}

	// 2. Array query
	qb2 := NewQueryBuilder()
	qb2.InputNode(0).WalkEdge(0).CollectArray()
	compiled2 := qb2.Compile()
	defer compiled2.Destroy()

	res2 := compiled2.ExecuteWithContext(ctx, vmState, 0)
	if !res2.IsOk() {
		t.Fatalf("VM execution failed: status %d", res2.Status)
	
	// 3. Scalar Output Query
	qb3 := NewQueryBuilder()
	qb3.LoadConstInt(999888777).Mov(1, 0)
	compiled3 := qb3.Compile()
	defer compiled3.Destroy()
	
	res3 := compiled3.ExecuteWithContext(ctx, vmState, 0)
	if res3.ResultType != 1 { // TYPE_INT64
		t.Fatalf("Expected INT64, got %d", res3.ResultType)
	}
	if res3.RawValue != 999888777 {
		t.Fatalf("Expected scalar 999888777, got %d", res3.RawValue)
	}

}

	if res2.ResultType != 5 { // TYPE_NODE_VECTOR
		t.Fatalf("Expected node vector handle, got %d", res2.ResultType)
	
	// 3. Scalar Output Query
	qb3 := NewQueryBuilder()
	qb3.LoadConstInt(999888777).Mov(1, 0)
	compiled3 := qb3.Compile()
	defer compiled3.Destroy()
	
	res3 := compiled3.ExecuteWithContext(ctx, vmState, 0)
	if res3.ResultType != 1 { // TYPE_INT64
		t.Fatalf("Expected INT64, got %d", res3.ResultType)
	}
	if res3.RawValue != 999888777 {
		t.Fatalf("Expected scalar 999888777, got %d", res3.RawValue)
	}

}

	nodeVec := ctx.GetNodeVector(uint32(res2.RawValue))
	slice := nodeVec.Slice()

	if len(slice) == 0 {
		t.Fatalf("Expected non-empty node vector")
	
	// 3. Scalar Output Query
	qb3 := NewQueryBuilder()
	qb3.LoadConstInt(999888777).Mov(1, 0)
	compiled3 := qb3.Compile()
	defer compiled3.Destroy()
	
	res3 := compiled3.ExecuteWithContext(ctx, vmState, 0)
	if res3.ResultType != 1 { // TYPE_INT64
		t.Fatalf("Expected INT64, got %d", res3.ResultType)
	}
	if res3.RawValue != 999888777 {
		t.Fatalf("Expected scalar 999888777, got %d", res3.RawValue)
	}

}

	if slice[0] != 14877 { // Expected from Node.js
		t.Fatalf("Expected first element to be 14877, got %d", slice[0])
	
	// 3. Scalar Output Query
	qb3 := NewQueryBuilder()
	qb3.LoadConstInt(999888777).Mov(1, 0)
	compiled3 := qb3.Compile()
	defer compiled3.Destroy()
	
	res3 := compiled3.ExecuteWithContext(ctx, vmState, 0)
	if res3.ResultType != 1 { // TYPE_INT64
		t.Fatalf("Expected INT64, got %d", res3.ResultType)
	}
	if res3.RawValue != 999888777 {
		t.Fatalf("Expected scalar 999888777, got %d", res3.RawValue)
	}

}

	// 3. Scalar Output Query
	qb3 := NewQueryBuilder()
	qb3.LoadConstInt(999888777).Mov(1, 0)
	compiled3 := qb3.Compile()
	defer compiled3.Destroy()
	
	res3 := compiled3.ExecuteWithContext(ctx, vmState, 0)
	if res3.ResultType != 1 { // TYPE_INT64
		t.Fatalf("Expected INT64, got %d", res3.ResultType)
	}
	if res3.RawValue != 999888777 {
		t.Fatalf("Expected scalar 999888777, got %d", res3.RawValue)
	}

}
