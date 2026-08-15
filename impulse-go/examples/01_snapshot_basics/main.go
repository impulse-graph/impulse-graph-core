package main

import (
	"fmt"
	"log"
	"os"

	impulse "github.com/impulse-graph/impulse-graph/go"
)

func main() {
	fmt.Println("===============================================================")
	fmt.Println(" Impulse Graph Engine — Example 01: Snapshot Basics (Go)")
	fmt.Println("===============================================================\n")

	snapshotPath := "sample_basics.imps"

	// ------------------------------------------------------------------------
	// Step 1: Programmatic Snapshot Creation with Writer
	// ------------------------------------------------------------------------
	fmt.Printf("1. Creating binary snapshot file: %s...\n", snapshotPath)

	writer, err := impulse.NewWriter(snapshotPath)
	if err != nil {
		log.Fatalf("Failed to create writer: %v", err)
	}

	writer.AddDomain(0, impulse.KeyTypeInt64, "User")

	// CSR Topology: 4 Users (0, 1, 2, 3)
	// Node 0 -> [1, 2]
	// Node 1 -> [2, 3]
	// Node 2 -> [3]
	// Node 3 -> []
	rowOffsets := []uint32{0, 2, 4, 5, 5}
	colIndices := []uint32{1, 2, 2, 3, 3}

	err = writer.AddRelation(0, 0, 0, 4, 5, 0, rowOffsets, colIndices)
	if err != nil {
		writer.Close()
		log.Fatalf("Failed to add relation: %v", err)
	}

	if err := writer.Finalize(); err != nil {
		writer.Close()
		log.Fatalf("Failed to finalize snapshot: %v", err)
	}
	writer.Close()

	fi, _ := os.Stat(snapshotPath)
	fmt.Printf("   -> Successfully wrote snapshot (%d bytes).\n\n", fi.Size())

	// ------------------------------------------------------------------------
	// Step 2: Zero-Copy Memory-Mapped Reading with OpenSnapshot
	// ------------------------------------------------------------------------
	fmt.Println("2. Opening snapshot via zero-copy OS memory mapping...")
	snap, err := impulse.OpenSnapshot(snapshotPath)
	if err != nil {
		os.Remove(snapshotPath)
		log.Fatalf("Failed to open snapshot: %v", err)
	}
	defer func() {
		snap.Close()
		os.Remove(snapshotPath)
	}()

	fmt.Printf("   -> Magic:     0x%X ('IMPS')\n", snap.Magic())
	fmt.Printf("   -> Version:   %d\n", snap.Version())
	fmt.Printf("   -> Domains:   %d\n", snap.DomainCount())
	fmt.Printf("   -> Relations: %d\n\n", snap.RelationCount())

	// ------------------------------------------------------------------------
	// Step 3: Direct Point Reachability Queries
	// ------------------------------------------------------------------------
	fmt.Println("3. Direct Point Reachability Queries:")
	fmt.Printf("   -> Node 0 -> Node 1 reachable? %v\n", snap.IsReachable(0, 0, 1))
	fmt.Printf("   -> Node 0 -> Node 3 reachable? %v\n", snap.IsReachable(0, 0, 3))

	fmt.Println("\n[SUCCESS] Example 01 completed cleanly.")
}
