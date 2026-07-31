package org.impulsegraph.core.delta;

import org.impulsegraph.core.csr.BinarySnapshotLoader;
import org.impulsegraph.core.csr.FullCsrGraph;
import org.impulsegraph.domain.loader.TsvRefGraphEngine;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import java.lang.foreign.Arena;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

import static org.junit.jupiter.api.Assertions.*;

class ImpulseCoreLiveDeltaCompactionTest {

    private static Path getFixturesDir() {
        Path curr = Paths.get("").toAbsolutePath();
        while (curr != null && !Files.exists(curr.resolve("tools"))) {
            curr = curr.getParent();
        }
        if (curr == null) {
            throw new IllegalStateException("Workspace root containing 'tools' directory not found");
        }
        return curr.resolve("tools/impulse-cli/testdata/fixtures/edge_cases");
    }

    @Test
    @DisplayName("Feed live TSV stream into impulse-core OpCode processor, trigger A/B swap, and export snapshot byte-for-byte matching Go impulse-cli")
    void testImpulseCoreLiveDeltaCompactionAndAbSwap() throws Exception {
        Path tsvPath = getFixturesDir().resolve("ec07_node_delete_cascading_edges.tsv");
        assertTrue(Files.exists(tsvPath), "Fixture must exist: " + tsvPath);

        try (Arena arena = Arena.ofShared()) {
            DefaultOpCodeDeltaProcessor processor = new DefaultOpCodeDeltaProcessor(arena);

            // Parse live TSV stream into impulse-core OpCode delta processor
            try (var reader = Files.newBufferedReader(tsvPath)) {
                processor.getEngine().parseTsv(reader);
            }

            // Trigger compaction & atomic A/B pointer swap in impulse-core
            byte[] exportedSnapshotBytes = processor.triggerCompactionAndSwap();

            assertNotNull(exportedSnapshotBytes);
            assertTrue(exportedSnapshotBytes.length >= 58);

            // Calculate SHA256 of exported binary snapshot
            String exportedSha256 = TsvRefGraphEngine.calculateSha256Hex(exportedSnapshotBytes);

            // Expected Go impulse-cli SHA256 hash for EC07
            String expectedGoSha256 = "9a2acff877110b2690b7c9e5276c7ea9c3496c1a0f21d51d6cb28037bad5b209";

            assertEquals(expectedGoSha256, exportedSha256,
                    "Exported impulse-core binary snapshot MUST match golang-cli SHA256 byte-for-byte!");

            // Verify atomic A/B swap occurred in impulse-core
            FullCsrGraph activeGraph = processor.getSwapManager().getCurrent();
            assertNotNull(activeGraph, "Active swapped FullCsrGraph must not be null");
            assertTrue(activeGraph.getOffHeapMemorySizeBytes() > 0, "Swapped graph must occupy off-heap memory");

            System.out.println("[+] impulse-core Live Delta Compaction & A/B Swap PASSED!");
            System.out.println("    Active Swapped Graph Off-Heap Size: " + activeGraph.getOffHeapMemorySizeBytes() + " bytes");
            System.out.println("    Exported SHA256 Hash:               " + exportedSha256);
            System.out.println("    Match Status:                        EXACT BYTE-FOR-BYTE MATCH WITH GOLANG-CLI ✅");
        }
    }
}
