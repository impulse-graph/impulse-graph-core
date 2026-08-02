#ifndef IMPULSE_GRAPH_H
#define IMPULSE_GRAPH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Little-Endian Magic Constant: 'I' 'M' 'P' 'S' (0x494D5053)
#define IMPULSE_MAGIC 0x494D5053
#define IMPULSE_VERSION_MAJOR 2
#define IMPULSE_VERSION_MINOR 4
#define IMPULSE_DEFAULT_DATA_OFFSET 4096

// Domain Catalog Key Type Enums
typedef enum impulse_key_type {
    IMPULSE_KEY_TYPE_INT16  = 0x00,
    IMPULSE_KEY_TYPE_INT32  = 0x01,
    IMPULSE_KEY_TYPE_INT64  = 0x02,
    IMPULSE_KEY_TYPE_UUID   = 0x03,
    IMPULSE_KEY_TYPE_STRING = 0x04
} impulse_key_type_t;

// Primary Topology Encoding Enums (EncodingType uint8)
typedef enum impulse_encoding_type {
    IMPULSE_ENC_RAW_UINT32     = 0x00,
    IMPULSE_ENC_DELTA_VBYTE    = 0x01,
    IMPULSE_ENC_RAW_UINT16     = 0x02,
    IMPULSE_ENC_HYBRID_16_32   = 0x03,
    IMPULSE_ENC_SIMDCOMP       = 0x04,
    IMPULSE_ENC_SLICED_ELLPACK = 0x05,
    IMPULSE_ENC_TPU_BCOO       = 0x06,
    IMPULSE_ENC_RAW_UINT64     = 0x07,
    IMPULSE_ENC_ROARING_BITMAP = 0x08
} impulse_encoding_type_t;

// C-ABI Status / Error Codes
typedef enum impulse_status {
    IMPULSE_OK                             = 0,
    IMPULSE_ERR_INVALID_MAGIC              = 1,
    IMPULSE_ERR_UNSUPPORTED_VERSION        = 2,
    IMPULSE_ERR_UNSUPPORTED_GLOBAL_FEATURE = 3,
    IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE= 4,
    IMPULSE_ERR_CORRUPT_CHECKSUM           = 5,
    IMPULSE_ERR_IO_FAILURE                 = 6,
    IMPULSE_ERR_INVALID_ARGUMENT           = 7,
    IMPULSE_ERR_SIGNATURE_MISMATCH         = 8,
    IMPULSE_ERR_BUFFER_OVERFLOW            = 9
} impulse_status_t;

// Global Feature Flags (Header Offset 0x40..0x47)
#define IMPULSE_GLOBAL_FEAT_64BIT_NODES        (1ULL << 0)
#define IMPULSE_GLOBAL_FEAT_ZSTD_DICT_EMBEDDED (1ULL << 1)
#define IMPULSE_GLOBAL_FEAT_DELTA_LOG_PRESENT  (1ULL << 2)
#define IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED   (1ULL << 3)
#define IMPULSE_GLOBAL_FEAT_CRYPTO_SIGNED      (1ULL << 4)

// Signature Algorithm Enum
typedef enum impulse_sig_algorithm {
    IMPULSE_SIG_ALG_NONE = 0,
    IMPULSE_SIG_ALG_ED25519 = 1,
    IMPULSE_SIG_ALG_ECDSA_P256 = 2,
    IMPULSE_SIG_ALG_RSA4096 = 3
} impulse_sig_algorithm_t;

// Signature Flags Bitmask
#define IMPULSE_SIG_FLAG_ENFORCED        (1ULL << 0)
#define IMPULSE_SIG_FLAG_KEY_EMBEDDED    (1ULL << 1)
#define IMPULSE_SIG_FLAG_KEY_FINGERPRINT (1ULL << 2)

// Cryptographic Signature Block (1024 bytes)
typedef struct impulse_snapshot_signature_block {
    uint16_t sig_algorithm;   // Algorithm enum
    uint16_t sig_bytes;       // Length of signature payload
    uint16_t pubkey_bytes;    // Length of embedded public key
    uint16_t sig_flags;       // Bitmask flags
    uint8_t  key_fingerprint[32]; // SHA-256 fingerprint / KMS ID
    uint8_t  signature[64];   // Signature payload (max size for Ed25519)
    uint8_t  public_key[32];  // Embedded public key (for Ed25519)
    uint8_t  reserved[888];   // Reserved for future use / PQC signatures
} impulse_snapshot_signature_block_t;

// Section 3 CSR Relation Topology Encoding Bitmask Flags (Bits 0..8)
// Generator Rule: Must match 1ULL << EncodingType
#define IMPULSE_RELATION_FEAT_ENC_RAW_UINT32     (1ULL << 0)
#define IMPULSE_RELATION_FEAT_ENC_DELTA_VBYTE    (1ULL << 1)
#define IMPULSE_RELATION_FEAT_ENC_RAW_UINT16     (1ULL << 2)
#define IMPULSE_RELATION_FEAT_ENC_HYBRID_16_32   (1ULL << 3)
#define IMPULSE_RELATION_FEAT_ENC_SIMDCOMP       (1ULL << 4)
#define IMPULSE_RELATION_FEAT_ENC_SLICED_ELLPACK (1ULL << 5)
#define IMPULSE_RELATION_FEAT_ENC_TPU_BCOO       (1ULL << 6)
#define IMPULSE_RELATION_FEAT_ENC_RAW_UINT64     (1ULL << 7)
#define IMPULSE_RELATION_FEAT_ENC_ROARING_BITMAP (1ULL << 8)

// Section 3 CSR Relation Features & Annotations (Bits 16..31)
#define IMPULSE_RELATION_FEAT_WEIGHTED_EDGES       (1ULL << 16)
#define IMPULSE_RELATION_FEAT_KV_LABELS            (1ULL << 17)
#define IMPULSE_RELATION_FEAT_DTO_EDGE_ANNOTATIONS (1ULL << 18)
#define IMPULSE_RELATION_FEAT_TEMPORAL_TIMESTAMPS  (1ULL << 19)
#define IMPULSE_RELATION_FEAT_PER_SECTION_ZSTD     (1ULL << 20)
#define IMPULSE_RELATION_FEAT_INCOMING_CSR_INDEX   (1ULL << 21)

// Section 4 ID Mapping Feature Flags (Bits 0..7)
#define IMPULSE_MAPPING_FEAT_ZSTD_COMPRESSION    (1ULL << 0)
#define IMPULSE_MAPPING_FEAT_HUFFMAN_PREFIX     (1ULL << 1)
#define IMPULSE_MAPPING_FEAT_UUID128_BINARY      (1ULL << 2)

// Explicit Binary Format Struct Layouts (#pragma pack(push, 1))
#pragma pack(push, 1)

// Section 1: Snapshot Header (4096 Bytes, DataOffset = 4096)
typedef struct impulse_snapshot_header {
    uint32_t magic;                    // 0x00..0x03: "IMPS" Little-Endian (0x494D5053)
    uint16_t version;                  // 0x04..0x05: Version 2 (0x0002)
    uint32_t data_offset;              // 0x06..0x09: Byte offset to Section 2 (4096)
    uint16_t domain_count;             // 0x0A..0x0B: Total number of domains
    uint16_t relation_count;           // 0x0C..0x0D: Total number of relations
    uint64_t kafka_offset;             // 0x0E..0x15: Kafka WAL offset
    uint64_t timestamp_ms;             // 0x16..0x1D: Unix timestamp (ms)
    uint8_t  sha256_checksum[32];      // 0x1E..0x3D: SHA256 payload checksum
    uint8_t  reserved[2];              // 0x3E..0x3F: Alignment padding
    uint64_t global_required_features; // 0x40..0x47: Global Feature-in-Use Bitmask
    impulse_snapshot_signature_block_t sig_block; // 0x48..0x447: Cryptographic Signature Block
    uint8_t  header_padding[3000];     // 0x448..0x0FFF: Padding to enforce 4KB alignment
} impulse_snapshot_header_t;

// Section 2 Part A: Domain Catalog Record Fixed Header
typedef struct impulse_domain_catalog_entry_header {
    uint16_t domain_id; // Domain identifier (0..N-1)
    uint8_t  key_type;  // Domain Key Type enum (impulse_key_type_t)
    uint16_t name_len;  // Length of domain name string
} impulse_domain_catalog_entry_header_t;

// Section 2 Part B: Relation Directory Entry Descriptor (121 Bytes Packed)
typedef struct impulse_relation_directory_entry {
    uint16_t src_domain_id;    // Source node domain ID
    uint16_t tgt_domain_id;    // Target node domain ID
    uint8_t  encoding_type;    // Primary topology encoding enum (impulse_encoding_type_t)
    uint64_t node_count;       // Source node count (N)
    uint64_t edge_count;       // Directed edge count (E)
    uint64_t section_features; // Per-Section Feature-in-Use Bitmask
    uint64_t csr_row_off_offset;// File offset to RowOffsets array
    uint64_t csr_row_off_bytes; // Size of RowOffsets array in bytes
    uint64_t csr_col_idx_offset;// File offset to ColumnIndices stream
    uint64_t csr_col_idx_bytes; // Size of ColumnIndices stream in bytes
    uint64_t id_map_offset;     // File offset to Section 4 ID Mappings (0 if omitted)
    uint64_t id_map_bytes;      // Size of Section 4 ID Mappings (0 if omitted)
    uint64_t dto_lookup_offset; // File offset to Section 5 DTO Data (0 if omitted)
    uint64_t dto_lookup_bytes;  // Size of Section 5 DTO Data (0 if omitted)
    uint64_t delta_log_offset;  // File offset to Section 6 Delta Log (0 if omitted)
    uint64_t delta_log_bytes;   // Size of Section 6 Delta Log (0 if omitted)
} impulse_relation_directory_entry_t;

#pragma pack(pop)

// Compiler Static Assertions to Enforce ABI Binary Stability
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(impulse_snapshot_header_t) == 4096, "impulse_snapshot_header_t size must be 4096 bytes");
_Static_assert(sizeof(impulse_domain_catalog_entry_header_t) == 5, "impulse_domain_catalog_entry_header_t size must be 5 bytes");
_Static_assert(sizeof(impulse_relation_directory_entry_t) == 109, "impulse_relation_directory_entry_t size must be 109 bytes");
#elif defined(__cplusplus) && __cplusplus >= 201103L
static_assert(sizeof(impulse_snapshot_header_t) == 4096, "impulse_snapshot_header_t size must be 4096 bytes");
static_assert(sizeof(impulse_domain_catalog_entry_header_t) == 5, "impulse_domain_catalog_entry_header_t size must be 5 bytes");
static_assert(sizeof(impulse_relation_directory_entry_t) == 109, "impulse_relation_directory_entry_t size must be 109 bytes");
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(IMPULSE_BUILDING_DLL)
    #define IMPULSE_API __declspec(dllexport)
  #elif defined(IMPULSE_USING_DLL)
    #define IMPULSE_API __declspec(dllimport)
  #else
    #define IMPULSE_API
  #endif
#else
  #if defined(__GNUC__) && __GNUC__ >= 4
    #define IMPULSE_API __attribute__ ((visibility ("default")))
  #else
    #define IMPULSE_API
  #endif
#endif

// Opaque Handle Definitions
typedef struct impulse_snapshot impulse_snapshot_t;
typedef struct impulse_graph impulse_graph_t;
typedef struct impulse_query impulse_query_t;
typedef struct impulse_writer impulse_writer_t;

// Snapshot Reader C-ABI API
IMPULSE_API impulse_snapshot_t* impulse_snapshot_open(const char* file_path, impulse_status_t* out_status);
IMPULSE_API void impulse_snapshot_close(impulse_snapshot_t* snapshot);
IMPULSE_API uint32_t impulse_snapshot_magic(const impulse_snapshot_t* snapshot);
IMPULSE_API uint16_t impulse_snapshot_version(const impulse_snapshot_t* snapshot);
IMPULSE_API uint16_t impulse_snapshot_domain_count(const impulse_snapshot_t* snapshot);
IMPULSE_API uint16_t impulse_snapshot_relation_count(const impulse_snapshot_t* snapshot);
IMPULSE_API impulse_status_t impulse_snapshot_get_relation_entry(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    impulse_relation_directory_entry_t* out_entry
);
IMPULSE_API bool impulse_snapshot_is_reachable(
    const impulse_snapshot_t* snapshot,
    uint16_t src_domain, uint32_t src_id,
    uint16_t tgt_domain, uint32_t tgt_id
);
IMPULSE_API const char* impulse_get_last_error(void);

// Snapshot Generator / Writer C-ABI API (For Third-Party Tooling)
IMPULSE_API impulse_writer_t* impulse_writer_create(const char* output_file_path, uint64_t global_features);
IMPULSE_API impulse_status_t impulse_writer_add_domain(impulse_writer_t* writer, uint16_t domain_id, uint8_t key_type, const char* name);
IMPULSE_API impulse_status_t impulse_writer_add_relation(
    impulse_writer_t* writer,
    uint16_t src_domain_id,
    uint16_t tgt_domain_id,
    uint8_t encoding_type,
    uint64_t node_count,
    uint64_t edge_count,
    uint64_t section_features,
    const void* row_offsets_data, uint64_t row_offsets_bytes,
    const void* col_indices_data, uint64_t col_indices_bytes
);
IMPULSE_API impulse_status_t impulse_writer_finalize(impulse_writer_t* writer);
IMPULSE_API void impulse_writer_destroy(impulse_writer_t* writer);
IMPULSE_API impulse_status_t impulse_snapshot_sign_ed25519(const char* snapshot_path, const uint8_t secret_key[64], const uint8_t public_key[32], uint16_t sig_flags);
IMPULSE_API impulse_status_t impulse_snapshot_verify_ed25519(const impulse_snapshot_t* snapshot);


// Extended Snapshot Inspection & Zero-Copy Access
IMPULSE_API uint16_t impulse_snapshot_domain_count(const impulse_snapshot_t* snapshot);
IMPULSE_API uint16_t impulse_snapshot_relation_count(const impulse_snapshot_t* snapshot);
IMPULSE_API impulse_status_t impulse_snapshot_get_relation_entry(
    const impulse_snapshot_t* snapshot,
    uint16_t index,
    impulse_relation_directory_entry_t* out_entry
);
IMPULSE_API const void* impulse_snapshot_get_buffer(
    const impulse_snapshot_t* snapshot,
    uint64_t offset,
    uint64_t size
);

// High-performance Zero-Copy C-ABI Neighborhood Sampler
IMPULSE_API impulse_status_t impulse_snapshot_sample_neighbors(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    const uint32_t* src_nodes,
    size_t num_nodes,
    int k_samples,
    uint64_t seed,
    uint32_t* out_src,
    uint32_t* out_tgt,
    size_t out_capacity,
    size_t* out_count
);

#ifdef __cplusplus
}
#endif

#endif // IMPULSE_GRAPH_H
