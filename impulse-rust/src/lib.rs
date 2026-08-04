//! Impulse Graph Engine Core Rust Crate (Spec v0.9.0)
//!
//! Provides zero-copy memory-mapped reading, zero-dependency binary parsing,
//! Structure of Arrays (SoA) attribute accessors, and binary snapshot generation.

pub mod ffi;
pub mod mmap;
pub mod reader;
pub mod simd;
pub mod spec;
pub mod writer;

pub use ffi::*;
pub use reader::{AttributeInfo, DomainInfo, RelationInfo, SnapshotReader};
pub use spec::{
    BaseDataType, EncodingType, ImpulseError, KeyType, SnapshotHeader,
    IMPULSE_MAGIC, IMPULSE_VERSION_PACKED,
};
pub use writer::{AttributeField, SnapshotWriter};

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    const TEST_SNAPSHOT: &str = "__impulse_test_rust_v09.imps";

    #[test]
    fn test_sha256_known_vector() {
        // SHA-256("abc") = ba7816bf 8f01cfea 414140de 5dae2223 b00361a3 96177a9c b410ff61 f20015ad
        let hash = spec::compute_sha256(b"abc");
        let expected: [u8; 32] = [
            0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae,
            0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61,
            0xf2, 0x00, 0x15, 0xad,
        ];
        assert_eq!(hash, expected);
    }

    #[test]
    fn test_crc16_known_vector() {
        // CRC-16-CCITT test
        let crc = spec::compute_crc16(b"123456789");
        assert!(crc > 0);
    }

    #[test]
    fn test_v09_roundtrip_write_read() {
        // 1. Create v0.9.0 Snapshot with 1 Domain and 1 Relation
        // Domain 0: "User"
        // Relation 0: "Follows" (0->1, 0->2, 1->2, 2->3, 3->0)
        let mut writer = SnapshotWriter::new(TEST_SNAPSHOT);

        writer.add_domain(0, KeyType::Int32, "User");

        let row_offsets = vec![0u32, 2, 3, 4, 5];
        let col_indices = vec![1u32, 2, 2, 3, 0];

        writer.add_relation(
            0,
            0,
            4,
            5,
            row_offsets,
            col_indices,
        );

        // Add Edge Attribute (SoA mode): "weight" (float32)
        let mut weight_bytes = Vec::new();
        weight_bytes.extend_from_slice(&1.5f32.to_ne_bytes());
        weight_bytes.extend_from_slice(&2.5f32.to_ne_bytes());
        weight_bytes.extend_from_slice(&3.5f32.to_ne_bytes());
        weight_bytes.extend_from_slice(&4.5f32.to_ne_bytes());
        weight_bytes.extend_from_slice(&5.5f32.to_ne_bytes());

        writer.add_attribute_to_relation(
            0,
            "weight",
            BaseDataType::Float32 as u8,
            1, // scalar
            weight_bytes,
            None,
        );

        // Set metadata
        let mut metadata = std::collections::HashMap::new();
        metadata.insert("tenant_id".to_string(), "tenant_999".to_string());
        metadata.insert("build_env".to_string(), "production".to_string());
        writer.set_metadata(metadata);

        let finalize_res = writer.finalize();
        assert!(finalize_res.is_ok(), "Finalize failed: {:?}", finalize_res);

        // 2. Read Snapshot using SnapshotReader
        let reader = SnapshotReader::open(TEST_SNAPSHOT).unwrap();

        assert_eq!(reader.header().magic(), IMPULSE_MAGIC);
        assert_eq!(reader.header().version(), IMPULSE_VERSION_PACKED);
        assert_eq!(reader.domain_count(), 1);
        assert_eq!(reader.relation_count(), 1);

        // Test Adjacency Queries
        assert!(reader.is_adjacent(0, 0, 1).unwrap());
        assert!(reader.is_adjacent(0, 0, 2).unwrap());
        assert!(reader.is_adjacent(0, 1, 2).unwrap());
        assert!(reader.is_adjacent(0, 2, 3).unwrap());
        assert!(reader.is_adjacent(0, 3, 0).unwrap());

        assert!(!reader.is_adjacent(0, 0, 3).unwrap());
        assert!(!reader.is_adjacent(0, 1, 0).unwrap());

        // Test Zero-Copy Neighbor Slicing
        let nbrs_0 = reader.get_neighbors(0, 0).unwrap();
        assert_eq!(nbrs_0, &[1, 2]);

        let nbrs_1 = reader.get_neighbors(0, 1).unwrap();
        assert_eq!(nbrs_1, &[2]);

        // Test Metadata
        let meta = reader.get_metadata().unwrap();
        assert_eq!(meta.get("tenant_id").unwrap(), "tenant_999");
        assert_eq!(meta.get("build_env").unwrap(), "production");

        // Cleanup
        let _ = fs::remove_file(TEST_SNAPSHOT);
    }

    #[test]
    fn test_zero_node_relation_and_1023_node_alignment() {
        const TEST_FILE: &str = "__impulse_test_alignment_v09.imps";
        let mut writer = SnapshotWriter::new(TEST_FILE);

        writer.add_domain(0, KeyType::Int32, "User");

        // Relation 0: Node count = 0, edge count = 0 (1 row offset [0] = 4 bytes)
        writer.add_relation(0, 0, 0, 0, vec![0u32], vec![]);

        // Relation 1: Node count = 1023 (requires N + 1 = 1024 int32 row offsets = 4096 bytes)
        let row_offs_1024 = vec![0u32; 1024];
        writer.add_relation(0, 0, 1023, 0, row_offs_1024, vec![]);

        // Relation 2: Follower relation
        writer.add_relation(0, 0, 1, 1, vec![0u32, 1], vec![0u32]);

        assert!(writer.finalize().is_ok());

        let reader = SnapshotReader::open(TEST_FILE).unwrap();
        assert_eq!(reader.relation_count(), 3);

        let rels = reader.relations();

        // 1. Verify relation 0 (node_count = 0) has 1 row offset = 4 bytes, 0 column bytes
        assert_eq!(rels[0].node_count, 0);
        assert_eq!(rels[0].csr_row_off_bytes, 4); // N + 1 = 1 uint32 offset [0]
        assert_eq!(rels[0].csr_col_idx_bytes, 0);  // 0 column indices

        // 2. Verify relation 1: node_count = 1023 has N + 1 = 1024 uint32 row offsets = 4096 bytes
        assert_eq!(rels[1].node_count, 1023);
        assert_eq!(rels[1].csr_row_off_bytes, 4096); // 1024 * 4 = 4096 bytes
        assert_eq!(rels[1].csr_col_idx_bytes, 0);

        // Verify relation 1 row_offsets ends on a 4KB boundary and relation 2 block starts immediately
        let rel1_end_offset = rels[1].csr_row_off_offset + rels[1].csr_row_off_bytes;
        assert_eq!(rel1_end_offset % 4096, 0, "Relation 1 end offset 0x{:X} should land exactly on 4KB page boundary", rel1_end_offset);

        // Relation 2 start offset should match rel1_end_offset
        assert_eq!(rels[2].csr_row_off_offset, rel1_end_offset, "Relation 2 row offset 0x{:X} should start immediately at 4KB page boundary 0x{:X}", rels[2].csr_row_off_offset, rel1_end_offset);

        let _ = fs::remove_file(TEST_FILE);
    }
}


