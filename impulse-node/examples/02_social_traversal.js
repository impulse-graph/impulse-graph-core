/**
 * Impulse Graph Engine — Example 02: Multi-Hop Social Graph Traversal (Node.js)
 *
 * Demonstrates:
 * 1. Loading social_graph.imps via embedded engine path resolution (IMPULSE_DATASETS_DIR).
 * 2. Multi-hop traversal using Fluent Traversal API (snap.traverse).
 * 3. Fast bytecode execution on ImpulseVM.
 */

const fs = require('fs');
const { Writer, Snapshot } = require('../index.js');

function main() {
  console.log('===============================================================');
  console.log(' Impulse Graph Engine — Example 02: Social Graph Traversal (Node.js)');
  console.log('===============================================================\n');

  const snapshotPath = 'social_graph.imps';
  let snap;
  let isTemp = false;
  const tempPath = 'temp_social_graph.imps';

  try {
    snap = new Snapshot(snapshotPath);
    console.log(`[INFO] Successfully resolved and opened '${snapshotPath}'.`);
  } catch (err) {
    console.log(`[INFO] '${snapshotPath}' not found in $IMPULSE_DATASETS_DIR or local paths.`);
    console.log('[INFO] Generating fallback in-memory sample social graph...');
    isTemp = true;

    const writer = new Writer(tempPath);
    writer.addDomain(0, 4, 'User');

    // 8 Users with follow relations
    const rowOffsets = new Uint32Array([0, 2, 4, 6, 8, 9, 10, 11, 11]);
    const colIndices = new Uint32Array([1, 2,  2, 3,  3, 4,  4, 5,  6, 7, 0]);

    writer.addRelation(0, 0, 0, 8, 11, 0, rowOffsets, colIndices);
    writer.finalize();

    snap = new Snapshot(tempPath);
  }

  // --------------------------------------------------------------------------
  // Step 1: Execute Fluent 2-Hop Traversal Pipeline
  // --------------------------------------------------------------------------
  console.log('\n1. Constructing Fluent Traversal Pipeline:');
  console.log('   Query: Seed(User 0) -> out(0) -> out(0)');

  const t0 = process.hrtime.bigint();
  const hop2Friends = snap.traverse(0)
    .out(0)
    .out(0)
    .toArray();
  const elapsedNs = process.hrtime.bigint() - t0;

  console.log(`   -> Traversal Latency: ${Number(elapsedNs) / 1000.0} µs`);
  console.log(`   -> 2-Hop Friends-of-Friends from User 0: [${hop2Friends.join(', ')}]`);

  // --------------------------------------------------------------------------
  // Step 2: Immediate Neighbors Inspection
  // --------------------------------------------------------------------------
  console.log('\n2. Inspecting 1-Hop Outgoing & Incoming Edges for User 0:');
  const directFriends = snap.traverse(0).out(0).toArray();
  console.log(`   -> User 0 follows: [${directFriends.join(', ')}]`);

  const followers = snap.traverse(0).in(0).toArray();
  console.log(`   -> Users who follow User 0: [${followers.join(', ')}]`);

  // Cleanup
  snap.close();
  if (isTemp && fs.existsSync(tempPath)) {
    fs.unlinkSync(tempPath);
  }

  console.log('\n[SUCCESS] Example 02 completed cleanly.');
}

main();
