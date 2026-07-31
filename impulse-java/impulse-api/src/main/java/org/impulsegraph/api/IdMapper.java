package org.impulsegraph.domain.id;

import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Pod-local bi-directional mapping between external 128-bit UUIDs and local dense INT32 identifiers.
 */
public class IdMapper {

    private final String domainType;
    private final ConcurrentHashMap<UUID, Integer> uuidToIntMap = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<Integer, UUID> intToUuidMap = new ConcurrentHashMap<>();
    private final AtomicInteger sequence = new AtomicInteger(1);

    public IdMapper(String domainType) {
        this.domainType = Objects.requireNonNull(domainType, "domainType must not be null");
    }

    public String getDomainType() {
        return domainType;
    }

    /**
     * Obtains or assigns a local INT32 ID for a given UUID.
     */
    public int getOrAssignId(UUID uuid) {
        Objects.requireNonNull(uuid, "uuid must not be null");
        return uuidToIntMap.computeIfAbsent(uuid, k -> {
            int newId = sequence.getAndIncrement();
            intToUuidMap.put(newId, k);
            return newId;
        });
    }

    /**
     * Registers a known UUID <-> INT32 mapping (e.g., loaded from RocksDB).
     */
    public void registerMapping(UUID uuid, int id) {
        Objects.requireNonNull(uuid, "uuid must not be null");
        uuidToIntMap.put(uuid, id);
        intToUuidMap.put(id, uuid);
        sequence.updateAndGet(current -> Math.max(current, id + 1));
    }

    /**
     * Resolves a local INT32 ID back to its UUID.
     */
    public UUID getUuid(int id) {
        return intToUuidMap.get(id);
    }

    /**
     * Look up INT32 ID for UUID without assigning.
     */
    public Integer getId(UUID uuid) {
        return uuidToIntMap.get(uuid);
    }

    public int size() {
        return uuidToIntMap.size();
    }

    public void clear() {
        uuidToIntMap.clear();
        intToUuidMap.clear();
        sequence.set(1);
    }
}
