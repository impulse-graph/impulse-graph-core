'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const impulse = require('./index.js');

console.log('=== Running Impulse Graph Engine Node.js v0.11 Native Binding Tests ===');

assert.strictEqual(typeof impulse.Snapshot, 'function', 'Snapshot class should be exported');
assert.strictEqual(typeof impulse.Writer, 'function', 'Writer class should be exported');
assert.strictEqual(typeof impulse.VmWorkspace, 'function', 'VmWorkspace class should be exported');
assert.strictEqual(typeof impulse.Opcodes, 'object', 'Opcodes table should be exported');
assert.strictEqual(typeof impulse.VmStatus, 'object', 'VmStatus enum should be exported');

const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'impulse-node-v11-'));
const snapPath = path.join(tmpDir, 'test.imps');
try {
  const writer = new impulse.Writer(snapPath, 1);
  writer.addDomain(0, 5, 'Node');
  writer.addRelation(0, 0, 0, 3, 2, 0, new Uint32Array([0, 2, 2, 2]), new Uint32Array([1, 2]));
  writer.finalize();

  const snap = new impulse.Snapshot(snapPath);
  assert.strictEqual(snap.domainCount(), 1);
  assert.strictEqual(snap.relationCount(), 1);
  assert.strictEqual(snap.isReachable(0, 0n, 1n), true);
  assert.strictEqual(snap.isReachable(0, 0n, 2n), true);
  assert.strictEqual(snap.isReachable(0, 1n, 2n), false);

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
  const rc = ws.execute(impb, snap, [0n], 0, 0n);
  assert.strictEqual(rc, impulse.VmStatus.OK, `expected OK (0), got ${rc}`);
  assert.strictEqual(ws.outputCount(), 1);
  ws.destroy();
  snap.close();
  console.log('ALL NODE v0.11 TESTS PASSED!');
} finally {
  fs.rmSync(tmpDir, { recursive: true, force: true });
}
