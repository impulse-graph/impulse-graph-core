package impulse_test

import (
	"os"
	"path/filepath"
	"runtime"
	"sync"
	"testing"

	impulse "github.com/impulse-graph/impulse-graph/go"
)

func TestVMV11ExecutionPoolingAndNoLeak(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "impulse_vm_v11_test_*")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	snapPath := filepath.Join(tmpDir, "vm_graph.imps")

	// 1. Build test snapshot: 0 -> 1, 0 -> 2
	writer, err := impulse.NewWriter(snapPath, 1)
	if err != nil {
		t.Fatalf("failed to create writer: %v", err)
	}
	if err := writer.AddDomain(0, impulse.KeyTypeString, "Node"); err != nil {
		t.Fatalf("failed to add domain: %v", err)
	}
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

	// 2. Build & validate v0.11 .impb program:
	//    0: OP_LOAD_PARAM.frontier F0, param=0, dom=0
	//    1: OP_WALK.fwd            F1, F0, rel=0
	//    2: OP_COLLECT.frontier    slot=0, F1
	//    3: OP_HALT
	words := []uint64{
		impulse.EncodeV11Word(0x07, 1, 0, 0, 0, 0, 0, 0),
		impulse.EncodeV11Word(0x30, 0, 0, 1, 0, 0, 0, 0),
		impulse.EncodeV11Word(0x57, 1, 0, 0, 1, 0, 0, 0),
		impulse.EncodeV11Word(0x00, 0, 0, 0, 0, 0, 0, 0),
	}
	impb := impulse.BuildImpbV11(words, 1)

	if st, badPC := impulse.ValidateBytecode(impb); st != impulse.VmOk {
		t.Fatalf("ValidateBytecode failed: status=%d badPC=%d", st, badPC)
	}

	plan, err := impulse.PlanMemory(impb, snap, 1)
	if err != nil {
		t.Fatalf("PlanMemory failed: %v", err)
	}
	if plan.GuaranteedScratchBytes == 0 {
		t.Fatalf("expected non-zero GuaranteedScratchBytes")
	}

	// 3. Execute on pooled workspace and verify outputs + pool reuse
	ws, err := snap.AcquireWorkspace(plan.GuaranteedScratchBytes)
	if err != nil {
		t.Fatalf("AcquireWorkspace failed: %v", err)
	}
	if st := ws.Execute(impb, snap, []uint64{0}, 0, 0); st != impulse.VmOk {
		snap.ReleaseWorkspace(ws)
		t.Fatalf("Execute failed: status=%d", st)
	}
	ids, err := ws.OutputU64Slice(0)
	if err != nil {
		snap.ReleaseWorkspace(ws)
		t.Fatalf("OutputU64Slice failed: %v", err)
	}
	if len(ids) != 2 || ids[0] != 1 || ids[1] != 2 {
		snap.ReleaseWorkspace(ws)
		t.Fatalf("expected [1 2], got %v", ids)
	}
	snap.ReleaseWorkspace(ws)

	ws2, err := snap.AcquireWorkspace(plan.GuaranteedScratchBytes)
	if err != nil {
		t.Fatalf("reacquire failed: %v", err)
	}
	if ws2 != ws {
		t.Errorf("expected pooled VmWorkspace reuse")
	}
	snap.ReleaseWorkspace(ws2)

	// 4. Concurrent multi-goroutine stress (verify race-free & leak-free)
	var wg sync.WaitGroup
	const workers = 8
	const iters = 250
	for g := 0; g < workers; g++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := 0; i < iters; i++ {
				w, err := snap.AcquireWorkspace(plan.GuaranteedScratchBytes)
				if err != nil {
					t.Errorf("AcquireWorkspace: %v", err)
					return
				}
				if st := w.Execute(impb, snap, []uint64{0}, 0, 0); st != impulse.VmOk {
					snap.ReleaseWorkspace(w)
					t.Errorf("Execute: status=%d", st)
					return
				}
				out, err := w.OutputU64Slice(0)
				snap.ReleaseWorkspace(w)
				if err != nil || len(out) != 2 {
					t.Errorf("unexpected output: %v err=%v", out, err)
					return
				}
			}
		}()
	}
	wg.Wait()
	runtime.GC()
}
