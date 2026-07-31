package org.impulsegraph.core.csr;

import java.lang.foreign.Arena;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;

/**
 * High-performance off-heap CSR graph container holding edge relationships across domain types.
 */
public class FullCsrGraph implements AutoCloseable {

    private final Arena arena;
    private final Map<String, CsrSnapshot> relationMap = new ConcurrentHashMap<>();

    private final CsrSnapshot userToGroup;
    private final CsrSnapshot groupToParentGroup;
    private final CsrSnapshot groupToRole;
    private final CsrSnapshot roleToPermission;
    private final CsrSnapshot permissionToDataset;
    private final CsrSnapshot permissionToMenuItem;
    private final CsrSnapshot entityToAttrValue;

    // Reverse Snapshots for Audit / Reverse Queries
    private final CsrSnapshot groupToUser;
    private final CsrSnapshot roleToGroup;
    private final CsrSnapshot permissionToRole;

    // Optional Delta Layers per Snapshot
    private final Map<CsrSnapshot, CsrDeltaLayer> deltaLayers = new ConcurrentHashMap<>();

    public FullCsrGraph(
        Arena arena,
        CsrSnapshot userToGroup,
        CsrSnapshot groupToParentGroup,
        CsrSnapshot groupToRole,
        CsrSnapshot roleToPermission,
        CsrSnapshot permissionToDataset,
        CsrSnapshot permissionToMenuItem,
        CsrSnapshot entityToAttrValue,
        CsrSnapshot groupToUser,
        CsrSnapshot roleToGroup,
        CsrSnapshot permissionToRole
    ) {
        this.arena = Objects.requireNonNull(arena, "Arena must not be null");
        this.userToGroup = userToGroup;
        this.groupToParentGroup = groupToParentGroup;
        this.groupToRole = groupToRole;
        this.roleToPermission = roleToPermission;
        this.permissionToDataset = permissionToDataset;
        this.permissionToMenuItem = permissionToMenuItem;
        this.entityToAttrValue = entityToAttrValue;
        this.groupToUser = groupToUser;
        this.roleToGroup = roleToGroup;
        this.permissionToRole = permissionToRole;

        registerIfNotNull("userToGroup", userToGroup);
        registerIfNotNull("groupToParentGroup", groupToParentGroup);
        registerIfNotNull("groupToRole", groupToRole);
        registerIfNotNull("roleToPermission", roleToPermission);
        registerIfNotNull("permissionToDataset", permissionToDataset);
        registerIfNotNull("permissionToMenuItem", permissionToMenuItem);
        registerIfNotNull("entityToAttrValue", entityToAttrValue);
        registerIfNotNull("groupToUser", groupToUser);
        registerIfNotNull("roleToGroup", roleToGroup);
        registerIfNotNull("permissionToRole", permissionToRole);
    }

    public FullCsrGraph(
        Arena arena,
        CsrSnapshot userToGroup,
        CsrSnapshot groupToParentGroup,
        CsrSnapshot groupToRole,
        CsrSnapshot roleToPermission,
        CsrSnapshot permissionToDataset,
        CsrSnapshot permissionToMenuItem,
        CsrSnapshot entityToAttrValue
    ) {
        this(arena, userToGroup, groupToParentGroup, groupToRole, roleToPermission, permissionToDataset, permissionToMenuItem, entityToAttrValue, null, null, null);
    }

    public FullCsrGraph(Arena arena, Map<String, CsrSnapshot> snapshots) {
        this.arena = Objects.requireNonNull(arena, "Arena must not be null");
        if (snapshots != null) {
            this.relationMap.putAll(snapshots);
        }
        this.userToGroup = relationMap.get("userToGroup");
        this.groupToParentGroup = relationMap.get("groupToParentGroup");
        this.groupToRole = relationMap.get("groupToRole");
        this.roleToPermission = relationMap.get("roleToPermission");
        this.permissionToDataset = relationMap.get("permissionToDataset");
        this.permissionToMenuItem = relationMap.get("permissionToMenuItem");
        this.entityToAttrValue = relationMap.get("entityToAttrValue");
        this.groupToUser = relationMap.get("groupToUser");
        this.roleToGroup = relationMap.get("roleToGroup");
        this.permissionToRole = relationMap.get("permissionToRole");
    }

    private void registerIfNotNull(String name, CsrSnapshot snapshot) {
        if (snapshot != null) {
            relationMap.put(name, snapshot);
        }
    }

    public CsrSnapshot getRelationSnapshot(String relationName) {
        return relationMap.get(relationName);
    }

    public Map<String, CsrSnapshot> getAllRelationSnapshots() {
        return relationMap;
    }

    public CsrDeltaLayer getDeltaLayer(CsrSnapshot snapshot) {
        if (snapshot == null) return null;
        return deltaLayers.computeIfAbsent(snapshot, k -> new CsrDeltaLayer());
    }

    public CsrSnapshot getUserToGroup() { return userToGroup; }
    public CsrSnapshot getGroupToParentGroup() { return groupToParentGroup; }
    public CsrSnapshot getGroupToRole() { return groupToRole; }
    public CsrSnapshot getRoleToPermission() { return roleToPermission; }
    public CsrSnapshot getPermissionToDataset() { return permissionToDataset; }
    public CsrSnapshot getPermissionToMenuItem() { return permissionToMenuItem; }
    public CsrSnapshot getEntityToAttrValue() { return entityToAttrValue; }
    public CsrSnapshot getGroupToUser() { return groupToUser; }
    public CsrSnapshot getRoleToGroup() { return roleToGroup; }
    public CsrSnapshot getPermissionToRole() { return permissionToRole; }

    public long getOffHeapMemorySizeBytes() {
        long total = 0;
        for (CsrSnapshot snapshot : relationMap.values()) {
            if (snapshot != null) {
                total += snapshot.getMemoryFootprintBytes();
            }
        }
        return total;
    }

    @Override
    public void close() {
        if (arena.scope().isAlive()) {
            arena.close();
        }
    }
}
