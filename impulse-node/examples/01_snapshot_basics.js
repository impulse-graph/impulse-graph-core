/**
 * Impulse Graph Engine — Example 01: Snapshot Basics (Node.js)
 *
 * Demonstrates:
 * 1. Programmatic creation of an immutable binary snapshot (.imps) using Writer.
 * 2. Opening the snapshot via zero-copy memory mapping with Snapshot.
 * 3. Inspecting domains, relations, degree arrays, and point reachability.
 */

const fs = require('fs');
const { Writer, Snapshot } = require('../index.js');

function main() {
  console.log('===============================================================');
  console.log(' Impulse Graph Engine — Example 01: Snapshot Basics (Node.js)');
  console.log('===============================================================\n');

  const snapshotPath = 'sample_basics.imps';

  // --------------------------------------------------------------------------
  // Step 1: Programmatic Snapshot Creation with Writer
  // --------------------------------------------------------------------------
  console.log(`1. Creating binary snapshot file: ${snapshotPath}...`);

  const writer = new Writer(snapshotPath);
  writer.addDomain(0, 4, 'User'); // Key Type: INT64

  // CSR Topology: 4 Users (0, 1, 2, 3)
  // Node 0 -> [1, 2]
  // Node 1 -> [2, 3]
  // Node 2 -> [3]
  // Node 3 -> []
  const rowOffsets = new Uint32Array([0, 2, 4, 5, 5]);
  const colIndices = new Uint32Array([1, 2, 2, 3, 3]);

  writer.addRelation(
    0, // srcDomain
    0, // tgtDomain
    0, // encoding (CSR)
    4, // nodeCount
    5, // edgeCount
    0, // sectionFeatures
    rowOffsets,
    colIndices
  );

  writer.finalize();
  const fileSize = fs.statSync(snapshotPath).size;
  console.log(`   -> Successfully wrote snapshot (${fileSize} bytes).\n`);

  // --------------------------------------------------------------------------
  // Step 2: Zero-Copy Memory-Mapped Reading with Snapshot
  // --------------------------------------------------------------------------
  console.log('2. Opening snapshot via zero-copy OS memory mapping...');
  const snap = new Snapshot(snapshotPath);

  // --------------------------------------------------------------------------
  // Step 3: Point Reachability Queries
  // --------------------------------------------------------------------------
  console.log('\n3. Direct Point Reachability Queries:');
  console.log(`   -> Node 0 -> Node 1 reachable? ${snap.isReachable(0, 0, 0, 1) ? 'YES' : 'NO'}`);
  console.log(
    `   -> Node 0 -> Node 3 reachable? ${snap.isReachable(0, 0, 0, 3) ? 'YES (direct)' : 'NO (multi-hop path)'}`
  );

  // Cleanup
  snap.close();
  if (fs.existsSync(snapshotPath)) {
    fs.unlinkSync(snapshotPath);
  }

  console.log('\n[SUCCESS] Example 01 completed cleanly.');
}

main();
