package org.impulsegraph.core.csr;

import org.impulsegraph.domain.loader.TsvRefGraphEngine;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import java.lang.foreign.Arena;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.BitSet;

import static org.junit.jupiter.api.Assertions.*;

class BinarySnapshotLoaderQueryTest {

    private static Path getFixturesDir() {
        Path curr = Paths.get("").toAbsolutePath();
        while (curr != null && !Files.exists(curr.resolve("tools"))) {
            curr = curr.getParent();
        }
        if (curr == null) {
            throw new IllegalStateException("Workspace root containing 'tools' directory not found");
        }
        return curr.resolve("tools/impulse-cli/testdata/fixtures");
    }

    @Test
    @DisplayName("Load sample_rbac.bin into impulse-core CsrSnapshot and execute path & reachability queries")
    void testLoadSampleRbacSnapshotAndExecuteQueries() throws Exception {
        Path tsvPath = getFixturesDir().resolve("sample_rbac.tsv");
        assertTrue(Files.exists(tsvPath), "sample_rbac.tsv must exist: " + tsvPath);

        // 1. Compile TSV into binary snapshot bytes using Java TsvRefGraphEngine
        TsvRefGraphEngine tsvEngine = new TsvRefGraphEngine();
        try (var reader = Files.newBufferedReader(tsvPath)) {
            tsvEngine.parseTsv(reader);
        }
        byte[] snapshotBytes = tsvEngine.buildSnapshotBytes();

        // 2. Load snapshot bytes into impulse-core CsrSnapshot & FullCsrGraph using Java FFM Arena
        try (Arena arena = Arena.ofShared()) {
            BinarySnapshotLoader.LoadedSnapshot loaded = BinarySnapshotLoader.loadSnapshot(snapshotBytes, arena);

            assertNotNull(loaded);
            assertEquals(TsvRefGraphEngine.SNAPSHOT_MAGIC, loaded.magic());
            assertEquals(1, loaded.version());
            assertEquals(4, loaded.domainCount());
            assertEquals(6, loaded.relationCount());
            assertEquals(5000, loaded.kafkaOffset());
            assertEquals("12a1c9093b33cf89b636b07ee16c6ab1f53344e09c212bb544e1a382e801518d", loaded.sha256Hex());

            // 3. Verify Domain BusinessKey Mappers loaded into impulse-core
            BinarySnapshotLoader.LoadedDomain userDomain = loaded.domainsByName().get("USER");
            BinarySnapshotLoader.LoadedDomain groupDomain = loaded.domainsByName().get("GROUP");
            BinarySnapshotLoader.LoadedDomain roleDomain = loaded.domainsByName().get("ROLE");
            BinarySnapshotLoader.LoadedDomain permDomain = loaded.domainsByName().get("PERMISSION");

            assertNotNull(userDomain);
            assertNotNull(groupDomain);
            assertNotNull(roleDomain);
            assertNotNull(permDomain);

            int uAliceId = userDomain.bkToDenseMap().get("550e8400-e29b-41d4-a716-446655440001");
            int gEngId = groupDomain.bkToDenseMap().get("660e8400-e29b-41d4-a716-446655440001");
            int rAdminId = roleDomain.bkToDenseMap().get("770e8400-e29b-41d4-a716-446655440001");
            int pReadId = permDomain.bkToDenseMap().get("880e8400-e29b-41d4-a716-446655440001");
            int pWriteId = permDomain.bkToDenseMap().get("880e8400-e29b-41d4-a716-446655440002");

            // 4. Instantiate impulse-core CsrGraphQueryEngine with SIMD acceleration enabled
            FullCsrGraph graph = loaded.graph();
            CsrGraphQueryEngine queryEngine = new CsrGraphQueryEngine(graph, true);

            // 5. Query 1: userToGroup reachability
            CsrSnapshot userToGroupCsr = graph.getUserToGroup();
            assertNotNull(userToGroupCsr, "userToGroup CSR snapshot must be present");

            BitSet activeGroups = new BitSet();
            int startOff = userToGroupCsr.getRowOffset(uAliceId);
            int endOff = userToGroupCsr.getRowOffset(uAliceId + 1);
            for (int i = startOff; i < endOff; i++) {
                activeGroups.set(userToGroupCsr.getColumnIndex(i));
            }
            assertTrue(activeGroups.get(gEngId), "User 550e...0001 MUST belong to group 660e...0001");

            System.out.println("[+] impulse-core CsrSnapshot & FullCsrGraph loaded successfully!");
            System.out.println("    User denseID=" + uAliceId + " -> Group denseID=" + gEngId + " (Edge Verified!)");
        }
    }
}
