package main

import (
	"fmt"
	"log"
	"os"
	"time"

	impulse "github.com/impulse-graph/impulse-graph/go"
)

func main() {
	fmt.Println("===================================================================")
	fmt.Println(" Impulse Graph Engine — Example 04: Analytical VM Queries (Go)")
	fmt.Println("===================================================================\n")

	snapshotPath := "financial_transactions.imps"
	snap, err := impulse.OpenSnapshot(snapshotPath)

	isTemp := false
	tempPath := "temp_transactions.imps"

	if err != nil {
		fmt.Printf("[INFO] '%s' not found in $IMPULSE_DATASETS_DIR or local paths.\n", snapshotPath)
		fmt.Println("[INFO] Generating sample transaction snapshot...")
		isTemp = true

		writer, err := impulse.NewWriter(tempPath)
		if err != nil {
			log.Fatalf("Failed to create writer: %v", err)
		}
		writer.AddDomain(0, impulse.KeyTypeInt64, "Account")

		// 5 Accounts, 6 Transfer Edges
		rowOffsets := []uint32{0, 3, 4, 6, 6, 6}
		colIndices := []uint32{1, 2, 3, 3, 0, 4}
		writer.AddRelation(0, 0, 0, 5, 6, 0, rowOffsets, colIndices)
		writer.Finalize()
		writer.Close()

		snap, err = impulse.OpenSnapshot(tempPath)
		if err != nil {
			log.Fatalf("Failed to open fallback snapshot: %v", err)
		}
	} else {
		fmt.Printf("[INFO] Successfully resolved and opened '%s'.\n", snapshotPath)
	}

	defer func() {
		snap.Close()
		if isTemp {
			os.Remove(tempPath)
		}
	}()

	// ------------------------------------------------------------------------
	// Step 1: Programmatic VM Bytecode Generation
	// ------------------------------------------------------------------------
	fmt.Println("\n1. Compiling Low-Level impOps Bytecode with QueryBuilder:")

	bytecode := impulse.NewQueryBuilder().
		InputNode(0).     // R0: Seed Account 0
		WalkEdge(0, 1).   // R1: 1-hop transfer recipients
		CollectArray(1).  // Collect array of target IDs
		Compile()

	fmt.Printf("   -> Generated %d bytes of impOps binary bytecode.\n", len(bytecode))

	// ------------------------------------------------------------------------
	// Step 2: Off-Heap VmContext Allocation & Execution
	// ------------------------------------------------------------------------
	fmt.Println("\n2. Executing against ImpulseVM:")
	ctx, err := impulse.NewVMContext(snap)
	if err != nil {
		log.Fatalf("Failed to create VM Context: %v", err)
	}
	defer ctx.Close()

	state := impulse.NewVMState()
	state.SetQueryContext(ctx)

	t0 := time.Now()
	err = impulse.ExecuteVM(bytecode, state, 0)
	elapsed := time.Since(t0)

	if err != nil {
		log.Fatalf("Query execution failed: %v", err)
	}

	fmt.Printf("   -> VM Execution Time:  %v\n", elapsed)
	fmt.Printf("   -> VM Status Flags:    0x%X\n", state.Flags())
	fmt.Printf("   -> Result Register R1: %d\n", state.GetRegister(1))

	fmt.Println("\n[SUCCESS] Example 04 completed cleanly.")
}
