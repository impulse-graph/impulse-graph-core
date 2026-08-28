const assert = require('assert');
const impulse = require('./index.js');
const { Snapshot, executeBytecodeAsync, QueryBuilder, VmContext, VmState } = impulse;

async function run() {
    console.log('=== Node.js Vector & Async Tests (Hetionet) ===');
    
    // 1. Load Hetionet
    const hetionetPath = '/Users/jesse/impulse/datasets/hetionet/hetionet.v09.imps';
    const snap = new Snapshot(hetionetPath);
    const ctx = new VmContext(snap);
    
    // 2. Query returning an Array (Node Vector)
    const qbArray = new QueryBuilder();
    qbArray.inputNode(0).walkEdge(0).collectArray();
    const compiledArray = qbArray.compile();

    const vmState = new VmState();
    console.log('Executing Node Vector Query Async...');
    const res2 = await compiledArray.executeWithContextAsync(ctx, vmState, 0n);
    assert.strictEqual(res2.isOk(), true);
    
    if (res2.resultType === impulse.RegisterType.TYPE_NODE_VECTOR) {
        const handle = Number(res2.rawValue);
        const nodeArray = ctx.getNodeVector(handle);
        console.log(` -> Retrieved Node Vector of length: ${nodeArray.length}`);
        assert.strictEqual(nodeArray.length > 0, true);
        console.log(` -> First element in array: ${nodeArray[0]}`);
    } else {
        throw new Error("Result was not an array! Result Type: " + res2.resultType);
    }
    
    // Clean up
    ctx.destroy();
    snap.close();
    console.log('=== All tests passed! ===');
}

run().catch(console.error);
