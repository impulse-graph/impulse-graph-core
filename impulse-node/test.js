'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const impulse = require('./index.js');

const { Decimal64 } = impulse;

console.log('=== Running Impulse Graph Engine Node.js v0.11 Native Binding Tests ===');

assert.strictEqual(typeof impulse.Snapshot, 'function', 'Snapshot class should be exported');
assert.strictEqual(typeof impulse.Writer, 'function', 'Writer class should be exported');
assert.strictEqual(typeof impulse.VmWorkspace, 'function', 'VmWorkspace class should be exported');
assert.strictEqual(typeof impulse.Decimal64, 'function', 'Decimal64 class should be exported');
assert.strictEqual(typeof impulse.compileImpScm, 'function', 'compileImpScm should be exported');
assert.strictEqual(typeof impulse.compileCypher, 'function', 'compileCypher should be exported');
assert.strictEqual(typeof impulse.Opcodes, 'object', 'Opcodes table should be exported');
assert.strictEqual(typeof impulse.VmStatus, 'object', 'VmStatus enum should be exported');
assert.strictEqual(typeof impulse.AttrType, 'object', 'AttrType enum should be exported');
assert.strictEqual(impulse.Opcodes.SCALAR_DECIMAL_ALU, 0x15);
assert.strictEqual(impulse.Opcodes.VEC_DECIMAL_ALU, 0x58);
assert.strictEqual(impulse.VmStatus.ARITHMETIC_OVERFLOW, 16);
assert.strictEqual(impulse.VmStatus.DIV_BY_ZERO, 17);
assert.strictEqual(impulse.AttrType.DECIMAL64, 0x15);
assert.strictEqual(impulse.AttrType.DECIMAL64_NULLABLE, 0x95);
assert.strictEqual(impulse.AttrType.BOOL, 0x0f);

// ---------------------------------------------------------------------------
// 1. Decimal64 value type unit tests (including > 2^53 BigInt precision)
// ---------------------------------------------------------------------------
{
  const d1 = Decimal64.fromString('123.45', 10, 2);
  assert.strictEqual(d1.unscaled, 12345n);
  assert.strictEqual(d1.precision, 10);
  assert.strictEqual(d1.scale, 2);
  assert.strictEqual(d1.toString(), '123.45');
  assert.strictEqual(d1.toNumber(), 123.45);
  assert.strictEqual(Object.isFrozen(d1), true);

  const d2 = Decimal64.fromUnscaled(1234500n, 10, 4);
  assert.strictEqual(d2.toString(), '123.4500');
  assert.strictEqual(d1.equals(d2), true, 'cross-scale equality should match');
  assert.strictEqual(d2.equals(d1), true);
  assert.strictEqual(d1.equals(Decimal64.fromString('123.46', 10, 2)), false);
  assert.strictEqual(d1.equals('123.45'), false);

  // Negative & sub-unit formatting
  const neg = Decimal64.fromString('-0.0050', 18, 4);
  assert.strictEqual(neg.unscaled, -50n);
  assert.strictEqual(neg.toString(), '-0.0050');

  const negZero = Decimal64.fromString('-0.00', 10, 2);
  assert.strictEqual(negZero.unscaled, 0n);
  assert.strictEqual(negZero.toString(), '0.00');

  // Half-away-from-zero rounding on string parsing
  assert.strictEqual(Decimal64.fromString('99.995', 10, 2).toString(), '100.00');
  assert.strictEqual(Decimal64.fromString('99.994', 10, 2).toString(), '99.99');
  assert.strictEqual(Decimal64.fromString('-1.235', 10, 2).toString(), '-1.24');
  assert.strictEqual(Decimal64.fromString('-1.234', 10, 2).toString(), '-1.23');

  // > 2^53 precision preservation via BigInt (9007199254740993 = 2^53 + 1)
  const bigStr = '9007199254740993.01';
  const bigDec = Decimal64.fromString(bigStr, 18, 2);
  assert.strictEqual(bigDec.unscaled, 900719925474099301n);
  assert.strictEqual(bigDec.toString(), bigStr);

  const maxDec = Decimal64.fromUnscaled(999999999999999999n, 18, 0);
  assert.strictEqual(maxDec.toString(), '999999999999999999');

  const minDec = Decimal64.fromUnscaled(-999999999999999999n, 18, 0);
  assert.strictEqual(minDec.toString(), '-999999999999999999');

  // Validation errors
  assert.throws(() => new Decimal64(1n, 0, 0), RangeError);
  assert.throws(() => new Decimal64(1n, 19, 0), RangeError);
  assert.throws(() => new Decimal64(1n, 4, 5), RangeError);
  assert.throws(() => new Decimal64(1000n, 3, 0), RangeError);
  assert.throws(() => new Decimal64(-1000n, 3, 0), RangeError);
  assert.throws(() => Decimal64.fromString('1000', 3, 0), RangeError);
  assert.throws(() => Decimal64.fromString('', 10, 2), SyntaxError);
  assert.throws(() => Decimal64.fromString('1.2.3', 10, 2), SyntaxError);
  assert.throws(() => Decimal64.fromString('12abc', 10, 2), SyntaxError);
  assert.throws(() => new Decimal64(Number.MAX_SAFE_INTEGER + 1, 18, 0), RangeError);
}

// ---------------------------------------------------------------------------
// 2. Snapshot roundtrip: non-null (0x15), nullable (0x95), > 2^53, VM & Cypher
// ---------------------------------------------------------------------------
const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'impulse-node-v11-'));
const snapPath = path.join(tmpDir, 'test.imps');
try {
  const writer = new impulse.Writer(snapPath, 1);
  writer.addDomain(0, 5, 'Account');
  writer.setDomainNodeCount(0, 3);

  // Non-null DECIMAL64 (0x15) with > 2^53 value
  writer.addDomainDecimal64(0, 'balance', 18, 2, [
    Decimal64.fromString('10.00', 18, 2),
    Decimal64.fromString('19.99', 18, 2),
    Decimal64.fromString('9007199254740993.01', 18, 2),
  ]);

  // Nullable DECIMAL64 (0x95) with null at index 1
  writer.addDomainDecimal64(
    0,
    'credit_limit',
    12,
    2,
    [Decimal64.fromString('500.00', 12, 2), null, Decimal64.fromString('1250.75', 12, 2)],
    { nullable: true }
  );

  // Relation: 0 -> 1, 0 -> 2
  writer.addRelation(0, 0, 0, 3, 2, 0, new Uint32Array([0, 2, 2, 2]), new Uint32Array([1, 2]));

  // Edge DECIMAL64 attribute (non-null) and nullable edge DECIMAL64 attribute
  writer.addRelationDecimal64(0, 'amount', 18, 4, [
    Decimal64.fromString('4.2500', 18, 4),
    Decimal64.fromString('100.1234', 18, 4),
  ]);
  writer.addRelationDecimal64(
    0,
    'fee',
    10,
    2,
    [null, Decimal64.fromString('1.50', 10, 2)],
    { nullable: true }
  );

  writer.finalize();

  const snap = new impulse.Snapshot(snapPath);
  assert.strictEqual(snap.domainCount(), 1);
  assert.strictEqual(snap.relationCount(), 1);
  assert.strictEqual(snap.getDomainAttributeCount(0), 2);
  assert.strictEqual(snap.isReachable(0, 0n, 1n), true);
  assert.strictEqual(snap.isReachable(0, 0n, 2n), true);
  assert.strictEqual(snap.isReachable(0, 1n, 2n), false);

  // Verify domain DECIMAL64 metadata & values
  const balInfo = snap.getDomainAttributeInfo(0, 0);
  assert.strictEqual(balInfo.name, 'balance');
  assert.strictEqual(balInfo.typeCode, 0x15);
  assert.strictEqual(balInfo.isNullable, false);
  assert.strictEqual(balInfo.precision, 18);
  assert.strictEqual(balInfo.scale, 2);

  const balances = snap.getDomainDecimal64(0, 0);
  assert.strictEqual(balances.length, 3);
  assert.strictEqual(balances[0].toString(), '10.00');
  assert.strictEqual(balances[1].toString(), '19.99');
  assert.strictEqual(balances[2].unscaled, 900719925474099301n);
  assert.strictEqual(balances[2].toString(), '9007199254740993.01');

  // Verify nullable domain DECIMAL64 (0x95)
  const limInfo = snap.getDomainAttributeInfo(0, 1);
  assert.strictEqual(limInfo.name, 'credit_limit');
  assert.strictEqual(limInfo.typeCode, 0x95);
  assert.strictEqual(limInfo.isNullable, true);
  assert.strictEqual(limInfo.precision, 12);
  assert.strictEqual(limInfo.scale, 2);

  const limits = snap.getDomainDecimal64(0, 1);
  assert.strictEqual(limits.length, 3);
  assert.strictEqual(limits[0].toString(), '500.00');
  assert.strictEqual(limits[1], null);
  assert.strictEqual(limits[2].toString(), '1250.75');

  // Verify edge DECIMAL64 metadata & values (non-null and nullable)
  const amtInfo = snap.getAttributeInfo(0, 0);
  assert.strictEqual(amtInfo.name, 'amount');
  assert.strictEqual(amtInfo.typeCode, 0x15);
  assert.strictEqual(amtInfo.precision, 18);
  assert.strictEqual(amtInfo.scale, 4);
  const amounts = snap.getRelationDecimal64(0, 0);
  assert.strictEqual(amounts.length, 2);
  assert.strictEqual(amounts[0].toString(), '4.2500');
  assert.strictEqual(amounts[1].toString(), '100.1234');

  const feeInfo = snap.getAttributeInfo(0, 1);
  assert.strictEqual(feeInfo.typeCode, 0x95);
  assert.strictEqual(feeInfo.isNullable, true);
  assert.strictEqual(feeInfo.precision, 10);
  assert.strictEqual(feeInfo.scale, 2);
  const fees = snap.getRelationDecimal64(0, 1);
  assert.strictEqual(fees.length, 2);
  assert.strictEqual(fees[0], null);
  assert.strictEqual(fees[1].toString(), '1.50');

  // Raw .impb v0.11 VM execution
  const enc = (op, sub, flg, dst, s1, s2, s3, imm) =>
    BigInt(op) |
    (BigInt(sub & 0xf) << 8n) |
    (BigInt(flg & 0xf) << 12n) |
    (BigInt(s1 & 0xf) << 16n) |
    (BigInt(dst & 0xf) << 20n) |
    (BigInt(s3 & 0xf) << 24n) |
    (BigInt(s2 & 0xf) << 28n) |
    (BigInt(imm >>> 0) << 32n);

  const words = [
    enc(0x07, 1, 0, 0, 0, 0, 0, 0),
    enc(0x30, 0, 0, 1, 0, 0, 0, 0),
    enc(0x57, 1, 0, 0, 1, 0, 0, 0),
    enc(0x00, 0, 0, 0, 0, 0, 0, 0),
  ];
  const impb = Buffer.alloc(32 + words.length * 8);
  impb.writeUInt32LE(0x494d5042, 0);
  impb.writeUInt16LE(11, 4);
  impb.writeUInt32LE(words.length, 8);
  impb.writeUInt16LE(1, 20);
  words.forEach((w, i) => impb.writeBigUInt64LE(w, 32 + i * 8));

  const ws = new impulse.VmWorkspace(1024 * 1024);
  const rc = ws.execute(impb, snap, [0n], 0);
  assert.strictEqual(rc, impulse.VmStatus.OK, `expected OK (0), got ${rc}`);
  assert.strictEqual(ws.outputCount(), 1);

  // Audit regressions: out-of-range BigInt (1n << 80n) and malformed executeParams bindings
  assert.throws(
    () => ws.executeParams(impb, snap, [{ paramId: 0, kind: 1, scalarI64: 1n << 80n }]),
    RangeError
  );
  assert.throws(
    () => ws.executeParams(impb, snap, [{ paramId: 0, kind: 2, scalarBits: 1n << 80n }]),
    RangeError
  );
  assert.throws(
    () => ws.executeParams(impb, snap, [{ paramId: 'bad', kind: 1 }]),
    TypeError
  );
  ws.destroy();

  // Audit regression: malformed DECIMAL(P, S) specs throw clean JS TypeError / RangeError
  assert.throws(
    () =>
      impulse.compileCypher('MATCH (a:Account) WHERE a.balance >= $x RETURN count(a)', snap, {
        x: 'DECIMAL(abc, 2)',
      }),
    TypeError
  );
  assert.throws(
    () =>
      impulse.compileCypher('MATCH (a:Account) WHERE a.balance >= $x RETURN count(a)', snap, {
        x: 'DECIMAL(18x, 2)',
      }),
    TypeError
  );
  assert.throws(
    () =>
      impulse.compileCypher('MATCH (a:Account) WHERE a.balance >= $x RETURN count(a)', snap, {
        x: 'DECIMAL(19, 2)',
      }),
    RangeError
  );
  assert.throws(
    () =>
      impulse.compileCypher('MATCH (a:Account) WHERE a.balance >= $x RETURN count(a)', snap, {
        x: 'DECIMAL(4, 5)',
      }),
    RangeError
  );

  // Audit regression: wider-precision Decimal64 rejected on narrower column & param
  const narrowPath = path.join(tmpDir, 'narrow.imps');
  const narrowWriter = new impulse.Writer(narrowPath, 1);
  narrowWriter.addDomain(0, 5, 'Narrow');
  narrowWriter.setDomainNodeCount(0, 1);
  assert.throws(
    () => narrowWriter.addDomainDecimal64(0, 'small', 4, 2, [Decimal64.fromString('123.45', 10, 2)]),
    RangeError
  );
  // JS number treated as scaled value (12.34 -> 1234n) consistently; NaN rejected
  assert.throws(
    () => narrowWriter.addDomainDecimal64(0, 'small', 4, 2, [Number.NaN]),
    RangeError
  );
  narrowWriter.addDomainDecimal64(0, 'small', 4, 2, [12.34]);
  narrowWriter.setMetadata('z_key', 'last');
  narrowWriter.setMetadata('a_key', 'first');
  narrowWriter.setMetadata('🚀_emoji', 'rocket');
  narrowWriter.setMetadata('\uff21_fullwidth', 'fw');
  narrowWriter.finalize();
  const narrowSnap = new impulse.Snapshot(narrowPath);
  const narrowVals = narrowSnap.getDomainDecimal64(0, 0);
  assert.strictEqual(narrowVals[0].toString(), '12.34');
  assert.strictEqual(narrowSnap.getMetadata('a_key'), 'first');
  assert.strictEqual(narrowSnap.getMetadata('z_key'), 'last');
  assert.strictEqual(narrowSnap.getMetadata('🚀_emoji'), 'rocket');
  assert.strictEqual(narrowSnap.getMetadata('\uff21_fullwidth'), 'fw');
  assert.strictEqual(narrowSnap.getMetadata('missing'), null);
  const narrowMeta = narrowSnap.getAllMetadata();
  assert.strictEqual(Object.keys(narrowMeta).length, 4);
  assert.strictEqual(narrowMeta['🚀_emoji'], 'rocket');
  narrowSnap.close();

  const narrowParamProg = impulse.compileCypher(
    'MATCH (a:Account) WHERE a.balance >= $x RETURN count(a)',
    snap,
    { x: 'DECIMAL(4, 2)' }
  );
  assert.throws(
    () => narrowParamProg.execute(snap, { x: Decimal64.fromString('123.45', 10, 2) }),
    RangeError
  );
  assert.throws(() => narrowParamProg.execute(snap, { x: Number.POSITIVE_INFINITY }), RangeError);

  // E2E openCypher DECIMAL64 with Decimal64 param and > 2^53 sum output
  const cypherProg = impulse.compileCypher(
    'MATCH (a:Account) WHERE a.balance >= $min_bal RETURN sum(a.balance)',
    snap,
    { min_bal: 'DECIMAL(18, 2)' }
  );
  assert.strictEqual(cypherProg.outputSchema[0].isDecimal, true);
  assert.strictEqual(cypherProg.outputSchema[0].scale, 2);
  const cypherRes = cypherProg.execute(snap, {
    min_bal: Decimal64.fromString('19.99', 18, 2),
  });
  assert.ok(cypherRes.result instanceof Decimal64);
  assert.strictEqual(cypherRes.result.unscaled, 900719925474101300n);
  assert.strictEqual(cypherRes.result.toString(), '9007199254741013.00');

  // E2E ImpScheme v0.11 DECIMAL64 query + NULL output from safe-div by zero
  const scmProg = impulse.compileImpScm(
    '(query (param $min_bal (decimal 18 2)) ' +
      '(let ((f (node-filter (domain-all "Account") "balance" >= $min_bal)) ' +
      '      (b (load-node-attr "Account" "balance"))) ' +
      '  (collect :scalar "total" (reduce :sum b :mask f))))',
    snap
  );
  const scmRes = scmProg.execute(snap, {
    min_bal: Decimal64.fromString('19.99', 18, 2),
  });
  assert.ok(scmRes.total instanceof Decimal64);
  assert.strictEqual(scmRes.total.toString(), '9007199254741013.00');

  const nullDivProg = impulse.compileImpScm(
    '(query (collect :scalar "q" (safe-div (dec "10.00" 10 2) (dec "0.00" 10 2))))',
    snap
  );
  const nullDivRes = nullDivProg.execute(snap);
  assert.strictEqual(nullDivRes.q, null);

  // Phase 1 metadata introspection assertions
  const crypto = require('crypto');
  const hdr = snap.header();
  assert.strictEqual(hdr.magic, 0x494d5053);
  assert.strictEqual(hdr.version, 11);
  assert.strictEqual(hdr.dataOffset, 4096);
  assert.strictEqual(hdr.domainCount, snap.domainCount());
  assert.strictEqual(hdr.relationCount, snap.relationCount());
  assert.strictEqual(hdr.fileSize, snap.fileSize());
  assert.strictEqual(hdr.snapshotUuid.length, 16);
  const expectedSha = crypto
    .createHash('sha256')
    .update(fs.readFileSync(snapPath))
    .digest('hex');
  assert.strictEqual(snap.sha256(), expectedSha);

  const dom0 = snap.getDomain(0);
  assert.strictEqual(dom0.domainId, 0);
  assert.ok(dom0.attrCount >= 1);
  assert.strictEqual(typeof dom0.nodeCount, 'bigint');

  const rel0 = snap.getRelation(0);
  assert.strictEqual(rel0.relationId, 0);
  assert.strictEqual(rel0.isVirtual, false);
  assert.deepStrictEqual(rel0.virtualComponentIds, []);
  assert.strictEqual(typeof rel0.csrRowOffOffset, 'bigint');
  assert.strictEqual(typeof rel0.cooSrcOffset, 'bigint');

  const da0 = snap.getDomainAttributeInfo(0, 0);
  assert.ok(da0.dataOffset >= 4096n);
  assert.strictEqual(typeof da0.dataBytes, 'bigint');
  assert.strictEqual(typeof da0.offsetsBytes, 'bigint');

  assert.strictEqual(snap.indexCount(), 0);
  assert.deepStrictEqual(snap.getAllMetadata(), {});
  assert.strictEqual(snap.getMetadata('missing'), null);

  // H2 regression: invalid uint16 indices must throw RangeError (never wrap to 0)
  for (const badIdx of [-1, -65536, 65536, NaN, Infinity, 0.5]) {
    assert.throws(() => snap.getDomain(badIdx), RangeError);
    assert.throws(() => snap.getRelation(badIdx), RangeError);
    assert.throws(() => snap.getDomainAttributeInfo(badIdx, 0), RangeError);
  }
  assert.throws(() => snap.getDomainAttributeInfo(0, 99), RangeError);
  assert.throws(() => snap.getAttributeInfo(0, 99), RangeError);

  snap.close();
  assert.throws(() => snap.header(), /closed/i);
  assert.throws(() => snap.fileSize(), /closed/i);
  assert.throws(() => snap.indexCount(), /closed/i);
  console.log('ALL NODE v0.11 + DECIMAL64 + INTROSPECTION TESTS PASSED!');
} finally {
  fs.rmSync(tmpDir, { recursive: true, force: true });
}
