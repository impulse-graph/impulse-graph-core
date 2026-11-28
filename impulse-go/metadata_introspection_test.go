package impulse

import (
	"crypto/sha256"
	"os"
	"path/filepath"
	"testing"
)

func TestPhase1MetadataIntrospection(t *testing.T) {
	tmpFile := filepath.Join(t.TempDir(), "phase1_meta.imps")

	w, err := NewWriter(tmpFile, 1)
	if err != nil {
		t.Fatalf("NewWriter failed: %v", err)
	}
	defer w.Close()

	if err := w.AddDomain(0, KeyTypeInt32, "Account"); err != nil {
		t.Fatalf("AddDomain failed: %v", err)
	}
	if err := w.SetDomainNodeCount(0, 3); err != nil {
		t.Fatalf("SetDomainNodeCount failed: %v", err)
	}
	if err := w.AddDomainAttributeDecimal64(0, "balance", AttrTypeDecimal64, 1, 12, 2, []int64{10050, 20075, 30000}); err != nil {
		t.Fatalf("AddDomainAttributeDecimal64 failed: %v", err)
	}
	if err := w.AddRelation(0, 0, EncRaw, 3, 2, 0, []uint32{0, 1, 2, 2}, []uint32{1, 2}); err != nil {
		t.Fatalf("AddRelation failed: %v", err)
	}
	if err := w.AddAttributeDecimal64(0, "amount", AttrTypeDecimal64, 1, 10, 2, []int64{500, 1250}); err != nil {
		t.Fatalf("AddAttributeDecimal64 failed: %v", err)
	}
	if err := w.SetMetadata("z_key", "last"); err != nil {
		t.Fatalf("SetMetadata failed: %v", err)
	}
	if err := w.SetMetadata("a_key", "first"); err != nil {
		t.Fatalf("SetMetadata failed: %v", err)
	}
	largeVal := make([]byte, 8192)
	for i := range largeVal {
		largeVal[i] = 'X'
	}
	if err := w.SetMetadata("big_key", string(largeVal)); err != nil {
		t.Fatalf("SetMetadata(big_key) failed: %v", err)
	}
	if err := w.Finalize(); err != nil {
		t.Fatalf("Finalize failed: %v", err)
	}

	snap, err := OpenSnapshot(tmpFile)
	if err != nil {
		t.Fatalf("OpenSnapshot failed: %v", err)
	}
	defer snap.Close()

	hdr, err := snap.Header()
	if err != nil {
		t.Fatalf("Header failed: %v", err)
	}
	if hdr.Magic != 0x494D5053 || hdr.Version != 11 || hdr.DomainCount != 1 || hdr.RelationCount != 1 {
		t.Fatalf("unexpected header: %+v", hdr)
	}
	rawBytes, err := os.ReadFile(tmpFile)
	if err != nil {
		t.Fatalf("ReadFile failed: %v", err)
	}
	if hdr.FileSize != uint64(len(rawBytes)) || snap.FileSize() != hdr.FileSize {
		t.Fatalf("file size mismatch: hdr=%d api=%d actual=%d", hdr.FileSize, snap.FileSize(), len(rawBytes))
	}
	gotSha, err := snap.SHA256()
	if err != nil {
		t.Fatalf("SHA256 failed: %v", err)
	}
	if gotSha != sha256.Sum256(rawBytes) {
		t.Fatalf("SHA256 mismatch")
	}

	dom, err := snap.GetDomainEntry(0)
	if err != nil {
		t.Fatalf("GetDomainEntry failed: %v", err)
	}
	if dom.Name != "Account" || dom.NodeCount != 3 || dom.AttrCount != 1 || dom.KeyAttrIndex != 0xFFFF {
		t.Fatalf("unexpected domain: %+v", dom)
	}

	da, err := snap.GetDomainAttributeInfo(0, 0)
	if err != nil {
		t.Fatalf("GetDomainAttributeInfo failed: %v", err)
	}
	if da.Name != "balance" || da.TypeCode != 0x15 || da.Precision != 12 || da.Scale != 2 || da.DataBytes != 24 {
		t.Fatalf("unexpected domain attr: %+v", da)
	}

	ra, err := snap.GetAttributeInfo(0, 0)
	if err != nil {
		t.Fatalf("GetAttributeInfo failed: %v", err)
	}
	if ra.Name != "amount" || ra.TypeCode != 0x15 || ra.Precision != 10 || ra.Scale != 2 || ra.DataBytes != 16 {
		t.Fatalf("unexpected rel attr: %+v", ra)
	}

	vcomps, err := snap.GetRelationVirtualComponents(0)
	if err != nil || len(vcomps) != 0 {
		t.Fatalf("expected 0 virtual components, got %v err=%v", vcomps, err)
	}
	if snap.IndexCount() != 0 {
		t.Fatalf("expected 0 indices, got %d", snap.IndexCount())
	}

	allMeta, err := snap.GetAllMetadata()
	if err != nil {
		t.Fatalf("GetAllMetadata failed: %v", err)
	}
	if len(allMeta) != 3 || allMeta["a_key"] != "first" || allMeta["z_key"] != "last" || len(allMeta["big_key"]) != 8192 {
		t.Fatalf("unexpected metadata map: len=%d", len(allMeta))
	}

	// H3 regression: GetMetadata supports >4096B values via O(log N) binary search
	gotBig, err := snap.GetMetadata("big_key")
	if err != nil || len(gotBig) != 8192 {
		t.Fatalf("GetMetadata(big_key) failed: len=%d err=%v", len(gotBig), err)
	}
	if _, err := snap.GetMetadata("missing_key"); err == nil {
		t.Fatalf("expected error for missing metadata key")
	}

	// GetRelationName & GetRelationEntry (v0.11 COO fields)
	if _, err := snap.GetRelationName(0); err != nil {
		t.Fatalf("GetRelationName(0) failed: %v", err)
	}
	relEntry, err := snap.GetRelationEntry(0)
	if err != nil || relEntry.CooOrder != 0 || relEntry.CooSrcBytes != 0 {
		t.Fatalf("unexpected relation entry: %+v err=%v", relEntry, err)
	}

	// Closed snapshot checks
	snap.Close()
	if snap.FileSize() != 0 || snap.IndexCount() != 0 {
		t.Fatalf("expected 0 on closed snapshot")
	}
	if _, err := snap.Header(); err == nil {
		t.Fatalf("expected error on closed Header()")
	}
	if _, err := snap.GetMetadata("a_key"); err == nil {
		t.Fatalf("expected error on closed GetMetadata()")
	}
}
