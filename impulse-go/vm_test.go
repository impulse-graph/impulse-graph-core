package impulse_test

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/impulse-graph/impulse-graph/go"
)

func TestVMExecutionAndContext(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "impulse_vm_test_*")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	snapPath := filepath.Join(tmpDir, "vm_graph.imps")

	// 1. Build test snapshot
	writer, err := impulse.NewWriter(snapPath, 1)
	if err != nil {
		t.Fatalf("failed to create writer: %v", err)
	}
	if err := writer.AddDomain(0, impulse.KeyTypeString, "Node"); err != nil {
		t.Fatalf("failed to add domain: %v", err)
	}
	// Graph: 0 -> 1, 0 -> 2
	rowOffsets := []uint32{0, 2, 2, 2}
	colIndices := []uint32{1, 2}
	if err := writer.AddRelation(0, 0, impulse.EncRaw, 3, 2, 0, rowOffsets, colIndices); err != nil {
		t.Fatalf("failed to add relation: %v", err)
	}
	if err := writer.Finalize(); err != nil {
		t.Fatalf("failed to finalize writer: %v", err)
	}

	snap, err := impulse.OpenSnapshot(snapPath)
	if err != nil {
		t.Fatalf("failed to open snapshot: %v", err)
	}
	defer snap.Close()

	// 2. Create VM Context
	ctx, err := impulse.NewVMContext(snap)
	if err != nil {
		t.Fatalf("failed to create VM context: %v", err)
	}
	defer ctx.Close()

	// Test Context Bitset
	hBitset := ctx.AcquireBitset()
	if hBitset < 0 {
		t.Fatalf("failed to acquire bitset handle")
	}
	defer ctx.ReleaseBitset(hBitset)

	ctx.BitsetAdd(hBitset, 42)
	if !ctx.BitsetTest(hBitset, 42) {
		t.Errorf("expected bit 42 to be set in bitset")
	}
	if ctx.BitsetTest(hBitset, 43) {
		t.Errorf("expected bit 43 NOT to be set in bitset")
	}

	// Test Context Float Vector
	hFloat := ctx.AcquireFloatVector()
	if hFloat < 0 {
		t.Fatalf("failed to acquire float vector handle")
	}
	defer ctx.ReleaseFloatVector(hFloat)

	ctx.FloatVectorSet(hFloat, 0, 3.14)
	floatVec := ctx.GetFloatVector(hFloat)
	if len(floatVec) == 0 || floatVec[0] != 3.14 {
		t.Errorf("expected float vector index 0 to be 3.14, got %v", floatVec)
	}

	// 3. Test VM Bytecode Execution
	state := impulse.NewVMState()
	state.SetQueryContext(ctx)

	// Instructions:
	// 0: OP_INIT_INPUT_NODE -> R0
	// 1: OP_CSR_WALK (rel=0, src=R0) -> R1 (collect bitset handle)
	// 2: OP_HALT
	bytecode := []impulse.Instruction{
		{Opcode: impulse.OpInitInputNode, DstReg: 0, Payload: 0},
		{Opcode: impulse.OpCSRWalk, DstReg: 1, Payload: 0}, // rel 0, src R0
		{Opcode: impulse.OpHalt, DstReg: 0, Payload: 0},
	}

	// Execute starting at seed node 0
	if err := impulse.ExecuteVM(bytecode, state, 0); err != nil {
		t.Fatalf("failed to execute VM bytecode: %v", err)
	}

	// Verify register R0 contains seed node 0
	if state.GetRegister(0) != 0 {
		t.Errorf("expected R0 register value 0, got %d", state.GetRegister(0))
	}
}
