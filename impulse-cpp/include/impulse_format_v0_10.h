// Generated from impulse-graph-spec/spec/v0.10.0.yaml (Spec v0.10.0).
#ifndef IMPULSE_GRAPH_FORMAT_V0_10_H
#define IMPULSE_GRAPH_FORMAT_V0_10_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IMPULSE_SPEC_VERSION_MAJOR_V0_10 0
#define IMPULSE_SPEC_VERSION_MINOR_V0_10 10
#define IMPULSE_SPEC_MAGIC_V0_10 0x494D5053
#define IMPULSE_SPEC_HEADER_BASELINE_OFFSET_V0_10 4096

#ifndef IMPULSE_SPEC_VERSION_PACKED
#define IMPULSE_SPEC_VERSION_PACKED 10
#define IMPULSE_SPEC_VERSION_MAJOR 0
#define IMPULSE_SPEC_VERSION_MINOR 10
#endif

#ifndef IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED
#define IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED 0x0000000000000001ULL
#endif
#ifndef IMPULSE_GLOBAL_FEAT_CRYPTO_SIGNED
#define IMPULSE_GLOBAL_FEAT_CRYPTO_SIGNED 0x0000000000000002ULL
#endif
#ifndef IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG
#define IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG 0x0000000000000004ULL
#endif

#ifndef IMPULSE_SECTION_FEAT_RAW_UINT32
#define IMPULSE_SECTION_FEAT_RAW_UINT32 0x0000000000000001ULL
#endif
#ifndef IMPULSE_SECTION_FEAT_DELTA_VBYTE
#define IMPULSE_SECTION_FEAT_DELTA_VBYTE 0x0000000000000002ULL
#endif
#ifndef IMPULSE_SECTION_FEAT_RAW_UINT16
#define IMPULSE_SECTION_FEAT_RAW_UINT16 0x0000000000000004ULL
#endif
#ifndef IMPULSE_SECTION_FEAT_SIMDCOMP
#define IMPULSE_SECTION_FEAT_SIMDCOMP 0x0000000000000010ULL
#endif

// Relation Directory SectionFeatures Bitmask (Spec v0.10.0 §2.4)
#ifndef IMP_REL_FEATURE_INCLUDE_CSC
#define IMP_REL_FEATURE_INCLUDE_CSC  0x0000000000000001ULL
#define IMP_REL_FEATURE_WEIGHTED     0x0000000000000002ULL
#define IMP_REL_FEATURE_VIRTUAL      0x0000000000000004ULL
#define IMP_REL_CARDINALITY_MASK     0x0000000000000018ULL
#define IMP_REL_CARDINALITY_M_N      0x0000000000000000ULL
#define IMP_REL_CARDINALITY_M_1      0x0000000000000008ULL
#define IMP_REL_CARDINALITY_1_M      0x0000000000000010ULL
#define IMP_REL_CARDINALITY_1_1      0x0000000000000018ULL
#endif

// C-ABI Status and Error Return Codes (v0.10.0)
typedef enum impulse_status_t_v0_10 {
    IMPULSE_OK_V0_10 = 0,
    IMPULSE_ERR_INVALID_MAGIC_V0_10 = 1,
    IMPULSE_ERR_UNSUPPORTED_VERSION_V0_10 = 2,
    IMPULSE_ERR_UNSUPPORTED_GLOBAL_FEATURE_V0_10 = 3,
    IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE_V0_10 = 4,
    IMPULSE_ERR_CORRUPT_CHECKSUM_V0_10 = 5,
    IMPULSE_ERR_IO_FAILURE_V0_10 = 6,
    IMPULSE_ERR_INVALID_ARGUMENT_V0_10 = 7,
    IMPULSE_ERR_SIGNATURE_MISMATCH_V0_10 = 8,
    IMPULSE_ERR_BUFFER_OVERFLOW_V0_10 = 9
} impulse_status_t_v0_10_t;

// Domain Catalog Key Type Enums (v0.10.0)
typedef enum impulse_key_type_t_v0_10 {
    IMPULSE_KEY_TYPE_INT8_V0_10 = 1,
    IMPULSE_KEY_TYPE_INT16_V0_10 = 2,
    IMPULSE_KEY_TYPE_INT32_V0_10 = 3,
    IMPULSE_KEY_TYPE_INT64_V0_10 = 4,
    IMPULSE_KEY_TYPE_UINT8_V0_10 = 5,
    IMPULSE_KEY_TYPE_UINT16_V0_10 = 6,
    IMPULSE_KEY_TYPE_UINT32_V0_10 = 7,
    IMPULSE_KEY_TYPE_UINT64_V0_10 = 8,
    IMPULSE_KEY_TYPE_FLOAT32_V0_10 = 9,
    IMPULSE_KEY_TYPE_FLOAT64_V0_10 = 10,
    IMPULSE_KEY_TYPE_VAR_STRING_V0_10 = 11,
    IMPULSE_KEY_TYPE_UUID128_V0_10 = 12
} impulse_key_type_t_v0_10_t;

// Primary Topology CSR Encoding Enums (v0.10.0)
typedef enum impulse_encoding_type_t_v0_10 {
    IMPULSE_ENC_RAW_UINT32_V0_10 = 0,
    IMPULSE_ENC_DELTA_VBYTE_V0_10 = 1,
    IMPULSE_ENC_RAW_UINT16_V0_10 = 2,
    IMPULSE_ENC_HYBRID_16_32_V0_10 = 3,
    IMPULSE_ENC_SIMDCOMP_V0_10 = 4,
    IMPULSE_ENC_SLICED_ELLPACK_V0_10 = 5,
    IMPULSE_ENC_TPU_BCOO_V0_10 = 6,
    IMPULSE_ENC_RAW_UINT64_V0_10 = 7,
    IMPULSE_ENC_ROARING_BITMAP_V0_10 = 8
} impulse_encoding_type_t_v0_10_t;

#pragma pack(push, 1)

// Section 1 Fixed 4KB Baseline Snapshot Header (Spec v0.10.0)
typedef struct impulse_snapshot_header_v0_10_t {
    uint32_t magic;                   // Magic constant (0x494D5053 = 'IMPS')
    uint16_t version;                 // Protocol version number (0x000A)
    uint32_t data_offset;             // Byte offset where Section 2 payload begins (4096)
    uint16_t domain_count;            // Total number of domains in catalog
    uint16_t relation_count;          // Total number of relations in matrix
    uint64_t timestamp_ms;            // Unix epoch timestamp (milliseconds)
    uint64_t required_features;       // Global feature flags bitmask
    uint64_t footer_directory_offset; // Absolute file offset to Footer Directory Table
    uint64_t footer_directory_bytes;  // Byte size of Footer Directory Table
    uint8_t snapshot_uuid[16];        // 128-bit Binary UUID
    uint16_t header_checksum;         // CRC-16-CCITT checksum over bytes 0x00..0x3D
    uint16_t index_count;             // Total number of secondary indices
    uint8_t header_padding[4030];     // Reserved header expansion padding
} impulse_snapshot_header_v0_10_t;

// Section 2.2 Domain Catalog Entry (32 Bytes, Spec v0.10.0)
typedef struct impulse_domain_catalog_entry_v0_10_t {
    uint16_t domain_id;               // Zero-indexed domain identifier
    uint8_t key_type;                 // Domain key primitive type enum
    uint8_t reserved1;                // Alignment padding (0x00)
    uint32_t name_offset;             // Offset into Shared String Table
    uint64_t node_count;              // Total node count in domain
    uint16_t attr_count;              // Number of domain attribute descriptors following this entry
    uint16_t key_attr_index;          // 0-indexed domain attribute index holding primary key column (0xFFFF if none)
    uint8_t reserved2[12];            // Reserved expansion padding (0x00)
} impulse_domain_catalog_entry_v0_10_t;

// Section 2.3 Relation Directory Entry (128 Bytes, Spec v0.10.0)
typedef struct impulse_relation_directory_entry_v0_10_t {
    uint16_t relation_id;             // Zero-indexed relation identifier
    uint16_t src_domain_id;           // Source domain ID
    uint16_t tgt_domain_id;           // Target domain ID
    uint8_t encoding_id;              // Topology encoding type enum
    uint8_t node_id_width;            // Node index width (bytes: 2, 4, or 8)
    uint8_t edge_index_width;         // Edge index width (bytes: 4 or 8)
    uint8_t reserved1[3];             // Reserved alignment
    uint32_t name_offset;             // Offset into Shared String Table
    uint64_t node_count;              // Source node count
    uint64_t edge_count;              // Edge count
    uint64_t section_features;        // Section feature bitmask
    uint64_t csr_row_off_offset;      // CSR row offsets file offset
    uint64_t csr_row_off_bytes;       // CSR row offsets byte size
    uint64_t csr_col_idx_offset;      // CSR col indices file offset
    uint64_t csr_col_idx_bytes;       // CSR col indices byte size
    uint64_t csc_row_off_offset;      // CSC row offsets file offset
    uint64_t csc_row_off_bytes;       // CSC row offsets byte size
    uint64_t csc_col_idx_offset;      // CSC col indices file offset
    uint64_t csc_col_idx_bytes;       // CSC col indices byte size
    uint16_t attr_count;              // Number of edge attribute descriptors following this entry
    uint8_t reserved2[22];            // Directory entry expansion padding
} impulse_relation_directory_entry_v0_10_t;

// Section 2.4 Node Domain & Edge Relation Attribute Descriptor Entry (44 Bytes, Spec v0.10.0)
typedef struct impulse_attribute_descriptor_v0_10_t {
    uint32_t name_offset;             // Offset into Shared String Table
    uint8_t type_code;                // Attribute primitive type enum (bits 0..6) + 0x80 nullable flag (bit 7)
    uint8_t reserved1;                // Alignment padding
    uint16_t reserved2;               // Alignment padding
    uint32_t dimension;               // Vector dimension (1 for scalar, 0 for variable-length)
    uint64_t data_offset;             // Absolute 128B-aligned file offset to attribute payload
    uint64_t data_bytes;              // Byte size of attribute payload
    uint64_t offsets_offset;          // Absolute 128B-aligned file offset to var-string offsets array
    uint64_t offsets_bytes;           // Byte size of var-string offsets array
} impulse_attribute_descriptor_v0_10_t;

// Section 2.6 Secondary Index Directory Entry (Fixed 64 Bytes POD, Spec v0.10.0)
typedef struct impulse_index_directory_entry_v0_10_t {
    uint32_t index_id;                // Zero-indexed secondary index identifier
    uint16_t domain_id;               // Target Node Domain ID
    uint16_t relation_id;             // Target Relation ID (0xFFFF for Node Domain attributes)
    uint16_t attribute_index;         // 0-indexed Attribute index within relation or domain
    uint8_t index_type;               // Generic Index Type enum (0x01..0x08)
    uint8_t reserved1;                // Alignment padding
    uint32_t name_offset;             // Offset into Shared String Table
    uint64_t data_offset;             // Absolute 128B-aligned file offset to index data
    uint64_t data_bytes;              // Total byte size of index data payload
    uint64_t payload_feature_mask;    // Generic Index Feature Bitmask
    uint8_t reserved_padding[24];     // Reserved expansion padding
} impulse_index_directory_entry_v0_10_t;

// 16-Byte Footer Trailer at EOF (Spec v0.10.0)
typedef struct impulse_footer_trailer_v0_10_t {
    uint64_t footer_length;           // Byte size of Footer Block
    uint32_t spec_version;            // Spec version (0x000A)
    uint32_t footer_magic;            // Footer magic (0x494D5053 = 'IMPS')
} impulse_footer_trailer_v0_10_t;

#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(impulse_snapshot_header_v0_10_t) == 4096, "v0.10.0 SnapshotHeader must be 4096 bytes");
static_assert(sizeof(impulse_domain_catalog_entry_v0_10_t) == 32, "v0.10.0 DomainCatalogEntry must be 32 bytes");
static_assert(sizeof(impulse_relation_directory_entry_v0_10_t) == 128, "v0.10.0 RelationDirectoryEntry must be 128 bytes");
static_assert(sizeof(impulse_attribute_descriptor_v0_10_t) == 44, "v0.10.0 AttributeDescriptor must be 44 bytes");
static_assert(sizeof(impulse_index_directory_entry_v0_10_t) == 64, "v0.10.0 IndexDirectoryEntry must be 64 bytes");
static_assert(sizeof(impulse_footer_trailer_v0_10_t) == 16, "v0.10.0 FooterTrailer must be 16 bytes");
#endif

#ifdef __cplusplus
}
#endif

#endif // IMPULSE_GRAPH_FORMAT_V0_10_H
