// Impulse Graph C-ABI Binary Snapshot Format Specification v0.9.0 Header
#ifndef IMPULSE_GRAPH_FORMAT_V0_9_H
#define IMPULSE_GRAPH_FORMAT_V0_9_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IMPULSE_SPEC_VERSION_MAJOR 0
#define IMPULSE_SPEC_VERSION_MINOR 9
#define IMPULSE_SPEC_VERSION_PACKED 9
#define IMPULSE_SPEC_MAGIC 0x494D5053
#define IMPULSE_DEFAULT_DATA_OFFSET 4096

// Alignment Macros
#define IMPULSE_ALIGN_128(off) (((uint64_t)(off) + 127ULL) & ~127ULL)
#define IMPULSE_ALIGN_4K(off)  (((uint64_t)(off) + 4095ULL) & ~4095ULL)

// Attribute Nullability Flag & Base Type Mask
#define IMPULSE_TYPE_MASK 0x7F
#define IMPULSE_NULLABLE_FLAG 0x80

#pragma pack(push, 1)

// Section 1 Header Page 0 (4096 Bytes Baseline)
typedef struct impulse_snapshot_header_v0_9_t {
    uint32_t magic;                    // 0x00..0x03 ("IMPS" = 0x494D5053)
    uint16_t version;                  // 0x04..0x05 (0x0009)
    uint32_t data_offset;              // 0x06..0x09 (4096)
    uint16_t domain_count;             // 0x0A..0x0B
    uint16_t relation_count;           // 0x0C..0x0D
    uint64_t timestamp_ms;             // 0x0E..0x15
    uint64_t required_features;        // 0x16..0x1D
    uint64_t footer_directory_offset;  // 0x1E..0x25
    uint64_t footer_directory_bytes;   // 0x26..0x2D
    uint8_t  snapshot_uuid[16];        // 0x2E..0x3D
    uint16_t header_checksum;          // 0x3E..0x3F (CRC-16-CCITT)
    uint8_t  header_padding[4032];     // 0x40..0xFFF (Pads to 4096 bytes)
} impulse_snapshot_header_v0_9_t;

#ifdef __cplusplus
static_assert(sizeof(impulse_snapshot_header_v0_9_t) == 4096,
              "impulse_snapshot_header_v0_9_t size mismatch with spec v0.9.0");
#endif

// 16-Byte Footer Trailer (EOF - 16)
typedef struct impulse_footer_trailer_v0_9_t {
    uint64_t footer_length;            // 0x00..0x07 (Byte size of Footer Block)
    uint32_t spec_version;             // 0x08..0x0B (0x0009)
    uint32_t footer_magic;             // 0x0C..0x0F ("IMPS" = 0x494D5053)
} impulse_footer_trailer_v0_9_t;

#ifdef __cplusplus
static_assert(sizeof(impulse_footer_trailer_v0_9_t) == 16,
              "impulse_footer_trailer_v0_9_t size mismatch with spec v0.9.0");
#endif

// Domain Catalog Header (6 Bytes)
typedef struct impulse_domain_catalog_entry_header_v0_9_t {
    uint16_t domain_id;                // 0x00..0x01
    uint8_t  key_type;                 // 0x02
    uint8_t  reserved;                 // 0x03
    uint16_t name_len;                 // 0x04..0x05
} impulse_domain_catalog_entry_header_v0_9_t;

// Relation Directory Entry Descriptor (112 Bytes)
typedef struct impulse_relation_directory_entry_v0_9_t {
    uint16_t relation_id;              // 0x00..0x01
    uint16_t src_domain_id;            // 0x02..0x03
    uint16_t tgt_domain_id;            // 0x04..0x05
    uint8_t  encoding_id;               // 0x06
    uint8_t  node_id_width;             // 0x07 (2, 4, 8)
    uint8_t  edge_index_width;          // 0x08 (4, 8)
    uint8_t  reserved1[7];              // 0x09..0x0F
    uint64_t node_count;               // 0x10..0x17
    uint64_t edge_count;               // 0x18..0x1F
    uint64_t section_features;         // 0x20..0x27
    uint64_t csr_row_off_offset;       // 0x28..0x2F
    uint64_t csr_row_off_bytes;        // 0x30..0x37
    uint64_t csr_col_idx_offset;       // 0x38..0x3F
    uint64_t csr_col_idx_bytes;        // 0x40..0x47
    uint64_t csc_row_off_offset;       // 0x48..0x4F
    uint64_t csc_row_off_bytes;        // 0x50..0x57
    uint64_t csc_col_idx_offset;       // 0x58..0x5F
    uint64_t csc_col_idx_bytes;        // 0x60..0x67
    uint16_t attr_count;               // 0x68..0x69
    uint8_t  reserved2[6];              // 0x6A..0x6F (Pads to 112 bytes)
} impulse_relation_directory_entry_v0_9_t;

#ifdef __cplusplus
static_assert(sizeof(impulse_relation_directory_entry_v0_9_t) == 112,
              "impulse_relation_directory_entry_v0_9_t size mismatch with spec v0.9.0");
#endif

// Attribute Descriptor Entry (40 Bytes)
typedef struct impulse_attribute_descriptor_v0_9_t {
    uint16_t name_len;                 // 0x00..0x01
    uint8_t  type_code;                // 0x02 (Base type + 0x80 Nullable flag)
    uint8_t  reserved;                 // 0x03
    uint32_t dimension;                // 0x04..0x07 (1 for scalar, D for vector)
    uint64_t data_offset;              // 0x08..0x0F
    uint64_t data_bytes;               // 0x10..0x17
    uint64_t offsets_offset;           // 0x18..0x1F (For VAR_STRING / VAR_BYTES)
    uint64_t offsets_bytes;            // 0x20..0x27 (For VAR_STRING / VAR_BYTES)
} impulse_attribute_descriptor_v0_9_t;

#ifdef __cplusplus
static_assert(sizeof(impulse_attribute_descriptor_v0_9_t) == 40,
              "impulse_attribute_descriptor_v0_9_t size mismatch with spec v0.9.0");
#endif

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif // IMPULSE_GRAPH_FORMAT_V0_9_H
