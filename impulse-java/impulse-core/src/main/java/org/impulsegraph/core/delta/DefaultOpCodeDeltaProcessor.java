package org.impulsegraph.core.delta;

import org.impulsegraph.core.csr.BinarySnapshotLoader;
import org.impulsegraph.core.csr.CsrSwapManager;
import org.impulsegraph.core.csr.FullCsrGraph;
import org.impulsegraph.domain.loader.TsvRefGraphEngine;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.UUID;

/**
 * Production OpCode Delta Processor in impulse-core.
 * Processes live streaming deltas into the in-memory graph, triggers atomic A/B swaps,
 * and exports canonical snapshots byte-for-byte identical to golang-cli.
 */
public class DefaultOpCodeDeltaProcessor implements OpCodeDeltaConsumer {

    private final TsvRefGraphEngine engine;
    private final CsrSwapManager<FullCsrGraph> swapManager;
    private final Arena arena;

    public DefaultOpCodeDeltaProcessor(Arena arena) {
        this.arena = arena;
        this.engine = new TsvRefGraphEngine();
        this.swapManager = new CsrSwapManager<>(null);
    }

    public TsvRefGraphEngine getEngine() {
        return engine;
    }

    public CsrSwapManager<FullCsrGraph> getSwapManager() {
        return swapManager;
    }

    @Override
    public void onInsertNodeInt32(int domainId, int key) {
        engine.insertNode(String.valueOf(domainId), String.valueOf(key));
    }

    @Override
    public void onInsertNodeInt64(int domainId, long key) {
        engine.insertNode(String.valueOf(domainId), String.valueOf(key));
    }

    @Override
    public void onInsertNodeUuid(int domainId, UUID uuid) {
        engine.insertNode(String.valueOf(domainId), uuid.toString());
    }

    @Override
    public void onInsertNodeString(int domainId, String key) {
        engine.insertNode(String.valueOf(domainId), key);
    }

    @Override
    public void onInsertNodeBytes(int domainId, MemorySegment keyBytes) {
        engine.insertNode(String.valueOf(domainId), new String(keyBytes.toArray(java.lang.foreign.ValueLayout.JAVA_BYTE)));
    }

    @Override
    public void onDeleteNodeInt32(int domainId, int key) {
        engine.deleteNode(String.valueOf(domainId), String.valueOf(key));
    }

    @Override
    public void onDeleteNodeInt64(int domainId, long key) {
        engine.deleteNode(String.valueOf(domainId), String.valueOf(key));
    }

    @Override
    public void onDeleteNodeUuid(int domainId, UUID uuid) {
        engine.deleteNode(String.valueOf(domainId), uuid.toString());
    }

    @Override
    public void onDeleteNodeString(int domainId, String key) {
        engine.deleteNode(String.valueOf(domainId), key);
    }

    @Override
    public void onDeleteNodeBytes(int domainId, MemorySegment keyBytes) {
        engine.deleteNode(String.valueOf(domainId), new String(keyBytes.toArray(java.lang.foreign.ValueLayout.JAVA_BYTE)));
    }

    @Override
    public void onInsertEdgeInt32(int relationId, int srcKey, int tgtKey) {
        engine.insertEdge(String.valueOf(relationId), String.valueOf(srcKey), String.valueOf(tgtKey));
    }

    @Override
    public void onInsertEdgeInt64(int relationId, long srcKey, long tgtKey) {
        engine.insertEdge(String.valueOf(relationId), String.valueOf(srcKey), String.valueOf(tgtKey));
    }

    @Override
    public void onInsertEdgeUuid(int relationId, UUID srcUuid, UUID tgtUuid) {
        engine.insertEdge(String.valueOf(relationId), srcUuid.toString(), tgtUuid.toString());
    }

    @Override
    public void onInsertEdgeString(int relationId, String srcKey, String tgtKey) {
        engine.insertEdge(String.valueOf(relationId), srcKey, tgtKey);
    }

    @Override
    public void onInsertEdgeBytes(int relationId, MemorySegment srcBytes, MemorySegment tgtBytes) {
        String src = new String(srcBytes.toArray(java.lang.foreign.ValueLayout.JAVA_BYTE));
        String tgt = new String(tgtBytes.toArray(java.lang.foreign.ValueLayout.JAVA_BYTE));
        engine.insertEdge(String.valueOf(relationId), src, tgt);
    }

    @Override
    public void onDeleteEdgeInt32(int relationId, int srcKey, int tgtKey) {
        engine.deleteEdge(String.valueOf(relationId), String.valueOf(srcKey), String.valueOf(tgtKey));
    }

    @Override
    public void onDeleteEdgeInt64(int relationId, long srcKey, long tgtKey) {
        engine.deleteEdge(String.valueOf(relationId), String.valueOf(srcKey), String.valueOf(tgtKey));
    }

    @Override
    public void onDeleteEdgeUuid(int relationId, UUID srcUuid, UUID tgtUuid) {
        engine.deleteEdge(String.valueOf(relationId), srcUuid.toString(), tgtUuid.toString());
    }

    @Override
    public void onDeleteEdgeString(int relationId, String srcKey, String tgtKey) {
        engine.deleteEdge(String.valueOf(relationId), srcKey, tgtKey);
    }

    @Override
    public void onCheckpoint(long kafkaOffset) {
        engine.checkpoint(kafkaOffset);
    }

    /**
     * Compacts live delta layer, generates canonical C-ABI binary bytes,
     * triggers atomic A/B pointer swap in impulse-core, and returns binary snapshot.
     */
    public byte[] triggerCompactionAndSwap() {
        byte[] snapshotBytes = engine.buildSnapshotBytes();
        BinarySnapshotLoader.LoadedSnapshot loaded = BinarySnapshotLoader.loadSnapshot(snapshotBytes, arena);
        swapManager.swap(loaded.graph());
        return snapshotBytes;
    }
}
