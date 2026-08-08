package impulse_test

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/impulse-graph/impulse-graph/go"
)

func TestSnapshotAPIsAndSampling(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "impulse_snap_test_*")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	snapPath := filepath.Join(tmpDir, "sample_graph.imps")

	// 1. Build test snapshot
	writer, err := impulse.NewWriter(snapPath, 1)
	if err != nil {
		t.Fatalf("failed to create writer: %v", err)
	}

	if err := writer.AddDomain(0, impulse.KeyTypeString, "Node"); err != nil {
		t.Fatalf("failed to add domain: %v", err)
	}

	// CSR topology: 0 -> [1, 2, 3], 1 -> [2, 3], 2 -> [3], 3 -> []
	rowOffsets := []uint32{0, 3, 5, 6, 6}
	colIndices := []uint32{1, 2, 3, 2, 3, 3}

	if err := writer.AddRelation(0, 0, impulse.EncRaw, 4, 6, 0, rowOffsets, colIndices); err != nil {
		t.Fatalf("failed to add relation: %v", err)
	}

	// Add float attribute
	attrData := []byte{0, 0, 128, 63, 0, 0, 0, 64} // 1.0f, 2.0f
	if err := writer.AddAttribute(0, "weights", 0x08, 1, attrData, nil); err != nil {
		t.Fatalf("failed to add attribute: %v", err)
	}

	if err := writer.Finalize(); err != nil {
		t.Fatalf("failed to finalize writer: %v", err)
	}

	// 2. Open snapshot and test APIs
	snap, err := impulse.OpenSnapshot(snapPath)
	if err != nil {
		t.Fatalf("failed to open snapshot: %v", err)
	}
	defer snap.Close()

	if snap.Version() != impulse.VersionPatch {
		t.Logf("Snapshot spec version: %d", snap.Version())
	}
	if snap.MaxNodeCount() != 4 {
		t.Errorf("expected max node count 4, got %d", snap.MaxNodeCount())
	}

	// Test SampleNeighbors
	srcNodes := []uint64{0, 1}
	sampledSrc, sampledTgt, err := snap.SampleNeighbors(0, srcNodes, 2, 42)
	if err != nil {
		t.Fatalf("failed to sample neighbors: %v", err)
	}
	t.Logf("Sampled %d edges: src=%v, tgt=%v", len(sampledSrc), sampledSrc, sampledTgt)
	if len(sampledSrc) == 0 {
		t.Errorf("expected non-zero sampled neighbors")
	}

	// Test CSC Buffers
	cscOffsets, cscTargets, rowCnt, edgeCnt, err := snap.GetRelationCSCBuffers(0)
	if err != nil {
		t.Fatalf("failed to get CSC buffers: %v", err)
	}
	t.Logf("CSC buffers: rowCnt=%d, edgeCnt=%d, cscOffsets len=%d, cscTargets len=%d", rowCnt, edgeCnt, len(cscOffsets), len(cscTargets))

	// Test Attribute Buffers
	data, offsets, typeCode, dim, err := snap.GetAttributeBuffers(0, 0)
	if err != nil {
		t.Fatalf("failed to get attribute buffers: %v", err)
	}
	if len(data) == 0 || typeCode != 0x08 || dim != 1 {
		t.Errorf("attribute buffer mismatch: data len=%d, typeCode=%d, dim=%d", len(data), typeCode, dim)
	}
	_ = offsets
}
