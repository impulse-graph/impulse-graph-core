package main

import (
	"fmt"
	"log"
	"time"

	impulse "github.com/impulse-graph/impulse-graph/go"
)

func main() {
	snapshotPath := "../../datasets/twitter-2010/twitter-2010.imps"

	// 1. Open off-heap zero-copy binary snapshot (.imps)
	snap, err := impulse.OpenSnapshot(snapshotPath)
	if err != nil {
		log.Fatalf("Error opening snapshot: %v", err)
	}
	defer snap.Close()

	fmt.Printf("Impulse Graph Snapshot Opened:\n")
	fmt.Printf("  Magic:     0x%X\n", snap.Magic())
	fmt.Printf("  Version:   %d\n", snap.Version())
	fmt.Printf("  Domains:   %d\n", snap.DomainCount())
	fmt.Printf("  Relations: %d\n", snap.RelationCount())

	// 2. Direct Reachability Point Query
	isReachable := snap.IsReachable(0, 0, 1)
	fmt.Printf("\nPoint Reachability (Node 0 -> Node 1 on relation 0): %v\n", isReachable)

	// 3. Assemble impOps Bytecode Query via Fluent QueryBuilder API
	bytecode := impulse.NewQueryBuilder().
		InputNode(0).
		WalkEdge(0, 1).
		CollectArray(1).
		Compile()

	// 4. Allocate Off-Heap VM Execution Context & State Frame
	ctx, err := impulse.NewVMContext(snap)
	if err != nil {
		log.Fatalf("Failed to create VM Context: %v", err)
	}
	defer ctx.Close()

	state := impulse.NewVMState()
	state.SetQueryContext(ctx)

	// 5. Execute Query Off-Heap against Native ImpulseVM
	t0 := time.Now()
	err = impulse.ExecuteVM(bytecode, state, 0)
	elapsed := time.Since(t0)

	if err != nil {
		log.Fatalf("Query execution failed: %v", err)
	}

	fmt.Printf("\nImpulseVM Execution Results:\n")
	fmt.Printf("  Status Flags:    0x%X\n", state.Flags())
	fmt.Printf("  Result Reg R0:   %d\n", state.GetRegister(0))
	fmt.Printf("  Execution Time:  %v\n", elapsed)
}
