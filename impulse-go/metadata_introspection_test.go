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

	// GetRelationName, GetRelationEntry, and ergonomic list/lookup/predicate helpers
	if _, err := snap.GetRelationName(0); err != nil {
		t.Fatalf("GetRelationName(0) failed: %v", err)
	}
	relEntry, err := snap.GetRelationEntry(0)
	if err != nil || relEntry.CooOrder != 0 || relEntry.CooSrcBytes != 0 {
		t.Fatalf("unexpected relation entry: %+v err=%v", relEntry, err)
	}
	if !relEntry.HasCSR() || relEntry.IsVirtual() || relEntry.IsFlatForward() {
		t.Fatalf("unexpected relation predicates: csr=%v virt=%v flat=%v", relEntry.HasCSR(), relEntry.IsVirtual(), relEntry.IsFlatForward())
	}
	if snap.RelationSrcNodeIDWidth(relEntry) != 4 || snap.RelationTgtNodeIDWidth(relEntry) != 4 {
		t.Fatalf("expected 4B src/tgt widths, got %d/%d", snap.RelationSrcNodeIDWidth(relEntry), snap.RelationTgtNodeIDWidth(relEntry))
	}
	if doms, err := snap.Domains(); err != nil || len(doms) != 1 {
		t.Fatalf("Domains() failed: len=%d err=%v", len(doms), err)
	}
	if d, ok, err := snap.DomainByID(0); err != nil || !ok || d.Name != "Account" {
		t.Fatalf("DomainByID(0) failed: %+v ok=%v err=%v", d, ok, err)
	}
	if d, ok, err := snap.DomainByName("Account"); err != nil || !ok || d.DomainID != 0 {
		t.Fatalf("DomainByName(Account) failed: %+v ok=%v err=%v", d, ok, err)
	}
	if _, ok, err := snap.DomainByName("__missing__"); err != nil || ok {
		t.Fatalf("expected miss for DomainByName(__missing__)")
	}
	if rels, err := snap.Relations(); err != nil || len(rels) != 1 {
		t.Fatalf("Relations() failed: len=%d err=%v", len(rels), err)
	}
	if r, ok, err := snap.RelationByID(0); err != nil || !ok || r.RelationID != 0 {
		t.Fatalf("RelationByID(0) failed: %+v ok=%v err=%v", r, ok, err)
	}
	if _, ok, err := snap.RelationByName(relEntry.Name); err != nil || !ok {
		t.Fatalf("RelationByName(%q) failed: ok=%v err=%v", relEntry.Name, ok, err)
	}
	if _, ok, err := snap.RelationByName("__missing__"); err != nil || ok {
		t.Fatalf("expected miss for RelationByName(__missing__)")
	}
	if idxs, err := snap.Indices(); err != nil || len(idxs) != 0 {
		t.Fatalf("Indices() failed: len=%d err=%v", len(idxs), err)
	}
	if snap.MetadataCount() != 3 {
		t.Fatalf("expected MetadataCount=3, got %d", snap.MetadataCount())
	}

	// Out-of-bounds checks
	if _, err := snap.GetDomainEntry(99); err == nil {
		t.Fatalf("expected OOB error on GetDomainEntry(99)")
	}
	if _, err := snap.GetRelationEntry(99); err == nil {
		t.Fatalf("expected OOB error on GetRelationEntry(99)")
	}
	if _, err := snap.GetIndexEntry(99); err == nil {
		t.Fatalf("expected OOB error on GetIndexEntry(99)")
	}

	// Closed snapshot checks
	snap.Close()
	if snap.FileSize() != 0 || snap.IndexCount() != 0 || snap.MetadataCount() != 0 {
		t.Fatalf("expected 0 on closed snapshot")
	}
	if _, err := snap.Header(); err == nil {
		t.Fatalf("expected error on closed Header()")
	}
	if _, err := snap.SHA256(); err == nil {
		t.Fatalf("expected error on closed SHA256()")
	}
	if _, err := snap.Domains(); err == nil {
		t.Fatalf("expected error on closed Domains()")
	}
	if _, _, err := snap.DomainByID(0); err == nil {
		t.Fatalf("expected error on closed DomainByID()")
	}
	if _, _, err := snap.DomainByName("Account"); err == nil {
		t.Fatalf("expected error on closed DomainByName()")
	}
	if _, err := snap.Relations(); err == nil {
		t.Fatalf("expected error on closed Relations()")
	}
	if _, _, err := snap.RelationByID(0); err == nil {
		t.Fatalf("expected error on closed RelationByID()")
	}
	if _, err := snap.Indices(); err == nil {
		t.Fatalf("expected error on closed Indices()")
	}
	if _, err := snap.GetAllMetadata(); err == nil {
		t.Fatalf("expected error on closed GetAllMetadata()")
	}
	if _, err := snap.GetMetadata("a_key"); err == nil {
		t.Fatalf("expected error on closed GetMetadata()")
	}
}

func TestMetadataIntrospectionEdgeCasesAndFixtures(t *testing.T) {
	// 1. AttributeDescriptor.ValidityRange & Has64BitOffsets
	nullable := AttributeDescriptor{
		Name:         "score",
		TypeCode:     0x86,
		BaseTypeCode: 0x06,
		IsNullable:   true,
		DataOffset:   4096,
		DataBytes:    256,
	}
	if off, ln := nullable.ValidityRange(0); off != 0 || ln != 0 {
		t.Fatalf("ValidityRange(0) = (%d,%d), want (0,0)", off, ln)
	}
	if off, ln := nullable.ValidityRange(4); off != 4096 || ln != 128 {
		t.Fatalf("ValidityRange(4) = (%d,%d), want (4096,128)", off, ln)
	}
	if off, ln := nullable.ValidityRange(1024); off != 4096 || ln != 128 {
		t.Fatalf("ValidityRange(1024) = (%d,%d), want (4096,128)", off, ln)
	}
	if off, ln := nullable.ValidityRange(1025); off != 4096 || ln != 256 {
		t.Fatalf("ValidityRange(1025) = (%d,%d), want (4096,256)", off, ln)
	}
	if off, ln := nullable.ValidityRange(^uint64(0)); off != 4096 || ln != 256 {
		t.Fatalf("ValidityRange(MaxUint64) = (%d,%d), want (4096,256)", off, ln)
	}
	nonNullable := nullable
	nonNullable.IsNullable = false
	nonNullable.TypeCode = 0x06
	if off, ln := nonNullable.ValidityRange(4); off != 0 || ln != 0 {
		t.Fatalf("non-nullable ValidityRange(4) = (%d,%d), want (0,0)", off, ln)
	}

	// 2. v0.9 vs v0.10+ section_features bit 2 (RAW_UINT16 vs VIRTUAL)
	v09Rel := RelationDirectoryEntry{
		SnapshotVersion: 9,
		SectionFeatures: 0x04, // IMPULSE_SECTION_FEAT_RAW_UINT16 in v0.9
		CSRRowOffBytes:  16,
		CSRColIdxBytes:  8,
	}
	if v09Rel.IsVirtual() {
		t.Fatalf("v0.9 RAW_UINT16 relation must not be classified as virtual")
	}
	if !v09Rel.HasCSR() {
		t.Fatalf("v0.9 RAW_UINT16 relation with CSR bytes must report HasCSR()=true")
	}
	v11Virt := RelationDirectoryEntry{
		SnapshotVersion: 11,
		SectionFeatures: 0x04, // IMP_REL_FEATURE_VIRTUAL in v0.10+
		CSRColIdxBytes:  8,
	}
	if !v11Virt.IsVirtual() || v11Virt.HasCSR() || v11Virt.HasCSC() || v11Virt.HasCOO() {
		t.Fatalf("v0.11 virtual relation predicate mismatch")
	}

	// 3. Nil *Snapshot receiver safety
	var nilSnap *Snapshot
	nilSnap.Close()
	if nilSnap.Ptr() != nil || nilSnap.Magic() != 0 || nilSnap.Version() != 0 ||
		nilSnap.FileSize() != 0 || nilSnap.DomainCount() != 0 || nilSnap.RelationCount() != 0 ||
		nilSnap.IndexCount() != 0 || nilSnap.MetadataCount() != 0 || nilSnap.MaxNodeCount() != 0 ||
		nilSnap.IsReachable(0, 0, 1) || nilSnap.GetDomainAttributeCount(0) != 0 {
		t.Fatalf("nil Snapshot scalar methods returned non-zero")
	}
	if _, err := nilSnap.Header(); err == nil {
		t.Fatalf("nil Snapshot.Header() expected error")
	}
	if _, err := nilSnap.SHA256(); err == nil {
		t.Fatalf("nil Snapshot.SHA256() expected error")
	}
	if _, err := nilSnap.Domains(); err == nil {
		t.Fatalf("nil Snapshot.Domains() expected error")
	}
	if _, err := nilSnap.Relations(); err == nil {
		t.Fatalf("nil Snapshot.Relations() expected error")
	}
	if _, err := nilSnap.Indices(); err == nil {
		t.Fatalf("nil Snapshot.Indices() expected error")
	}
	if _, err := nilSnap.GetAllMetadata(); err == nil {
		t.Fatalf("nil Snapshot.GetAllMetadata() expected error")
	}
	if _, err := nilSnap.GetMetadata("k"); err == nil {
		t.Fatalf("nil Snapshot.GetMetadata() expected error")
	}

	// 4. Empty snapshot (0 domains, 0 relations, 0 indices, 0 metadata)
	emptyFile := filepath.Join(t.TempDir(), "empty.imps")
	ew, err := NewWriter(emptyFile, 1)
	if err != nil {
		t.Fatalf("NewWriter(empty) failed: %v", err)
	}
	if err := ew.Finalize(); err != nil {
		ew.Close()
		t.Fatalf("Finalize(empty) failed: %v", err)
	}
	ew.Close()
	esnap, err := OpenSnapshot(emptyFile)
	if err != nil {
		t.Fatalf("OpenSnapshot(empty) failed: %v", err)
	}
	defer esnap.Close()
	if esnap.DomainCount() != 0 || esnap.RelationCount() != 0 || esnap.IndexCount() != 0 || esnap.MetadataCount() != 0 {
		t.Fatalf("empty snapshot counts non-zero")
	}
	if _, err := esnap.GetRelationName(0); err == nil {
		t.Fatalf("expected OOB error on GetRelationName(0)")
	}
	if _, err := esnap.GetRelationVirtualComponents(0); err == nil {
		t.Fatalf("expected OOB error on GetRelationVirtualComponents(0)")
	}
	if _, err := esnap.GetAttributeInfo(0, 0); err == nil {
		t.Fatalf("expected OOB error on GetAttributeInfo(0,0)")
	}
	if _, err := esnap.GetDomainAttributeInfo(0, 0); err == nil {
		t.Fatalf("expected OOB error on GetDomainAttributeInfo(0,0)")
	}

	// 5. Populated secondary indices & live nullable domain/relation attributes (always executed)
	liveFile := filepath.Join(t.TempDir(), "live_idx_nullable.imps")
	lw, err := NewWriter(liveFile, 1)
	if err != nil {
		t.Fatalf("NewWriter(live) failed: %v", err)
	}
	if err := lw.AddDomain(0, KeyTypeInt32, "User"); err != nil {
		lw.Close()
		t.Fatalf("AddDomain failed: %v", err)
	}
	if err := lw.SetDomainNodeCount(0, 3); err != nil {
		lw.Close()
		t.Fatalf("SetDomainNodeCount failed: %v", err)
	}
	if err := lw.AddDomainAttributeDecimal64Nullable(0, "score", 1, 10, 2, []NullableDecimal64{
		{Value: Decimal64{Unscaled: 1234, Scale: 2}, Valid: true},
		{Valid: false},
		{Value: Decimal64{Unscaled: 5678, Scale: 2}, Valid: true},
	}); err != nil {
		lw.Close()
		t.Fatalf("AddDomainAttributeDecimal64Nullable failed: %v", err)
	}
	if err := lw.AddRelation(0, 0, EncRaw, 3, 2, 0, []uint32{0, 1, 2, 2}, []uint32{1, 2}); err != nil {
		lw.Close()
		t.Fatalf("AddRelation failed: %v", err)
	}
	if err := lw.AddAttributeDecimal64Nullable(0, "weight", 1, 8, 2, []NullableDecimal64{
		{Value: Decimal64{Unscaled: 250, Scale: 2}, Valid: true},
		{Valid: false},
	}); err != nil {
		lw.Close()
		t.Fatalf("AddAttributeDecimal64Nullable failed: %v", err)
	}
	idxPayload := make([]byte, 128)
	idxPayload[0] = 0x42
	if err := lw.AddIndex(0, 0xFFFF, 0, 1, "idx_user_score", idxPayload, 0x01); err != nil {
		lw.Close()
		t.Fatalf("AddIndex failed: %v", err)
	}
	if err := lw.Finalize(); err != nil {
		lw.Close()
		t.Fatalf("Finalize(live) failed: %v", err)
	}
	lw.Close()

	lsnap, err := OpenSnapshot(liveFile)
	if err != nil {
		t.Fatalf("OpenSnapshot(live) failed: %v", err)
	}
	defer lsnap.Close()
	if lsnap.IndexCount() != 1 {
		t.Fatalf("expected IndexCount=1, got %d", lsnap.IndexCount())
	}
	idxs, err := lsnap.Indices()
	if err != nil || len(idxs) != 1 {
		t.Fatalf("Indices() mismatch: len=%d err=%v", len(idxs), err)
	}
	ie0, err := lsnap.GetIndexEntry(0)
	if err != nil || ie0.IndexID != idxs[0].IndexID || ie0.Name != "idx_user_score" || ie0.DataBytes != 128 || ie0.PayloadFeatureMask != 0x01 {
		t.Fatalf("GetIndexEntry(0) mismatch: %+v err=%v", ie0, err)
	}
	da, err := lsnap.GetDomainAttributeInfo(0, 0)
	if err != nil || !da.IsNullable || da.BaseTypeCode != 0x15 || da.Precision != 10 || da.Scale != 2 {
		t.Fatalf("live nullable domain attr mismatch: %+v err=%v", da, err)
	}
	if off, ln := da.ValidityRange(3); off != da.DataOffset || ln != 128 {
		t.Fatalf("live domain ValidityRange(3) = (%d,%d), want (%d,128)", off, ln, da.DataOffset)
	}
	ra, err := lsnap.GetAttributeInfo(0, 0)
	if err != nil || !ra.IsNullable || ra.BaseTypeCode != 0x15 || ra.Precision != 8 || ra.Scale != 2 {
		t.Fatalf("live nullable rel attr mismatch: %+v err=%v", ra, err)
	}
	if off, ln := ra.ValidityRange(2); off != ra.DataOffset || ln != 128 {
		t.Fatalf("live rel ValidityRange(2) = (%d,%d), want (%d,128)", off, ln, ra.DataOffset)
	}

	// 6. Spec version constants
	if VersionMinor != 11 || VersionString != "0.11.0" {
		t.Fatalf("expected version 0.11.0, got 0.%d.0 / %q", VersionMinor, VersionString)
	}
}
