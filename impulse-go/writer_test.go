package impulse_test

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/impulse-graph/impulse-graph/go"
)

func TestWriterAndSnapshotRoundtrip(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "impulse_go_test_*")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	snapPath := filepath.Join(tmpDir, "test_graph.imps")

	// 1. Build a simple snapshot via Writer
	writer, err := impulse.NewWriter(snapPath, 1) // 1 = 4KB page aligned
	if err != nil {
		t.Fatalf("failed to create writer: %v", err)
	}
	defer writer.Close()

	// Add Domain 0: User, Domain 1: Document
	if err := writer.AddDomain(0, impulse.KeyTypeString, "User"); err != nil {
		t.Fatalf("failed to add User domain: %v", err)
	}
	if err := writer.AddDomain(1, impulse.KeyTypeString, "Document"); err != nil {
		t.Fatalf("failed to add Document domain: %v", err)
	}

	// Add Relation: User -> READS -> Document
	// Graph topology:
	// Node 0 -> [1, 2]
	// Node 1 -> [2]
	// Node 2 -> []
	rowOffsets := []uint32{0, 2, 3, 3}
	colIndices := []uint32{1, 2, 2}

	err = writer.AddRelation(
		0, 1, // srcDomain, tgtDomain
		impulse.EncRaw,
		3, 3, // nodeCount, edgeCount
		0, // sectionFeatures
		rowOffsets,
		colIndices,
	)
	if err != nil {
		t.Fatalf("failed to add relation: %v", err)
	}

	if err := writer.SetMetadata("owner", "unit-test"); err != nil {
		t.Fatalf("failed to set metadata: %v", err)
	}

	if err := writer.Finalize(); err != nil {
		t.Fatalf("failed to finalize writer: %v", err)
	}

	// 2. Open snapshot and verify data
	snap, err := impulse.OpenSnapshot(snapPath)
	if err != nil {
		t.Fatalf("failed to open snapshot: %v", err)
	}
	defer snap.Close()

	if snap.Magic() != impulse.MagicConstant {
		t.Errorf("expected magic 0x%X, got 0x%X", impulse.MagicConstant, snap.Magic())
	}
	if snap.DomainCount() != 2 {
		t.Errorf("expected 2 domains, got %d", snap.DomainCount())
	}
	if snap.RelationCount() != 1 {
		t.Errorf("expected 1 relation, got %d", snap.RelationCount())
	}

	// Test reachability
	if !snap.IsReachable(0, 0, 1) {
		t.Errorf("expected 0 -> 1 to be reachable")
	}
	if !snap.IsReachable(0, 0, 2) {
		t.Errorf("expected 0 -> 2 to be reachable")
	}
	if snap.IsReachable(0, 1, 0) {
		t.Errorf("expected 1 -> 0 NOT to be reachable")
	}

	// Test relation directory entry metadata
	entry, err := snap.GetRelationEntry(0)
	if err != nil {
		t.Fatalf("failed to get relation entry: %v", err)
	}
	if entry.NodeCount != 3 || entry.EdgeCount != 3 {
		t.Errorf("relation entry mismatch: nodes=%d, edges=%d", entry.NodeCount, entry.EdgeCount)
	}

	// Test metadata
	val, err := snap.GetMetadata("owner")
	if err != nil {
		t.Fatalf("failed to get metadata: %v", err)
	}
	if val != "unit-test" {
		t.Errorf("expected metadata 'unit-test', got '%s'", val)
	}

	// Test GetRelationBuffers
	offsets, targets, nCnt, eCnt, err := snap.GetRelationBuffers(0)
	if err != nil {
		t.Fatalf("failed to get relation buffers: %v", err)
	}
	if nCnt != 3 || eCnt != 3 {
		t.Errorf("expected 3 nodes, 3 edges; got %d, %d", nCnt, eCnt)
	}
	if len(offsets) != 4 || len(targets) != 3 {
		t.Errorf("buffers len mismatch: offsets=%d, targets=%d", len(offsets), len(targets))
	}
}
