//! Impulse Graph Engine Core Rust Crate (Spec v2.4)
//!
//! Provides zero-copy memory-mapped reading, zero-dependency binary parsing,
//! property accessors (AoS/SoA), and binary snapshot generation.

pub mod ffi;
pub mod mmap;
pub mod reader;
pub mod simd;
pub mod spec;
pub mod writer;

pub use ffi::*;
pub use reader::{DomainInfo, RelationInfo, SnapshotReader};
pub use spec::{
    AuxSectionType, DataType, EncodingType, ImpulseError, KeyType, SnapshotHeader,
    IMPULSE_MAGIC, IMPULSE_VERSION_PACKED,
};
pub use writer::{PropertyBlock, PropertyField, SnapshotWriter};

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    const TEST_SNAPSHOT: &str = "__impulse_test_rust_v24.imps";

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
    fn test_crc32c_known_vector() {
        // CRC-32C("123456789") = 0xE3069283
        let crc = spec::compute_crc32c(b"123456789");
        assert_eq!(crc, 0xE3069283);
    }

    #[test]
    fn test_roundtrip_write_read_properties() {
        // 1. Create Snapshot with 1 Domain and 1 Relation
        // Domain 0: "User" (4 nodes)
        // Relation 0: "Follows" (0->1, 0->2, 1->2, 2->3, 3->0)
        let mut writer = SnapshotWriter::new(TEST_SNAPSHOT);

        writer.add_domain(0, KeyType::Int32, "User", 4);

        // Add Node Properties (AoS mode): "age" (uint32)
        let mut age_bytes = Vec::new();
        age_bytes.extend_from_slice(&25u32.to_ne_bytes()); // node 0: 25
        age_bytes.extend_from_slice(&30u32.to_ne_bytes()); // node 1: 30
        age_bytes.extend_from_slice(&35u32.to_ne_bytes()); // node 2: 35
        age_bytes.extend_from_slice(&40u32.to_ne_bytes()); // node 3: 40

        writer.add_domain_fixed_props(
            0,
            false, // AoS
            vec![PropertyField {
                name: "age".to_string(),
                data_type: DataType::Uint32,
                data: age_bytes,
            }],
        );

        let row_offsets = vec![0u32, 2, 3, 4, 5];
        let col_indices = vec![1u32, 2, 2, 3, 0];

        writer.add_relation(
            0,
            0,
            EncodingType::RawUint32,
            4,
            5,
            row_offsets,
            col_indices,
        );

        // Add Edge Properties (SoA mode): "weight" (float32)
        let mut weight_bytes = Vec::new();
        weight_bytes.extend_from_slice(&1.5f32.to_ne_bytes());
        weight_bytes.extend_from_slice(&2.5f32.to_ne_bytes());
        weight_bytes.extend_from_slice(&3.5f32.to_ne_bytes());
        weight_bytes.extend_from_slice(&4.5f32.to_ne_bytes());
        weight_bytes.extend_from_slice(&5.5f32.to_ne_bytes());

        writer.add_relation_fixed_props(
            0,
            true, // SoA
            vec![PropertyField {
                name: "weight".to_string(),
                data_type: DataType::Float32,
                data: weight_bytes,
            }],
        );

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

        // Test Node Property Reading (AoS)
        let age_node_0 = reader.get_node_property(0, 0, "age").unwrap().unwrap();
        let val_0 = u32::from_ne_bytes(age_node_0.try_into().unwrap());
        assert_eq!(val_0, 25);

        let age_node_2 = reader.get_node_property(0, 2, "age").unwrap().unwrap();
        let val_2 = u32::from_ne_bytes(age_node_2.try_into().unwrap());
        assert_eq!(val_2, 35);

        // Test Edge Property Reading (SoA)
        let weight_edge_0 = reader.get_edge_property(0, 0, "weight").unwrap().unwrap();
        let w_0 = f32::from_ne_bytes(weight_edge_0.try_into().unwrap());
        assert_eq!(w_0, 1.5);

        let weight_edge_3 = reader.get_edge_property(0, 3, "weight").unwrap().unwrap();
        let w_3 = f32::from_ne_bytes(weight_edge_3.try_into().unwrap());
        assert_eq!(w_3, 4.5);

        // Cleanup
        let _ = fs::remove_file(TEST_SNAPSHOT);
    }

    #[test]
    fn test_all_30_spec_v2_4_test_vectors() {
        let spec_dir = std::path::Path::new("/Users/jesse/impulse/impulse-graph-spec/test-vectors");
        assert!(spec_dir.exists(), "Test vectors directory must exist");

        let entries = fs::read_dir(spec_dir).unwrap();
        let mut count = 0;
        for entry in entries {
            let entry = entry.unwrap();
            let path = entry.path();
            if path.is_dir() {
                let imps_file = path.join("snapshot.imps");
                let manifest_file = path.join("manifest.json");
                if imps_file.exists() && manifest_file.exists() {
                    let folder_name = path.file_name().unwrap().to_str().unwrap();
                    let manifest_content = fs::read_to_string(&manifest_file).unwrap();
                    let is_rejection = manifest_content.contains("\"REJECT_") || manifest_content.contains("\"corrupt_") || !manifest_content.contains("\"SUCCESS\"");

                    if is_rejection {
                        let result = SnapshotReader::open(&imps_file).and_then(|r| {
                            for idx in 0..r.relation_count() {
                                r.get_row_offsets(idx)?;
                                r.get_col_indices(idx)?;
                            }
                            Ok(())
                        });
                        assert!(result.is_err(), "Vector {} should be REJECTED", folder_name);
                    } else {
                        let result = SnapshotReader::open(&imps_file);
                        assert!(result.is_ok(), "Vector {} should LOAD cleanly", folder_name);
                        let reader = result.unwrap();
                        assert_eq!(reader.header().magic(), IMPULSE_MAGIC);
                        assert_eq!(reader.header().version(), IMPULSE_VERSION_PACKED);
                        for idx in 0..reader.relation_count() {
                            assert!(reader.get_row_offsets(idx).is_ok(), "Vector {} failed get_row_offsets for relation {}", folder_name, idx);
                            assert!(reader.get_col_indices(idx).is_ok(), "Vector {} failed get_col_indices for relation {}", folder_name, idx);
                        }
                    }
                    count += 1;
                }
            }
        }
        assert!(count >= 30, "Should test at least 30 test vector folders, found {}", count);
    }

    #[test]
    fn test_custom_metadata_roundtrip_and_utf8_validation() {
        const TEST_SNAPSHOT: &str = "__test_metadata_roundtrip.imps";
        let mut writer = SnapshotWriter::new(TEST_SNAPSHOT);

        writer.add_domain(0, KeyType::Int32, "users", 2);
        writer.add_relation(0, 0, EncodingType::RawUint32, 2, 2, vec![0, 1, 2], vec![1, 0]);

        let mut hdr_meta = std::collections::HashMap::new();
        hdr_meta.insert("tenant_id".to_string(), "tenant_123".to_string());
        hdr_meta.insert("environment".to_string(), "production".to_string());
        assert!(writer.set_header_metadata(hdr_meta.clone()).is_ok());

        let mut ext_meta = std::collections::HashMap::new();
        ext_meta.insert("schema_definition".to_string(), "{\"nodes\": [\"user\"]}".to_string());
        assert!(writer.set_extended_metadata(ext_meta.clone()).is_ok());

        assert!(writer.finalize().is_ok());

        let reader = SnapshotReader::open(TEST_SNAPSHOT).unwrap();
        let read_hdr = reader.header_metadata().unwrap();
        assert_eq!(read_hdr.get("tenant_id").unwrap(), "tenant_123");
        assert_eq!(read_hdr.get("environment").unwrap(), "production");

        let read_ext = reader.extended_metadata().unwrap();
        assert_eq!(read_ext.get("schema_definition").unwrap(), "{\"nodes\": [\"user\"]}");

        let _ = fs::remove_file(TEST_SNAPSHOT);
    }
}
