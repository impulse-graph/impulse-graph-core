package org.impulsegraph.domain.id;

import java.util.Collection;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Bi-directional string BusinessKey <-> dense INT32 identifier mapper.
 */
public class StringIdMapper {

    private final Map<String, Integer> bkToDenseMap = new ConcurrentHashMap<>();
    private final Map<Integer, String> denseToBkMap = new ConcurrentHashMap<>();
    private final AtomicInteger sequence = new AtomicInteger(0);

    public int getOrAssignId(String businessKey) {
        Objects.requireNonNull(businessKey, "businessKey must not be null");
        return bkToDenseMap.computeIfAbsent(businessKey, k -> {
            int newId = sequence.getAndIncrement();
            denseToBkMap.put(newId, k);
            return newId;
        });
    }

    public int getDenseId(String businessKey) {
        return bkToDenseMap.getOrDefault(businessKey, -1);
    }

    public String getBusinessKey(int denseId) {
        return denseToBkMap.get(denseId);
    }

    public void remove(String businessKey) {
        Integer denseId = bkToDenseMap.remove(businessKey);
        if (denseId != null) {
            denseToBkMap.remove(denseId);
        }
    }

    public Collection<String> getAllBusinessKeys() {
        return bkToDenseMap.keySet();
    }

    public int size() {
        return bkToDenseMap.size();
    }

    public int getNextId() {
        return sequence.get();
    }
}
