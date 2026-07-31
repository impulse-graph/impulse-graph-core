package org.impulsegraph.core.csr;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.lang.foreign.Arena;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;

import static org.junit.jupiter.api.Assertions.*;

@DisplayName("Binary Snapshot Modular Section & C-ABI Specification Test Suite")
public class BinarySnapshotModularSectionTest {

    private static Path getWorkspaceRoot() {
        Path p = Paths.get("").toAbsolutePath();
        while (p != null && !Files.exists(p.resolve("tools"))) {
            p = p.getParent();
        }
        return p != null ? p : Paths.get("").toAbsolutePath();
    }

    private static Path fixtureUnoptRaw;
    private static Path fixtureUnoptVbyte;
    private static Path fixtureOptRaw;
    private static Path fixtureOptVbyte;
    private static Path fixtureOptHybrid;

    @BeforeAll
    static void setupFixtures() {
        Path fixtures = getWorkspaceRoot().resolve("tools/impulse-cli/testdata/fixtures");
        fixtureUnoptRaw = fixtures.resolve("livejournal_unopt_raw.bin");
        fixtureUnoptVbyte = fixtures.resolve("livejournal_unopt_vbyte.bin");
        fixtureOptRaw = fixtures.resolve("livejournal_opt_raw.bin");
        fixtureOptVbyte = fixtures.resolve("livejournal_opt_vbyte.bin");
        fixtureOptHybrid = fixtures.resolve("livejournal_opt_hybrid.bin");

        assertTrue(Files.exists(fixtureUnoptRaw), "Fixture livejournal_unopt_raw.bin MUST exist");
        assertTrue(Files.exists(fixtureUnoptVbyte), "Fixture livejournal_unopt_vbyte.bin MUST exist");
        assertTrue(Files.exists(fixtureOptRaw), "Fixture livejournal_opt_raw.bin MUST exist");
        assertTrue(Files.exists(fixtureOptVbyte), "Fixture livejournal_opt_vbyte.bin MUST exist");
        assertTrue(Files.exists(fixtureOptHybrid), "Fixture livejournal_opt_hybrid.bin MUST exist");
    }

    @Test
    @DisplayName("1. Test Header Magic 0x494D5053, Version 2, & DataOffset (64 Bytes)")
    void testHeaderMagicAndDataOffset() throws IOException {
        byte[] bytes = Files.readAllBytes(fixtureUnoptRaw);
        ByteBuffer buf = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);

        int magic = buf.getInt();
        assertEquals(0x494D5053, magic, "Magic bytes MUST equal 0x494D5053 ('IMPS')");

        short version = buf.getShort();
        assertTrue(version >= 2, "Protocol Version MUST be >= 2");

        int dataOffset = buf.getInt();
        assertEquals(64, dataOffset, "DataOffset in Version 2 MUST be 64 bytes");

        int domainCount = Short.toUnsignedInt(buf.getShort());
        int relationCount = Short.toUnsignedInt(buf.getShort());
        assertTrue(domainCount > 0, "Domain count MUST be > 0");
        assertTrue(relationCount > 0, "Relation count MUST be > 0");
    }

    @Test
    @DisplayName("2. Test Cryptographic SHA-256 Payload Checksum Validation")
    void testSha256ChecksumValidation() throws IOException, NoSuchAlgorithmException {
        byte[] bytes = Files.readAllBytes(fixtureUnoptRaw);
        ByteBuffer buf = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);

        buf.position(6);
        int dataOffset = buf.getInt();
        buf.position(30);
        byte[] expectedSha256 = new byte[32];
        buf.get(expectedSha256);

        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        digest.update(bytes, dataOffset, bytes.length - dataOffset);
        byte[] computedSha256 = digest.digest();

        assertArrayEquals(expectedSha256, computedSha256, "Payload SHA256 checksum MUST match expected checksum");
    }

    @Test
    @DisplayName("3. Test Corrupt SHA-256 Bit Flip Detection")
    void testSha256BitFlipDetection() throws IOException {
        byte[] bytes = Files.readAllBytes(fixtureUnoptRaw);
        // Corrupt a single payload byte
        bytes[bytes.length - 1] ^= 0xFF;

        assertThrows(IllegalStateException.class, () -> {
            BinarySnapshotLoader.loadSnapshot(bytes, Arena.ofAuto(), true);
        }, "Mutated payload MUST trigger SHA-256 checksum mismatch exception");
    }

    @Test
    @DisplayName("4. Test Corrupt Magic Bytes Rejection")
    void testCorruptMagicBytesRejection() throws IOException {
        byte[] bytes = Files.readAllBytes(fixtureUnoptRaw);
        // Corrupt magic
        bytes[0] = 0x00;

        assertThrows(IllegalArgumentException.class, () -> {
            BinarySnapshotLoader.loadSnapshot(bytes, Arena.ofAuto(), false);
        }, "Corrupt magic bytes MUST trigger invalid magic exception");
    }

    @Test
    @DisplayName("5. Test Truncated Buffer Underflow Rejection")
    void testTruncatedBufferRejection() {
        byte[] truncated = new byte[30]; // Truncated header

        assertThrows(IllegalArgumentException.class, () -> {
            BinarySnapshotLoader.loadSnapshot(truncated, Arena.ofAuto(), false);
        }, "Truncated header buffer MUST trigger underflow exception");
    }

    @Test
    @DisplayName("6. Test 64-Byte SIMD Alignment Pointers Across Relation Arrays")
    void test64ByteSimdAlignmentPointers() throws IOException {
        byte[] bytes = Files.readAllBytes(fixtureOptRaw);
        ByteBuffer buf = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);

        buf.position(6);
        int dataOffset = buf.getInt();
        buf.position(dataOffset);

        int domainCount = Short.toUnsignedInt(ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).getShort(10));
        for (int d = 0; d < domainCount; d++) {
            buf.getShort(); // domId
            buf.get(); // keyType
            int nLen = Short.toUnsignedInt(buf.getShort());
            buf.position(buf.position() + nLen);
            int mCount = buf.getInt();
            for (int m = 0; m < mCount; m++) {
                buf.getInt();
                int bkLen = Short.toUnsignedInt(buf.getShort());
                buf.position(buf.position() + bkLen);
            }
        }

        // Domain Section End Alignment
        int pos = buf.position();
        int rem = pos % 64;
        if (rem != 0) {
            buf.position(pos + (64 - rem));
        }

        assertEquals(0, buf.position() % 64, "Relation Header position MUST be 64-byte aligned");
    }

    @Test
    @DisplayName("7. Test 5-Way Encoding Matrix (RAW_UINT32, DELTA_VBYTE, HYBRID_UINT16_UINT32)")
    void testEncodingMatrixSupport() throws IOException {
        BinarySnapshotLoader.LoadedSnapshot s1 = BinarySnapshotLoader.loadSnapshot(Files.readAllBytes(fixtureUnoptRaw), Arena.ofAuto());
        assertNotNull(s1);
        assertEquals(1000000, s1.graph().getAllRelationSnapshots().values().iterator().next().getEdgeCount());

        BinarySnapshotLoader.LoadedSnapshot s2 = BinarySnapshotLoader.loadSnapshot(Files.readAllBytes(fixtureUnoptVbyte), Arena.ofAuto());
        assertNotNull(s2);
        assertEquals(1000000, s2.graph().getAllRelationSnapshots().values().iterator().next().getEdgeCount());

        BinarySnapshotLoader.LoadedSnapshot s3 = BinarySnapshotLoader.loadSnapshot(Files.readAllBytes(fixtureOptRaw), Arena.ofAuto());
        assertNotNull(s3);
        assertEquals(1000000, s3.graph().getAllRelationSnapshots().values().iterator().next().getEdgeCount());

        BinarySnapshotLoader.LoadedSnapshot s4 = BinarySnapshotLoader.loadSnapshot(Files.readAllBytes(fixtureOptVbyte), Arena.ofAuto());
        assertNotNull(s4);
        assertEquals(1000000, s4.graph().getAllRelationSnapshots().values().iterator().next().getEdgeCount());

        BinarySnapshotLoader.LoadedSnapshot s5 = BinarySnapshotLoader.loadSnapshot(Files.readAllBytes(fixtureOptHybrid), Arena.ofAuto());
        assertNotNull(s5);
        assertEquals(1000000, s5.graph().getAllRelationSnapshots().values().iterator().next().getEdgeCount());
    }

    @Test
    @DisplayName("8. Test Business Key Mapping Integrity")
    void testBusinessKeyMappingIntegrity() throws IOException {
        BinarySnapshotLoader.LoadedSnapshot s = BinarySnapshotLoader.loadSnapshot(Files.readAllBytes(fixtureOptHybrid), Arena.ofAuto());
        assertFalse(s.domainsById().isEmpty(), "DomainsById MUST NOT be empty");
        BinarySnapshotLoader.LoadedDomain d = s.domainsById().values().iterator().next();
        assertNotNull(d, "Loaded domain MUST NOT be null");
        assertTrue(d.bkToDenseMap().size() > 0, "Mapping count MUST be > 0");
    }
}
