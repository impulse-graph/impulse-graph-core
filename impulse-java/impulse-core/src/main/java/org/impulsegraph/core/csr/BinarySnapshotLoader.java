package org.impulsegraph.core.csr;

import java.io.IOException;
import java.lang.foreign.Arena;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.*;

public class BinarySnapshotLoader {

    public static final int SNAPSHOT_MAGIC = 0x494D5053; // "IMPS"

    public record LoadedDomain(int id, String name, byte keyType, Map<String, Integer> bkToDenseMap) {}

    public record LoadedSnapshot(
            int magic,
            short version,
            int domainCount,
            int relationCount,
            long kafkaOffset,
            long timestampMs,
            String sha256Hex,
            Map<Integer, LoadedDomain> domainsById,
            Map<String, LoadedDomain> domainsByName,
            FullCsrGraph graph
    ) {}

    public static LoadedSnapshot loadSnapshot(Path filePath, Arena arena) throws IOException {
        byte[] bytes = Files.readAllBytes(filePath);
        return loadSnapshot(bytes, arena, false);
    }

    public static LoadedSnapshot loadSnapshot(byte[] data, Arena arena) {
        return loadSnapshot(data, arena, false);
    }

    public static LoadedSnapshot loadSnapshot(byte[] data, Arena arena, boolean verifyChecksum) {
        Objects.requireNonNull(data, "snapshot data must not be null");
        Objects.requireNonNull(arena, "arena must not be null");

        if (data.length < 58) {
            throw new IllegalArgumentException("snapshot file size (" + data.length + " bytes) is smaller than 58-byte header");
        }

        ByteBuffer buf = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN);

        int magic = buf.getInt();
        if (magic != SNAPSHOT_MAGIC) {
            throw new IllegalArgumentException(String.format("invalid snapshot magic 0x%X (expected 0x%X)", magic, SNAPSHOT_MAGIC));
        }

        short version = buf.getShort();
        int dataOffset = 58;
        int domainCount = 0;
        int relationCount = 0;

        if (version >= 2) {
            dataOffset = buf.getInt();
            domainCount = Short.toUnsignedInt(buf.getShort());
            relationCount = Short.toUnsignedInt(buf.getShort());
        } else {
            domainCount = Short.toUnsignedInt(buf.getShort());
            relationCount = Short.toUnsignedInt(buf.getShort());
        }

        long kafkaOffset = buf.getLong();
        long timestampMs = buf.getLong();

        byte[] expectedSha256 = new byte[32];
        buf.get(expectedSha256);

        // Verify SHA256 payload checksum if enabled
        if (verifyChecksum) {
            byte[] payload = Arrays.copyOfRange(data, dataOffset, data.length);
            try {
                MessageDigest md = MessageDigest.getInstance("SHA-256");
                byte[] actualSha256 = md.digest(payload);
                if (!Arrays.equals(expectedSha256, actualSha256)) {
                    throw new IllegalStateException("SHA256 checksum mismatch on snapshot binary load!");
                }
            } catch (NoSuchAlgorithmException e) {
                throw new RuntimeException("SHA-256 algorithm missing", e);
            }
        }

        String sha256Hex = bytesToHex(expectedSha256);
        buf.position(dataOffset);

        // Parse Domain Section
        Map<Integer, LoadedDomain> domainsById = new HashMap<>();
        Map<String, LoadedDomain> domainsByName = new HashMap<>();

        for (int i = 0; i < domainCount; i++) {
            int domId = Short.toUnsignedInt(buf.getShort());
            byte keyType = buf.get();
            int nameLen = Short.toUnsignedInt(buf.getShort());

            byte[] nameBytes = new byte[nameLen];
            buf.get(nameBytes);
            String domName = new String(nameBytes, StandardCharsets.UTF_8);

            int mapCount = buf.getInt();
            Map<String, Integer> bkToDense = new HashMap<>(mapCount);
            for (int m = 0; m < mapCount; m++) {
                int denseId = buf.getInt();
                int bkLen = Short.toUnsignedInt(buf.getShort());
                byte[] bkBytes = new byte[bkLen];
                buf.get(bkBytes);
                String bk = new String(bkBytes, StandardCharsets.UTF_8);
                bkToDense.put(bk, denseId);
            }

            LoadedDomain domain = new LoadedDomain(domId, domName, keyType, bkToDense);
            domainsById.put(domId, domain);
            domainsByName.put(domName, domain);
        }

        if (version >= 2) {
            align64(buf);
        }

        // Parse Relation Section (CSR Adjacency Matrices)
        Map<String, CsrSnapshot> relationSnapshots = new HashMap<>();

        for (int j = 0; j < relationCount; j++) {
            int srcDomId = Short.toUnsignedInt(buf.getShort());
            int tgtDomId = Short.toUnsignedInt(buf.getShort());

            byte encodingType = 0x00;
            if (version >= 2) {
                encodingType = buf.get();
            }

            int nodeCount = buf.getInt();
            long edgeCount = buf.getLong();
            long rowOffBytes = buf.getLong();
            long colIdxBytes = buf.getLong();

            LoadedDomain srcDom = domainsById.get(srcDomId);
            LoadedDomain tgtDom = domainsById.get(tgtDomId);
            String relName = (srcDom != null && tgtDom != null) ?
                    srcDom.name().toLowerCase() + "To" + capitalize(tgtDom.name().toLowerCase()) :
                    "relation_" + j;

            int numRowOffsets = (int) (rowOffBytes / 4);
            if (version >= 2) {
                align64(buf);
            }
            int[] rowOffsetsData = new int[numRowOffsets];
            for (int r = 0; r < numRowOffsets; r++) {
                rowOffsetsData[r] = buf.getInt();
            }

            if (version >= 2) {
                align64(buf);
            }

            int[] columnIndicesData = new int[(int) edgeCount];
            if (encodingType == 0x01) {
                int colPtr = 0;
                for (int node = 0; node <= nodeCount; node++) {
                    int start = rowOffsetsData[node];
                    int end = rowOffsetsData[node + 1];
                    int prevTgt = 0;
                    for (int idx = start; idx < end; idx++) {
                        int delta = readVByte(buf);
                        int tgt = (idx == start) ? delta : (prevTgt + delta);
                        columnIndicesData[colPtr++] = tgt;
                        prevTgt = tgt;
                    }
                }
            } else if (encodingType == 0x02) {
                int numColIndices = (int) (colIdxBytes / 2);
                for (int c = 0; c < numColIndices; c++) {
                    columnIndicesData[c] = Short.toUnsignedInt(buf.getShort());
                }
            } else if (encodingType == 0x03) {
                int colPtr = 0;
                for (int node = 0; node <= nodeCount; node++) {
                    int start = rowOffsetsData[node];
                    int end = rowOffsetsData[node + 1];
                    int rowLen = end - start;
                    int numHot = Short.toUnsignedInt(buf.getShort());
                    for (int i = 0; i < numHot; i++) {
                        columnIndicesData[colPtr++] = Short.toUnsignedInt(buf.getShort());
                    }
                    for (int i = numHot; i < rowLen; i++) {
                        columnIndicesData[colPtr++] = buf.getInt();
                    }
                }
            } else {
                int numColIndices = (int) (colIdxBytes / 4);
                for (int c = 0; c < numColIndices; c++) {
                    columnIndicesData[c] = buf.getInt();
                }
            }

            if (version >= 2) {
                align64(buf);
            }

            CsrSnapshot csrSnapshot = new CsrSnapshot(arena, nodeCount, (int) edgeCount, rowOffsetsData, columnIndicesData);
            relationSnapshots.put(relName, csrSnapshot);
        }

        FullCsrGraph fullGraph = new FullCsrGraph(arena, relationSnapshots);

        return new LoadedSnapshot(
                magic, version, domainCount, relationCount,
                kafkaOffset, timestampMs, sha256Hex,
                domainsById, domainsByName, fullGraph
        );
    }

    private static int readVByte(ByteBuffer buf) {
        int val = 0;
        int shift = 0;
        while (true) {
            byte b = buf.get();
            val |= (b & 0x7F) << shift;
            if ((b & 0x80) == 0) break;
            shift += 7;
        }
        return val;
    }

    private static void align64(ByteBuffer buf) {
        int rem = buf.position() % 64;
        if (rem != 0) {
            buf.position(buf.position() + (64 - rem));
        }
    }

    private static String capitalize(String str) {
        if (str == null || str.isEmpty()) return str;
        return str.substring(0, 1).toUpperCase() + str.substring(1);
    }

    private static String bytesToHex(byte[] bytes) {
        StringBuilder sb = new StringBuilder();
        for (byte b : bytes) {
            sb.append(String.format("%02x", b));
        }
        return sb.toString();
    }
}
