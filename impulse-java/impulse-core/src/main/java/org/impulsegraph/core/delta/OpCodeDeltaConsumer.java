package org.impulsegraph.core.delta;

import java.lang.foreign.MemorySegment;
import java.util.UUID;

/**
 * Polymorphic OpCode Delta Consumer interface in impulse-core.
 * Processes incoming live stream deltas (Kafka CDC / Binary WAL) using strongly-typed BusinessKeys
 * without String conversion overhead.
 */
public interface OpCodeDeltaConsumer {

    // --- Typed Node Insertions ---
    void onInsertNodeInt32(int domainId, int key);
    void onInsertNodeInt64(int domainId, long key);
    void onInsertNodeUuid(int domainId, UUID uuid);
    void onInsertNodeString(int domainId, String key);
    void onInsertNodeBytes(int domainId, MemorySegment keyBytes);

    // --- Typed Node Deletions ---
    void onDeleteNodeInt32(int domainId, int key);
    void onDeleteNodeInt64(int domainId, long key);
    void onDeleteNodeUuid(int domainId, UUID uuid);
    void onDeleteNodeString(int domainId, String key);
    void onDeleteNodeBytes(int domainId, MemorySegment keyBytes);

    // --- Typed Edge Insertions ---
    void onInsertEdgeInt32(int relationId, int srcKey, int tgtKey);
    void onInsertEdgeInt64(int relationId, long srcKey, long tgtKey);
    void onInsertEdgeUuid(int relationId, UUID srcUuid, UUID tgtUuid);
    void onInsertEdgeString(int relationId, String srcKey, String tgtKey);
    void onInsertEdgeBytes(int relationId, MemorySegment srcKeyBytes, MemorySegment tgtKeyBytes);

    // --- Typed Edge Deletions ---
    void onDeleteEdgeInt32(int relationId, int srcKey, int tgtKey);
    void onDeleteEdgeInt64(int relationId, long srcKey, long tgtKey);
    void onDeleteEdgeUuid(int relationId, UUID srcUuid, UUID tgtUuid);
    void onDeleteEdgeString(int relationId, String srcKey, String tgtKey);

    // --- Control Marker ---
    void onCheckpoint(long kafkaOffset);
}
