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

	// Add float32 edge attribute (6 edges * 4 bytes = 24 bytes, type_code 0x06 = FLOAT32)
	attrData := []byte{
		0, 0, 128, 63, // 1.0f
		0, 0, 0, 64,   // 2.0f
		0, 0, 64, 64,  // 3.0f
		0, 0, 128, 64, // 4.0f
		0, 0, 160, 64, // 5.0f
		0, 0, 192, 64, // 6.0f
	}
	if err := writer.AddAttribute(0, "weights", 0x06, 1, attrData, nil); err != nil {
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
	if len(data) == 0 || typeCode != 0x06 || dim != 1 {
		t.Errorf("attribute buffer mismatch: data len=%d, typeCode=%d, dim=%d", len(data), typeCode, dim)
	}
	_ = offsets
}

func TestDecimal64ValueTypeAndParsing(t *testing.T) {
	d1, err := impulse.ParseDecimal64("123.45", 10, 2)
	if err != nil {
		t.Fatalf("ParseDecimal64 failed: %v", err)
	}
	if d1.Unscaled != 12345 || d1.Precision != 10 || d1.Scale != 2 {
		t.Fatalf("unexpected Decimal64: %+v", d1)
	}
	if d1.String() != "123.45" {
		t.Errorf("expected String() '123.45', got %q", d1.String())
	}
	if d1.Rat().RatString() != "2469/20" {
		t.Errorf("expected Rat() '2469/20', got %q", d1.Rat().RatString())
	}
	if d1.Float64() != 123.45 {
		t.Errorf("expected Float64() 123.45, got %v", d1.Float64())
	}

	d2, err := impulse.NewDecimal64(1234500, 10, 4)
	if err != nil {
		t.Fatalf("NewDecimal64 failed: %v", err)
	}
	if !d1.Equal(d2) {
		t.Errorf("expected cross-scale equality between %s and %s", d1, d2)
	}

	// Negative sub-unit formatting
	neg, err := impulse.ParseDecimal64("-0.0050", 18, 4)
	if err != nil {
		t.Fatalf("ParseDecimal64 negative failed: %v", err)
	}
	if neg.Unscaled != -50 || neg.String() != "-0.0050" {
		t.Errorf("unexpected negative Decimal64: unscaled=%d str=%q", neg.Unscaled, neg.String())
	}

	// Half-away-from-zero rounding
	r1, err := impulse.ParseDecimal64("99.995", 10, 2)
	if err != nil || r1.String() != "100.00" {
		t.Errorf("expected 99.995 -> '100.00', got %q (err=%v)", r1.String(), err)
	}
	r2, err := impulse.ParseDecimal64("-1.235", 10, 2)
	if err != nil || r2.String() != "-1.24" {
		t.Errorf("expected -1.235 -> '-1.24', got %q (err=%v)", r2.String(), err)
	}

	// > 2^53 precision preservation (9007199254740993.01)
	bigDec, err := impulse.ParseDecimal64("9007199254740993.01", 18, 2)
	if err != nil {
		t.Fatalf("ParseDecimal64 >2^53 failed: %v", err)
	}
	if bigDec.Unscaled != 900719925474099301 || bigDec.String() != "9007199254740993.01" {
		t.Errorf("precision lost for >2^53: unscaled=%d str=%q", bigDec.Unscaled, bigDec.String())
	}

	// Validation errors
	if _, err := impulse.NewDecimal64(1, 0, 0); err == nil {
		t.Errorf("expected error for precision=0")
	}
	if _, err := impulse.NewDecimal64(1, 19, 0); err == nil {
		t.Errorf("expected error for precision=19")
	}
	if _, err := impulse.NewDecimal64(1, 4, 5); err == nil {
		t.Errorf("expected error for scale > precision")
	}
	if _, err := impulse.NewDecimal64(1000, 3, 0); err == nil {
		t.Errorf("expected overflow error for 1000 in DECIMAL(3, 0)")
	}
	if _, err := impulse.ParseDecimal64("12.34.56", 10, 2); err == nil {
		t.Errorf("expected error for malformed literal")
	}
}

func TestDecimal64SnapshotRoundtripAndNullable(t *testing.T) {
	tmpDir, err := os.MkdirTemp("", "impulse_dec64_test_*")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	snapPath := filepath.Join(tmpDir, "decimal64.imps")
	writer, err := impulse.NewWriter(snapPath, 1)
	if err != nil {
		t.Fatalf("failed to create writer: %v", err)
	}

	if err := writer.AddDomain(0, impulse.KeyTypeString, "Account"); err != nil {
		t.Fatalf("AddDomain failed: %v", err)
	}
	if err := writer.SetDomainNodeCount(0, 3); err != nil {
		t.Fatalf("SetDomainNodeCount failed: %v", err)
	}

	b0, _ := impulse.ParseDecimal64("10.00", 18, 2)
	b1, _ := impulse.ParseDecimal64("19.99", 18, 2)
	b2, _ := impulse.ParseDecimal64("9007199254740993.01", 18, 2)
	if err := writer.AddDomainAttributeDecimal64(
		0, "balance", impulse.AttrTypeDecimal64, 1, 18, 2,
		[]int64{b0.Unscaled, b1.Unscaled, b2.Unscaled},
	); err != nil {
		t.Fatalf("AddDomainAttributeDecimal64 failed: %v", err)
	}

	l0, _ := impulse.ParseDecimal64("500.00", 12, 2)
	l2, _ := impulse.ParseDecimal64("1250.75", 12, 2)
	if err := writer.AddDomainAttributeDecimal64Nullable(
		0, "credit_limit", 1, 12, 2,
		[]impulse.NullableDecimal64{
			{Value: l0, Valid: true},
			{Valid: false},
			{Value: l2, Valid: true},
		},
	); err != nil {
		t.Fatalf("AddDomainAttributeDecimal64Nullable failed: %v", err)
	}

	rowOffsets := []uint32{0, 2, 2, 2}
	colIndices := []uint32{1, 2}
	if err := writer.AddRelation(0, 0, impulse.EncRaw, 3, 2, 0, rowOffsets, colIndices); err != nil {
		t.Fatalf("AddRelation failed: %v", err)
	}
	e0, _ := impulse.ParseDecimal64("4.2500", 18, 4)
	e1, _ := impulse.ParseDecimal64("100.1234", 18, 4)
	if err := writer.AddAttributeDecimal64(
		0, "amount", impulse.AttrTypeDecimal64, 1, 18, 4,
		[]int64{e0.Unscaled, e1.Unscaled},
	); err != nil {
		t.Fatalf("AddAttributeDecimal64 failed: %v", err)
	}

	if err := writer.Finalize(); err != nil {
		t.Fatalf("Finalize failed: %v", err)
	}

	snap, err := impulse.OpenSnapshot(snapPath)
	if err != nil {
		t.Fatalf("OpenSnapshot failed: %v", err)
	}
	defer snap.Close()

	if snap.GetDomainAttributeCount(0) != 2 {
		t.Fatalf("expected 2 domain attributes, got %d", snap.GetDomainAttributeCount(0))
	}

	balances, balMeta, err := snap.GetDomainAttributeDecimal64(0, 0)
	if err != nil {
		t.Fatalf("GetDomainAttributeDecimal64(balance) failed: %v", err)
	}
	if balMeta.Precision != 18 || balMeta.Scale != 2 || balMeta.IsNullable {
		t.Errorf("unexpected balance meta: %+v", balMeta)
	}
	if len(balances) != 3 || !balances[2].Valid || balances[2].Value.String() != "9007199254740993.01" {
		t.Errorf("unexpected balances: %+v", balances)
	}

	limits, limMeta, err := snap.GetDomainAttributeDecimal64(0, 1)
	if err != nil {
		t.Fatalf("GetDomainAttributeDecimal64(credit_limit) failed: %v", err)
	}
	if limMeta.Precision != 12 || limMeta.Scale != 2 || !limMeta.IsNullable {
		t.Errorf("unexpected credit_limit meta: %+v", limMeta)
	}
	if len(limits) != 3 || !limits[0].Valid || limits[1].Valid || !limits[2].Valid {
		t.Fatalf("unexpected nullable validity: %+v", limits)
	}
	if limits[0].Value.String() != "500.00" || limits[2].Value.String() != "1250.75" {
		t.Errorf("unexpected credit_limit values: %+v", limits)
	}

	amounts, amtMeta, err := snap.GetAttributeDecimal64(0, 0)
	if err != nil {
		t.Fatalf("GetAttributeDecimal64(amount) failed: %v", err)
	}
	if amtMeta.Precision != 18 || amtMeta.Scale != 4 || amtMeta.IsNullable {
		t.Errorf("unexpected amount meta: %+v", amtMeta)
	}
	if len(amounts) != 2 || amounts[0].Value.String() != "4.2500" || amounts[1].Value.String() != "100.1234" {
		t.Errorf("unexpected edge amounts: %+v", amounts)
	}
}
