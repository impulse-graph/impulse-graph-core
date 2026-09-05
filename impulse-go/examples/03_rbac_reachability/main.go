package main

import (
	"fmt"
	"log"
	"os"
	"time"

	impulse "github.com/impulse-graph/impulse-graph/go"
)

func main() {
	fmt.Println("===============================================================")
	fmt.Println(" Impulse Graph Engine — Example 03: ReBAC Authorization (Go)")
	fmt.Println("===============================================================")
	fmt.Println()

	snapshotPath := "rbac_snapshot.imps"
	snap, err := impulse.OpenSnapshot(snapshotPath)

	isTemp := false
	tempPath := "temp_rbac_snapshot.imps"

	if err != nil {
		fmt.Printf("[INFO] '%s' not found in $IMPULSE_DATASETS_DIR or local paths.\n", snapshotPath)
		fmt.Println("[INFO] Generating fallback ReBAC snapshot...")
		isTemp = true

		writer, err := impulse.NewWriter(tempPath, 0)
		if err != nil {
			log.Fatalf("Failed to create writer: %v", err)
		}
		writer.AddDomain(0, impulse.KeyTypeString, "User")
		writer.AddDomain(1, impulse.KeyTypeString, "Role")
		writer.AddDomain(2, impulse.KeyTypeString, "Permission")

		// Relation 0: User -> Role (User 0 is Admin(0) and Editor(1))
		uROwOff := []uint32{0, 2, 3, 4}
		uRColIdx := []uint32{0, 1, 1, 2}
		writer.AddRelation(0, 1, 0, 3, 4, 0, uROwOff, uRColIdx)

		// Relation 1: Role -> Permission (Admin(0) -> [Read(0), Write(1), Delete(2)])
		rPOwOff := []uint32{0, 3, 4, 5}
		rPColIdx := []uint32{0, 1, 2, 0, 0}
		writer.AddRelation(1, 2, 0, 3, 5, 0, rPOwOff, rPColIdx)

		writer.Finalize()
		writer.Close()

		snap, err = impulse.OpenSnapshot(tempPath)
		if err != nil {
			log.Fatalf("Failed to open fallback ReBAC snapshot: %v", err)
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
	// Step 1: ReBAC Multi-Hop Permission Traversal
	// ------------------------------------------------------------------------
	fmt.Println("\n1. ReBAC Policy: Check permissions for User 0 (User -> Role -> Permission):")

	t0 := time.Now()
	effectivePermissions, err := snap.Traverse("0", 0).
		Out("1"). // Walk Role -> Permission
		ToSlice()
	elapsed := time.Since(t0)

	if err != nil {
		log.Fatalf("ReBAC query failed: %v", err)
	}

	fmt.Printf("   -> ReBAC Evaluation Latency: %v\n", elapsed)
	fmt.Printf("   -> Reached Permission IDs: %v\n", effectivePermissions)

	// ------------------------------------------------------------------------
	// Step 2: Policy Decision Evaluation
	// ------------------------------------------------------------------------
	fmt.Println("\n2. Effective Permissions Evaluation for User 0:")
	permLabels := []struct {
		name string
		id   uint64
	}{
		{"READ", 0},
		{"WRITE", 1},
		{"DELETE", 2},
	}

	permSet := make(map[uint64]bool)
	for _, p := range effectivePermissions {
		permSet[p] = true
	}

	for _, perm := range permLabels {
		allowed := permSet[perm.id]
		statusStr := "DENIED  [✗]"
		if allowed {
			statusStr = "ALLOWED [✓]"
		}
		fmt.Printf("   -> Permission %s (%d): %s\n", perm.name, perm.id, statusStr)
	}

	fmt.Println("\n[SUCCESS] Example 03 completed cleanly.")
}
