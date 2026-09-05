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
	fmt.Println(" Impulse Graph Engine — Example 02: Social Graph Traversal (Go)")
	fmt.Println("===============================================================")
	fmt.Println()

	snapshotPath := "social_graph.imps"
	snap, err := impulse.OpenSnapshot(snapshotPath)

	isTemp := false
	tempPath := "temp_social_graph.imps"

	if err != nil {
		fmt.Printf("[INFO] '%s' not found in $IMPULSE_DATASETS_DIR or local paths.\n", snapshotPath)
		fmt.Println("[INFO] Generating fallback in-memory sample social graph...")
		isTemp = true

		writer, err := impulse.NewWriter(tempPath, 0)
		if err != nil {
			log.Fatalf("Failed to create writer: %v", err)
		}
		writer.AddDomain(0, impulse.KeyTypeInt64, "User")

		// 8 Users with follow relations
		rowOffsets := []uint32{0, 2, 4, 6, 8, 9, 10, 11, 11}
		colIndices := []uint32{1, 2, 2, 3, 3, 4, 4, 5, 6, 7, 0}
		writer.AddRelation(0, 0, 0, 8, 11, 0, rowOffsets, colIndices)
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
	// Step 1: Execute Fluent 2-Hop Traversal Pipeline
	// ------------------------------------------------------------------------
	fmt.Println("\n1. Constructing Fluent Traversal Pipeline:")
	fmt.Println("   Query: Seed(User 0) -> Out(\"0\") -> Out(\"0\")")

	t0 := time.Now()
	hop2Friends, err := snap.Traverse("0", 0).
		Out("0").
		ToSlice()
	elapsed := time.Since(t0)

	if err != nil {
		log.Fatalf("Traversal failed: %v", err)
	}

	fmt.Printf("   -> Traversal Latency: %v\n", elapsed)
	fmt.Printf("   -> 2-Hop Friends-of-Friends from User 0: %v\n", hop2Friends)

	// ------------------------------------------------------------------------
	// Step 2: Immediate 1-Hop Outgoing Neighbors
	// ------------------------------------------------------------------------
	fmt.Println("\n2. Inspecting 1-Hop Outgoing Edges for User 0:")
	directFriends, _ := snap.Traverse("0", 0).ToSlice()
	fmt.Printf("   -> User 0 follows: %v\n", directFriends)

	fmt.Println("\n[SUCCESS] Example 02 completed cleanly.")
}
