package main

import (
	"fmt"
	"log"
	"os"
	"path/filepath"

	impulse "github.com/impulse-graph/impulse-graph/go"
)

func main() {
	tmpDir, err := os.MkdirTemp("", "impulse_go_ex04_*")
	if err != nil {
		log.Fatal(err)
	}
	defer os.RemoveAll(tmpDir)

	snapPath := filepath.Join(tmpDir, "analytical.imps")
	w, err := impulse.NewWriter(snapPath, 1)
	if err != nil {
		log.Fatal(err)
	}
	if err := w.AddDomain(0, impulse.KeyTypeString, "Node"); err != nil {
		log.Fatal(err)
	}
	if err := w.AddRelation(0, 0, impulse.EncRaw, 4, 3, 0, []uint32{0, 2, 3, 3, 3}, []uint32{1, 2, 3}); err != nil {
		log.Fatal(err)
	}
	if err := w.Finalize(); err != nil {
		log.Fatal(err)
	}

	snap, err := impulse.OpenSnapshot(snapPath)
	if err != nil {
		log.Fatal(err)
	}
	defer snap.Close()

	words := []uint64{
		impulse.EncodeWord(0x07, 1, 0, 0, 0, 0, 0, 0),
		impulse.EncodeWord(0x30, 0, 0, 1, 0, 0, 0, 0),
		impulse.EncodeWord(0x57, 1, 0, 0, 1, 0, 0, 0),
		impulse.EncodeWord(0x00, 0, 0, 0, 0, 0, 0, 0),
	}
	impb := impulse.BuildImpb(words, 1)
	if st, badPC := impulse.ValidateBytecode(impb); st != impulse.VmOk {
		log.Fatalf("validation failed: status=%d badPC=%d", st, badPC)
	}
	plan, err := impulse.PlanMemory(impb, snap, 1)
	if err != nil {
		log.Fatal(err)
	}
	ws, err := snap.AcquireWorkspace(plan.GuaranteedScratchBytes)
	if err != nil {
		log.Fatal(err)
	}
	defer snap.ReleaseWorkspace(ws)

	if st := ws.Execute(impb, snap, []uint64{0}, 0, 0); st != impulse.VmOk {
		log.Fatalf("execute failed: status=%d", st)
	}
	ids, err := ws.OutputU64Slice(0)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Printf("Reachable from node 0: %v\n", ids)
}
