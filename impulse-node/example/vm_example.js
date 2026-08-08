/**
 * @file vm_example.js
 * @brief Impulse Graph Engine Node.js / Bun VM Pipeline Usage Example.
 */

const fs = require('fs');
const path = require('path');
const {
  Snapshot,
  Writer,
  VmContext,
  VmState,
  QueryBuilder,
  executeBytecode,
  RegisterType,
  VmStatus
} = require('../index.js');

function runExample() {
  console.log('=== Impulse Graph Engine Node.js VM Pipeline Example ===\n');

  const snapshotPath = path.join(__dirname, 'social_graph.imps');

  // Step 1: Create a Binary Snapshot (.imps) using Writer
  console.log('1. Building binary snapshot file (social_graph.imps)...');
  const writer = new Writer(snapshotPath);
  writer.addDomain(0, 1, 'User'); // Domain 0: User

  // Define a CSR relation graph:
  // Node 0 -> [1, 2]  (User 0 follows User 1 and User 2)
  // Node 1 -> [3]     (User 1 follows User 3)
  // Node 2 -> [3, 4]  (User 2 follows User 3 and User 4)
  const rowOffsets = new Uint32Array([0, 2, 3, 5, 5, 5]); // 5 nodes
  const colIndices = new Uint32Array([1, 2, 3, 3, 4]);

  writer.addRelation(
    0, // srcDomain (User)
    0, // tgtDomain (User)
    0, // encodingType (CSR)
    5, // nodeCount
    5, // edgeCount
    0, // sectionFeatures
    rowOffsets,
    colIndices
  );
  writer.finalize();
  console.log('   -> Snapshot file created successfully.\n');

  // Step 2: Open Snapshot via zero-copy mmap
  console.log('2. Opening binary snapshot via zero-copy mmap...');
  const snapshot = new Snapshot(snapshotPath);
  console.log(`   -> 0 -> 1 reachable? ${snapshot.isReachable(0, 0, 0, 1)}`);
  console.log(`   -> 0 -> 3 direct reachable? ${snapshot.isReachable(0, 0, 0, 3)}\n`);

  // Step 3: Build a Fluent VM Query
  // Query Goal: 2-hop traversal starting from User 0
  // Step 1: inputNode(0) -> walkEdge(relation 0) -> R0 (1-hop neighbors: 1, 2)
  // Step 2: walkEdge(relation 0) -> R1 (2-hop neighbors: 3, 4)
  // Step 3: collectBitset() -> R2
  console.log('3. Constructing fluent VM bytecode query pipeline...');
  const qb = new QueryBuilder();
  qb.inputNode(0)
    .walkEdge(0)  // 1st hop -> [1, 2]
    .walkEdge(0)  // 2nd hop -> [3, 4]
    .collectBitset();

  const compiled = qb.compile();
  console.log(`   -> Compiled query into ${compiled.instructionCount()} bytecode instructions.`);
  console.log(`   -> Result register target: R${compiled.resultRegister()}\n`);

  // Step 4: Execute Query with off-heap VmContext & VmState
  console.log('4. Executing compiled VM query off-heap...');
  const ctx = new VmContext(snapshot);
  const state = new VmState();

  const result = compiled.executeWithContext(ctx, state, 0n); // Start seed = Node 0

  if (result.isOk()) {
    console.log('   -> Execution Status: IMPULSE_VM_OK');
    console.log(`   -> Result Type: ${result.resultType} (BITSET_HANDLE)`);

    console.log('\n5. Evaluating 2-hop reachable candidates from User 0:');
    for (let nodeId = 0; nodeId < 5; nodeId++) {
      const isReachable = result.testBitset(ctx, nodeId);
      console.log(`      Node ${nodeId}: ${isReachable ? 'REACHABLE ✓' : 'NOT REACHABLE ✗'}`);
    }
  } else {
    console.error(`   -> Execution Failed with Status Code: ${result.status}`);
  }

  // Cleanup
  ctx.destroy();
  snapshot.close();

  if (fs.existsSync(snapshotPath)) {
    fs.unlinkSync(snapshotPath);
  }
  console.log('\n=== Example Completed Successfully ===');
}

runExample();
