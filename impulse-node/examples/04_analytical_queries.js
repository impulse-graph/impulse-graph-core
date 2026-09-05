/**
 * Impulse Graph Engine — Example 04: Analytical Queries & VM Bytecode (Node.js)
 *
 * Demonstrates:
 * 1. Building low-level impOps bytecode using QueryBuilder.
 * 2. Vectorized operations and bitset collection.
 * 3. Inspecting execution performance against ImpulseVM.
 */

const fs = require('fs');
const { Writer, Snapshot, QueryBuilder, VmContext, VmState, VmStatus } = require('../index.js');

function main() {
  console.log('===================================================================');
  console.log(' Impulse Graph Engine — Example 04: Analytical VM Queries (Node.js)');
  console.log('===================================================================\n');

  const snapshotPath = 'financial_transactions.imps';
  let snap;
  let isTemp = false;
  const tempPath = 'temp_transactions.imps';

  try {
    snap = new Snapshot(snapshotPath);
    console.log(`[INFO] Successfully resolved and opened '${snapshotPath}'.`);
  } catch (_err) {
    console.log(`[INFO] '${snapshotPath}' not found in $IMPULSE_DATASETS_DIR or local paths.`);
    console.log('[INFO] Generating sample transaction snapshot...');
    isTemp = true;

    const writer = new Writer(tempPath);
    writer.addDomain(0, 1, 'Account');

    // 5 Accounts, 6 Transfer Edges
    const rowOffsets = new Uint32Array([0, 3, 4, 6, 6, 6]);
    const colIndices = new Uint32Array([1, 2, 3, 3, 0, 4]);

    writer.addRelation(0, 0, 0, 5, 6, 0, rowOffsets, colIndices);
    writer.finalize();

    snap = new Snapshot(tempPath);
  }

  // --------------------------------------------------------------------------
  // Step 1: Programmatic VM Bytecode Generation
  // --------------------------------------------------------------------------
  console.log('\n1. Compiling Low-Level impOps Bytecode with QueryBuilder:');

  const qb = new QueryBuilder();
  qb.inputNode(0) // R0: Seed Account 0
    .walkEdge(0) // R1: 1-hop transfers
    .collectBitset(); // R2: Output bitset

  const compiled = qb.compile();
  console.log(`   -> Generated ${compiled.instructionCount()} VM bytecode instructions.`);

  // --------------------------------------------------------------------------
  // Step 2: Off-Heap VmContext & Execution
  // --------------------------------------------------------------------------
  console.log('\n2. Executing against ImpulseVM:');
  const ctx = new VmContext(snap);
  const state = new VmState();

  const t0 = process.hrtime.bigint();
  const result = compiled.executeWithContext(ctx, state, 0n);
  const elapsedNs = process.hrtime.bigint() - t0;

  console.log(
    `   -> VM Execution Status: ${result.status === VmStatus.IMPULSE_VM_OK ? 'OK' : 'ERROR'}`
  );
  console.log(`   -> VM Execution Time:   ${Number(elapsedNs)} ns`);
  console.log(`   -> Result Register Type: BITSET_HANDLE (${result.resultType})`);

  // --------------------------------------------------------------------------
  // Step 3: Inspect Bitset Outputs
  // --------------------------------------------------------------------------
  console.log('\n3. Output Bitset Membership (Accounts transferred to from Account 0):');
  for (let acc = 0; acc < 5; acc++) {
    const reached = result.testBitset(ctx, acc);
    console.log(`   -> Account ${acc}: ${reached ? 'TRANSFER RECIPIENT [✓]' : 'NO'}`);
  }

  // Cleanup
  ctx.destroy();
  snap.close();
  if (isTemp && fs.existsSync(tempPath)) {
    fs.unlinkSync(tempPath);
  }

  console.log('\n[SUCCESS] Example 04 completed cleanly.');
}

main();
