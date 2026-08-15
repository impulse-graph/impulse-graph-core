/**
 * Impulse Graph Engine — Example 03: Relationship-Based Access Control (ReBAC) (Node.js)
 *
 * Demonstrates:
 * 1. Loading rbac_snapshot.imps via embedded engine path resolution (IMPULSE_DATASETS_DIR).
 * 2. Multi-domain authorization query:
 *    User -> assigned_role -> Role -> role_perm -> Permission
 * 3. Evaluating effective permissions for a user via typed graph traversal.
 */

const fs = require('fs');
const { Writer, Snapshot } = require('../index.js');

function main() {
  console.log('===============================================================');
  console.log(' Impulse Graph Engine — Example 03: ReBAC Authorization (Node.js)');
  console.log('===============================================================\n');

  const snapshotPath = 'rbac_snapshot.imps';
  let snap;
  let isTemp = false;
  const tempPath = 'temp_rbac_snapshot.imps';

  try {
    snap = new Snapshot(snapshotPath);
    console.log(`[INFO] Successfully resolved and opened '${snapshotPath}'.`);
  } catch (err) {
    console.log(`[INFO] '${snapshotPath}' not found in $IMPULSE_DATASETS_DIR or local paths.`);
    console.log('[INFO] Generating fallback ReBAC snapshot...');
    isTemp = true;

    const writer = new Writer(tempPath);
    writer.addDomain(0, 1, 'User');
    writer.addDomain(1, 1, 'Role');
    writer.addDomain(2, 1, 'Permission');

    // Relation 0: User -> Role (User 0 is Admin(0) and Editor(1))
    const uROwOff = new Uint32Array([0, 2, 3, 4]);
    const uRColIdx = new Uint32Array([0, 1, 1, 2]);
    writer.addRelation(0, 1, 0, 3, 4, 0, uROwOff, uRColIdx);

    // Relation 1: Role -> Permission (Admin(0) -> [Read(0), Write(1), Delete(2)])
    const rPOwOff = new Uint32Array([0, 3, 4, 5]);
    const rPColIdx = new Uint32Array([0, 1, 2, 0, 0]);
    writer.addRelation(1, 2, 0, 3, 5, 0, rPOwOff, rPColIdx);

    writer.finalize();
    snap = new Snapshot(tempPath);
  }

  // --------------------------------------------------------------------------
  // Step 1: ReBAC Multi-Hop Permission Traversal
  // --------------------------------------------------------------------------
  console.log('\n1. ReBAC Policy: Check permissions for User 0 (User -> Role -> Permission):');

  const t0 = process.hrtime.bigint();
  const effectivePermissions = snap.traverse(0)
    .out(0) // Walk User -> Role
    .out(1) // Walk Role -> Permission
    .toArray();
  const elapsedNs = process.hrtime.bigint() - t0;

  console.log(`   -> ReBAC Evaluation Latency: ${Number(elapsedNs)} ns`);
  console.log(`   -> Reached Permission IDs: [${effectivePermissions.join(', ')}]`);

  // --------------------------------------------------------------------------
  // Step 2: Policy Decision Evaluation
  // --------------------------------------------------------------------------
  console.log('\n2. Effective Permissions Evaluation for User 0:');
  const permLabels = [
    { name: 'READ', id: 0 },
    { name: 'WRITE', id: 1 },
    { name: 'DELETE', id: 2 }
  ];

  for (const perm of permLabels) {
    const allowed = effectivePermissions.includes(perm.id);
    const statusStr = allowed ? 'ALLOWED [✓]' : 'DENIED  [✗]';
    console.log(`   -> Permission ${perm.name} (${perm.id}): ${statusStr}`);
  }

  // Cleanup
  snap.close();
  if (isTemp && fs.existsSync(tempPath)) {
    fs.unlinkSync(tempPath);
  }

  console.log('\n[SUCCESS] Example 03 completed cleanly.');
}

main();
