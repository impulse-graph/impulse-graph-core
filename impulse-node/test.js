/**
 * @file test.js
 * @brief Comprehensive test suite for Impulse Graph Engine Node.js C++ VM Bindings.
 */

const fs = require('fs');
const path = require('path');
const assert = require('assert');
const impulse = require('./index.js');

const {
  Snapshot,
  Writer,
  VmContext,
  VmState,
  QueryBuilder,
  executeBytecode,
  Opcodes,
  RegisterType,
  VmStatus
} = impulse;

console.log('=== Running Impulse Graph Engine Node.js VM Native Binding Tests ===\n');

// 1. Verify Exports and Constants
console.log('[Test 1] Verifying export definitions & constants...');
assert.strictEqual(typeof Snapshot, 'function', 'Snapshot class should be exported');
assert.strictEqual(typeof Writer, 'function', 'Writer class should be exported');
assert.strictEqual(typeof VmContext, 'function', 'VmContext class should be exported');
assert.strictEqual(typeof VmState, 'function', 'VmState class should be exported');
assert.strictEqual(typeof QueryBuilder, 'function', 'QueryBuilder class should be exported');
assert.strictEqual(typeof executeBytecode, 'function', 'executeBytecode function should be exported');

assert.strictEqual(Opcodes.OP_NOP, 0, 'OP_NOP should be 0');
assert.strictEqual(Opcodes.OP_INIT_INPUT_NODE, 1, 'OP_INIT_INPUT_NODE should be 1');
assert.strictEqual(Opcodes.OP_CSR_WALK, 0x10, 'OP_CSR_WALK should be 0x10');
assert.strictEqual(Opcodes.OP_COLLECT_BITSET, 0x90, 'OP_COLLECT_BITSET should be 0x90');
assert.strictEqual(RegisterType.TYPE_BITSET_HANDLE, 4, 'TYPE_BITSET_HANDLE should be 4');
assert.strictEqual(VmStatus.IMPULSE_VM_OK, 0, 'IMPULSE_VM_OK should be 0');
console.log('  -> Constants verified successfully!\n');

// 2. Off-heap VmContext Pool Tests
console.log('[Test 2] Testing off-heap VmContext memory pool...');
const ctx = new VmContext();
assert.strictEqual(ctx.vectorSize(), 0, 'Default vector size should be 0');

const hBitset = ctx.acquireBitset();
assert(hBitset >= 0, 'Acquired bitset handle should be non-negative');
assert.strictEqual(ctx.bitsetTest(hBitset, 42), false, 'Node 42 should not be set initially');

ctx.bitsetAdd(hBitset, 42);
assert.strictEqual(ctx.bitsetTest(hBitset, 42), true, 'Node 42 should be set after bitsetAdd');
assert.strictEqual(ctx.bitsetTest(hBitset, 100), false, 'Node 100 should not be set');

ctx.releaseBitset(hBitset);

const hStr = ctx.acquireStringVector();
ctx.stringVectorAdd(hStr, 'impulse');
ctx.stringVectorAdd(hStr, 'graph');
assert.strictEqual(ctx.stringVectorSize(hStr), 2, 'String vector size should be 2');
assert.strictEqual(ctx.stringVectorGet(hStr, 0), 'impulse', 'String vector at index 0 should match');
assert.strictEqual(ctx.stringVectorGet(hStr, 1), 'graph', 'String vector at index 1 should match');
ctx.releaseStringVector(hStr);

ctx.destroy();
console.log('  -> VmContext memory pool tests passed!\n');

// 3. VmState Register Frame Tests
console.log('[Test 3] Testing VmState register frame...');
const state = new VmState();
assert.strictEqual(state.getPc(), 0, 'Initial PC should be 0');
assert.strictEqual(state.getRegister(0), 0n, 'Initial R0 should be 0');

state.setRegister(0, 12345n);
assert.strictEqual(state.getRegister(0), 12345n, 'R0 should be updated to 12345');

state.setRegister(1, 99n);
assert.strictEqual(state.getRegister(1), 99n, 'R1 should be updated to 99');
console.log('  -> VmState register frame tests passed!\n');

// 4. Create Binary Snapshot & Execute Fluent VM Query
console.log('[Test 4] Creating snapshot & executing Fluent VM Query...');
const testSnapPath = path.join(__dirname, 'test_node_vm.imps');

// Build a simple snapshot with 1 domain and 1 relation (0 -> 1, 0 -> 2, 1 -> 3)
const writer = new Writer(testSnapPath);
writer.addDomain(0, 1, 'Node');

const rowOffsets = new Uint32Array([0, 2, 3, 3, 3]); // 4 nodes: node 0: [1,2], node 1: [3]
const colIndices = new Uint32Array([1, 2, 3]);

writer.addRelation(
  0, // srcDomain
  0, // tgtDomain
  0, // encodingType (CSR)
  4, // nodeCount
  3, // edgeCount
  0, // sectionFeatures
  rowOffsets,
  colIndices
);
writer.finalize();

const snap = new Snapshot(testSnapPath);
assert.strictEqual(snap.isReachable(0, 0, 0, 1), true, '0 -> 1 should be reachable');
assert.strictEqual(snap.isReachable(0, 0, 0, 3), false, '0 -> 3 direct reachability should be false');

// Build a query: inputNode(0) -> walkEdge(0) -> collectBitset()
const qb = new QueryBuilder();
qb.inputNode(0)
  .walkEdge(0)
  .collectBitset();

const compiled = qb.compile();
assert.strictEqual(compiled.instructionCount(), 4, 'Compiled query instruction count should be 4 (including OP_HALT)');

const testCtx = new VmContext(snap);
const vmState = new VmState();

const queryRes = compiled.executeWithContext(testCtx, vmState, 0n); // Seed node 0
assert.strictEqual(queryRes.isOk(), true, 'VM Query execution should be OK');
assert.strictEqual(queryRes.status, VmStatus.IMPULSE_VM_OK, 'Status should be OK');
assert.strictEqual(queryRes.resultType, RegisterType.TYPE_BITSET_HANDLE, 'Result type should be BITSET_HANDLE');

assert.strictEqual(queryRes.testBitset(testCtx, 1), true, 'Target 1 should be in bitset output');
assert.strictEqual(queryRes.testBitset(testCtx, 2), true, 'Target 2 should be in bitset output');
assert.strictEqual(queryRes.testBitset(testCtx, 3), false, 'Target 3 should not be in bitset output for 1-hop walk');

testCtx.destroy();
console.log('  -> Fluent VM Query execution passed!\n');

// 5. Test Scalar & Constant Instructions
console.log('[Test 5] Testing loadConstInt & scalar VM instructions...');
const qb2 = new QueryBuilder();
qb2.loadConstInt(42n)
   .mov(1, 0);

const compiled2 = qb2.compile();
const queryRes2 = compiled2.execute(snap, 0n);
assert.strictEqual(queryRes2.isOk(), true, 'Scalar query should succeed');
assert.strictEqual(queryRes2.asInt(), 42n, 'Resulting integer should be 42');

console.log('  -> Scalar VM instructions passed!\n');

// 6. Direct Low-Level Bytecode Execution
console.log('[Test 6] Testing direct low-level executeBytecode...');
const bcBuf = compiled2.bytecode();
assert.strictEqual(Buffer.isBuffer(bcBuf), true, 'Bytecode should be a Buffer');

const queryRes3 = executeBytecode(snap, bcBuf, 0n);
assert.strictEqual(queryRes3.isOk(), true, 'executeBytecode should succeed');
assert.strictEqual(queryRes3.asInt(), 42n, 'executeBytecode result should match 42');

console.log('  -> Low-level executeBytecode passed!\n');

// Cleanup temporary snapshot
snap.close();
if (fs.existsSync(testSnapPath)) {
  fs.unlinkSync(testSnapPath);
}

console.log('=== All Impulse Graph Engine Node.js VM Tests Passed Successfully! ===');
