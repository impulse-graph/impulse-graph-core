package impulse_test

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/impulse-graph/impulse-graph/go"
)

func TestDeltaLayerAndCompaction(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "impulse_delta_test_*")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	basePath := filepath.Join(tmpDir, "base.imps")
	compactedPath := filepath.Join(tmpDir, "compacted.imps")

	// 1. Build Base Snapshot
	writer, err := impulse.NewWriter(basePath, 1)
	if err != nil {
		t.Fatalf("failed to create writer: %v", err)
	}

	if err := writer.AddDomain(0, impulse.KeyTypeString, "User"); err != nil {
		t.Fatalf("failed to add domain: %v", err)
	}
	if err := writer.AddDomain(1, impulse.KeyTypeString, "Group"); err != nil {
		t.Fatalf("failed to add domain: %v", err)
	}

	// Initial edge: 0 -> 1
	rowOffsets := []uint32{0, 1, 1}
	colIndices := []uint32{1}
	if err := writer.AddRelation(0, 1, impulse.EncRaw, 2, 1, 0, rowOffsets, colIndices); err != nil {
		t.Fatalf("failed to add relation: %v", err)
	}
	if err := writer.Finalize(); err != nil {
		t.Fatalf("failed to finalize writer: %v", err)
	}

	// 2. Create Delta Layer and add edge (0 -> 2), tombstone (0 -> 1)
	delta, err := impulse.NewDeltaLayer(0, 1, "MEMBER_OF")
	if err != nil {
		t.Fatalf("failed to create delta layer: %v", err)
	}
	defer delta.Close()

	if err := delta.AddEdge(0, 2); err != nil {
		t.Fatalf("failed to add edge in delta: %v", err)
	}
	if err := delta.TombstoneEdge(0, 1); err != nil {
		t.Fatalf("failed to tombstone edge in delta: %v", err)
	}

	if !delta.IsTombstoned(0, 1) {
		t.Errorf("expected 0 -> 1 to be tombstoned")
	}
	if delta.IsTombstoned(0, 2) {
		t.Errorf("expected 0 -> 2 NOT to be tombstoned")
	}

	// 3. Compact Base + Delta into Compacted file
	baseSnap, err := impulse.OpenSnapshot(basePath)
	if err != nil {
		t.Fatalf("failed to open base snapshot: %v", err)
	}
	defer baseSnap.Close()

	deltas := []*impulse.DeltaLayer{delta}
	if err := baseSnap.CompactToFile(deltas, compactedPath); err != nil {
		t.Fatalf("failed to compact snapshot: %v", err)
	}

	// 4. Verify Compacted Snapshot can be opened cleanly
	compactedSnap, err := impulse.OpenSnapshot(compactedPath)
	if err != nil {
		t.Fatalf("failed to open compacted snapshot: %v", err)
	}
	defer compactedSnap.Close()

	if compactedSnap.RelationCount() != 1 {
		t.Errorf("expected 1 relation in compacted snapshot, got %d", compactedSnap.RelationCount())
	}
}
