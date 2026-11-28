/**
 * @file impulse_graph.h
 * @brief Impulse Graph Engine Native C-ABI Kernel Specification v0.11.0.
 *
 * Provides high-performance zero-copy memory-mapped snapshot loading, graph reachability,
 * SIMD neighborhood sampling, live overlay delta compaction, and snapshot creation APIs.
 */

#ifndef IMPULSE_GRAPH_H
#define IMPULSE_GRAPH_H

#include "impulse_format_v0_11.h"
#include "impulse_format_v0_10.h"
#include "impulse_format_v0_9.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Little-Endian Magic Constant: 'I' 'M' 'P' 'S' (0x494D5053) */
#define IMPULSE_MAGIC 0x494D5053
#define IMPULSE_VERSION_MAJOR 0
#define IMPULSE_VERSION_MINOR 11
#define IMPULSE_VERSION_PACKED 11
#define IMPULSE_DEFAULT_DATA_OFFSET 4096

/** @brief Domain Catalog Key Type Definitions */
typedef enum impulse_key_type {
    IMPULSE_KEY_TYPE_INT8    = 0x01,
    IMPULSE_KEY_TYPE_INT16   = 0x02,
    IMPULSE_KEY_TYPE_INT32   = 0x03,
    IMPULSE_KEY_TYPE_INT64   = 0x04,
    IMPULSE_KEY_TYPE_UINT8   = 0x05,
    IMPULSE_KEY_TYPE_UINT16  = 0x06,
    IMPULSE_KEY_TYPE_UINT32  = 0x07,
    IMPULSE_KEY_TYPE_UINT64  = 0x08,
    IMPULSE_KEY_TYPE_FLOAT32 = 0x09,
    IMPULSE_KEY_TYPE_FLOAT64 = 0x0A,
    IMPULSE_KEY_TYPE_UUID    = 0x0A, // Legacy v0.9.0 alias
    IMPULSE_KEY_TYPE_STRING  = 0x0B,
    IMPULSE_KEY_TYPE_UUID128 = 0x0C
} impulse_key_type_t;

/** @brief Primary CSR Topology Encoding Enums */
typedef enum impulse_encoding_type {
    IMPULSE_ENC_RAW            = 0x00,
    IMPULSE_ENC_ZSTD           = 0x01
} impulse_encoding_type_t;

/** @brief C-ABI Return Status / Error Codes */
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

/** Global Feature Flags (Header Offset 0x16..0x1D) */
#ifndef IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED
#define IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED   (1ULL << 0)
#endif
#ifndef IMPULSE_GLOBAL_FEAT_CRYPTO_SIGNED
#define IMPULSE_GLOBAL_FEAT_CRYPTO_SIGNED      (1ULL << 1)
#endif
#ifndef IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG
#define IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG     (1ULL << 2)
#endif

/** @brief Stream Writer Callback Signature */
typedef int32_t (*impulse_write_fn)(const void* data, size_t bytes, void* user_data);

typedef impulse_snapshot_header_v0_11_t impulse_snapshot_header_t;
typedef impulse_footer_trailer_v0_11_t impulse_footer_trailer_t;
typedef impulse_domain_catalog_entry_v0_11_t impulse_domain_catalog_entry_t;
typedef impulse_relation_directory_entry_v0_11_t impulse_relation_directory_entry_t;
typedef impulse_attribute_descriptor_v0_11_t impulse_attribute_descriptor_t;
typedef impulse_index_directory_entry_v0_11_t impulse_index_directory_entry_t;

/**
 * @brief Naturally aligned summary of active Page 0 snapshot header fields
 *        (omits the 4030-byte reserved page padding of impulse_snapshot_header_t).
 */
typedef struct impulse_snapshot_header_info {
    uint32_t magic;                   // Magic constant (0x494D5053 = 'IMPS')
    uint32_t data_offset;             // Byte offset where Section 2 payload begins (4096)
    uint64_t timestamp_ms;            // Unix epoch timestamp (milliseconds)
    uint64_t required_features;       // Global feature flags bitmask
    uint64_t footer_directory_offset; // Absolute file offset to Footer Directory Table (0 if Page 1)
    uint64_t footer_directory_bytes;  // Byte size of Footer Directory Table
    uint64_t file_size;               // Total mapped byte size of the snapshot file
    uint16_t version;                 // Protocol version (9=v0.9, 10=v0.10, 11=v0.11)
    uint16_t domain_count;            // Total number of domains in catalog
    uint16_t relation_count;          // Total number of relations in directory
    uint16_t index_count;             // Total number of secondary indices
    uint16_t header_checksum;         // CRC-16-CCITT checksum over bytes 0x00..0x3D
    uint8_t  reserved[6];             // Zero-filled alignment padding
    uint8_t  snapshot_uuid[16];       // 128-bit Binary UUID
} impulse_snapshot_header_info_t;

#ifdef __cplusplus
static_assert(sizeof(impulse_snapshot_header_info_t) == 80, "impulse_snapshot_header_info_t must be 80 bytes");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(impulse_snapshot_header_info_t) == 80, "impulse_snapshot_header_info_t must be 80 bytes");
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

/** Opaque handle to memory-mapped binary graph snapshot */
typedef struct impulse_snapshot impulse_snapshot_t;
/** Opaque handle to active graph container */
typedef struct impulse_graph impulse_graph_t;
/** Opaque handle to query pipeline */
typedef struct impulse_query impulse_query_t;
/** Struct for compiled CEL queries */
typedef struct impulse_compiled_query {
    size_t instruction_count;
    size_t bytecode_length;
    uint8_t* bytecode;
} impulse_compiled_query_t;
/** Opaque handle to streaming snapshot writer */
typedef struct impulse_writer impulse_writer_t;
/** Opaque handle to live mutation delta layer */
typedef struct impulse_delta_layer impulse_delta_layer_t;

struct impulse_vm_state;

IMPULSE_API int impulse_compile_cel(const char* cel_expr, int is_projection, impulse_compiled_query_t** out_query);
IMPULSE_API void impulse_compiled_query_destroy(impulse_compiled_query_t* query);
IMPULSE_API int impulse_fluent_project(impulse_query_t* query, const char* cel_expr);
IMPULSE_API int impulse_execute_compiled_query(const impulse_snapshot_t* snapshot, impulse_compiled_query_t* query, void* state, uint64_t input_seed);


/**
 * @brief Open a binary snapshot file via zero-copy OS memory-mapping (`mmap`).
 * @param file_path Absolute or relative filesystem path to `.imps` file.
 * @param out_status Pointer to status return code (`IMPULSE_OK` on success).
 * @return Handle to opened snapshot, or NULL on failure.
 */
IMPULSE_API impulse_snapshot_t* impulse_snapshot_open(const char* file_path, impulse_status_t* out_status);

/**
 * @brief Close an open snapshot handle and unmap physical memory.
 * @param snapshot Pointer to snapshot handle.
 */
IMPULSE_API void impulse_snapshot_close(impulse_snapshot_t* snapshot);

/**
 * @brief Get snapshot magic word.
 * @param snapshot Pointer to snapshot handle.
 * @return 32-bit magic integer (0x494D5053).
 */
IMPULSE_API uint32_t impulse_snapshot_magic(const impulse_snapshot_t* snapshot);

/**
 * @brief Get snapshot format version.
 * @param snapshot Pointer to snapshot handle.
 * @return Format version number (e.g. 11 for v0.11.0 spec).
 */
IMPULSE_API uint16_t impulse_snapshot_version(const impulse_snapshot_t* snapshot);

/**
 * @brief Inspect active Page 0 snapshot header metadata into a naturally aligned struct.
 * @param snapshot Pointer to snapshot handle.
 * @param out_header Required output struct to populate with active header fields and file_size.
 * @return IMPULSE_OK on success, or IMPULSE_ERR_INVALID_ARGUMENT if snapshot or out_header is NULL.
 */
IMPULSE_API impulse_status_t impulse_snapshot_get_header(
    const impulse_snapshot_t* snapshot,
    impulse_snapshot_header_info_t* out_header
);

/**
 * @brief Get total mapped byte size of the snapshot file.
 * @param snapshot Pointer to snapshot handle.
 * @return Snapshot file size in bytes, or 0 if null.
 */
IMPULSE_API uint64_t impulse_snapshot_file_size(const impulse_snapshot_t* snapshot);

/**
 * @brief Compute SHA-256 digest over the entire mapped snapshot file.
 * @param snapshot Pointer to snapshot handle.
 * @param out_digest Caller-provided 32-byte buffer to receive the raw SHA-256 digest.
 * @return IMPULSE_OK on success, or IMPULSE_ERR_INVALID_ARGUMENT if null.
 */
IMPULSE_API impulse_status_t impulse_snapshot_compute_sha256(
    const impulse_snapshot_t* snapshot,
    uint8_t out_digest[32]
);

/**
 * @brief Get number of defined domains in snapshot catalog.
 * @param snapshot Pointer to snapshot handle.
 * @return Count of domains.
 */
IMPULSE_API uint16_t impulse_snapshot_domain_count(const impulse_snapshot_t* snapshot);

/**
 * @brief Inspect domain catalog entry metadata by domain index.
 * @param snapshot Pointer to snapshot handle.
 * @param domain_index Index of domain entry (0..domain_count-1).
 * @param out_entry Output struct to populate with domain entry data.
 * @param out_name Optional pointer to receive null-terminated domain name string.
 * @return IMPULSE_OK on success, or error code on invalid index.
 */
IMPULSE_API impulse_status_t impulse_snapshot_get_domain_entry(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_index,
    impulse_domain_catalog_entry_t* out_entry,
    const char** out_name
);

/**
 * @brief Get number of relation directory entries in snapshot.
 * @param snapshot Pointer to snapshot handle.
 * @return Count of relation directories.
 */
IMPULSE_API uint16_t impulse_snapshot_relation_count(const impulse_snapshot_t* snapshot);

/**
 * @brief Inspect relation directory metadata by relation index.
 * @param snapshot Pointer to snapshot handle.
 * @param relation_index Index of relation entry (0..relation_count-1).
 * @param out_entry Output struct to populate with directory entry data.
 * @return IMPULSE_OK on success, or error code on invalid index.
 */
IMPULSE_API impulse_status_t impulse_snapshot_get_relation_entry(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    impulse_relation_directory_entry_t* out_entry
);

/**
 * @brief Retrieve direct constituent child relation IDs for a virtual super-relation
 *        (handles both inline <=10 and 0xFFFF overflow component lists).
 *        Note: Child IDs may themselves be nested virtual relations (up to depth 16,
 *        validated acyclic at open).
 * @param snapshot Pointer to snapshot handle.
 * @param relation_index Index of relation entry (0..relation_count-1).
 * @param out_component_ids Optional output buffer (pass NULL to query count only).
 * @param out_capacity Capacity of out_component_ids in uint16_t elements.
 * @param out_count Required output pointer receiving direct constituent child count (0 for non-virtual).
 * @return IMPULSE_OK on success, IMPULSE_ERR_BUFFER_OVERFLOW if out_component_ids != NULL and
 *         out_capacity < *out_count (in which case *out_count still receives the required count),
 *         or IMPULSE_ERR_INVALID_ARGUMENT on invalid arguments/index.
 */
IMPULSE_API impulse_status_t impulse_snapshot_get_relation_virtual_components(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    uint16_t* out_component_ids,
    uint16_t out_capacity,
    uint16_t* out_count
);

/**
 * @brief Test direct reachability between a source node and target node on a relation.
 * @param snapshot Pointer to snapshot handle.
 * @param relation_index Relation index to search.
 * @param src_id Source node index.
 * @param tgt_id Target node index.
 * @return True if directed edge exists, false otherwise.
 */
IMPULSE_API bool impulse_snapshot_is_reachable(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    uint64_t src_id,
    uint64_t tgt_id
);

/**
 * @brief Retrieve human-readable error description for last failure on thread.
 * @return Thread-local error string.
 */
IMPULSE_API const char* impulse_get_last_error(void);

// Snapshot Generator / Writer C-ABI API (For Third-Party Tooling)
IMPULSE_API impulse_writer_t* impulse_writer_create(const char* output_file_path, uint64_t global_features);
IMPULSE_API impulse_writer_t* impulse_writer_create_stream(impulse_write_fn write_cb, void* user_data, uint64_t global_features);
IMPULSE_API impulse_status_t impulse_writer_add_domain(impulse_writer_t* writer, uint16_t domain_id, uint8_t key_type, const char* name);
IMPULSE_API impulse_status_t impulse_writer_set_domain_node_count(impulse_writer_t* writer, uint16_t domain_index, uint64_t node_count);
IMPULSE_API impulse_status_t impulse_writer_set_domain_dense_id_width(impulse_writer_t* writer, uint16_t domain_index, uint8_t dense_id_width);
IMPULSE_API impulse_status_t impulse_writer_set_domain_key_attr(impulse_writer_t* writer, uint16_t domain_index, uint16_t key_attr_index);
IMPULSE_API impulse_status_t impulse_writer_add_domain_attribute(
    impulse_writer_t* writer,
    uint16_t domain_index,
    const char* name,
    uint8_t type_code,
    uint32_t dimension,
    const void* data, uint64_t data_bytes,
    const void* offsets, uint64_t offsets_bytes
);
IMPULSE_API impulse_status_t impulse_writer_add_domain_attribute_decimal64(
    impulse_writer_t* writer,
    uint16_t domain_index,
    const char* name,
    uint8_t type_code,
    uint32_t dimension,
    uint16_t precision,
    uint8_t scale,
    const void* data, uint64_t data_bytes
);
IMPULSE_API impulse_status_t impulse_writer_add_relation(
    impulse_writer_t* writer,
    uint16_t src_domain_id,
    uint16_t tgt_domain_id,
    uint8_t encoding_id,
    uint64_t node_count,
    uint64_t edge_count,
    uint64_t section_features,
    const void* row_offsets_data, uint64_t row_offsets_bytes,
    const void* col_indices_data, uint64_t col_indices_bytes
);
IMPULSE_API impulse_status_t impulse_writer_add_relation_ex(
    impulse_writer_t* writer,
    uint16_t src_domain_id,
    uint16_t tgt_domain_id,
    uint8_t encoding_id,
    uint8_t node_id_width,
    uint8_t edge_index_width,
    uint64_t node_count,
    uint64_t edge_count,
    uint64_t section_features,
    const void* csr_row_offsets_data, uint64_t csr_row_offsets_bytes,
    const void* csr_col_indices_data, uint64_t csr_col_indices_bytes,
    const void* csc_row_offsets_data, uint64_t csc_row_offsets_bytes,
    const void* csc_col_indices_data, uint64_t csc_col_indices_bytes,
    const void* coo_src_data, uint64_t coo_src_bytes,
    const void* coo_tgt_data, uint64_t coo_tgt_bytes,
    const void* coo_edge_id_data, uint64_t coo_edge_id_bytes,
    uint8_t coo_order
);
IMPULSE_API impulse_status_t impulse_writer_add_attribute(
    impulse_writer_t* writer,
    uint16_t relation_index,
    const char* name,
    uint8_t type_code,
    uint32_t dimension,
    const void* data, uint64_t data_bytes,
    const void* offsets, uint64_t offsets_bytes
);
IMPULSE_API impulse_status_t impulse_writer_add_attribute_decimal64(
    impulse_writer_t* writer,
    uint16_t relation_index,
    const char* name,
    uint8_t type_code,
    uint32_t dimension,
    uint16_t precision,
    uint8_t scale,
    const void* data, uint64_t data_bytes
);
IMPULSE_API impulse_status_t impulse_writer_add_index(
    impulse_writer_t* writer,
    uint16_t domain_id,
    uint16_t relation_id,
    uint16_t attribute_index,
    uint8_t index_type,
    const char* index_name,
    const void* index_data, uint64_t index_bytes,
    uint64_t payload_feature_mask
);
IMPULSE_API impulse_status_t impulse_writer_set_metadata(impulse_writer_t* writer, const char* key, const char* value);
IMPULSE_API impulse_status_t impulse_writer_finalize(impulse_writer_t* writer);
IMPULSE_API void impulse_writer_destroy(impulse_writer_t* writer);

IMPULSE_API uint16_t impulse_snapshot_get_index_count(const impulse_snapshot_t* snapshot);

/**
 * @brief Inspect full 64-byte secondary index directory entry by index.
 * @param snapshot Pointer to snapshot handle.
 * @param index_idx Zero-based index (0..index_count-1).
 * @param out_entry Required output struct receiving the index directory descriptor.
 * @param out_name Optional output pointer receiving borrowed null-terminated UTF-8 index name
 *        (valid until impulse_snapshot_close; caller must not free).
 * @return IMPULSE_OK on success, or IMPULSE_ERR_INVALID_ARGUMENT on NULL args or OOB index.
 */
IMPULSE_API impulse_status_t impulse_snapshot_get_index_entry(
    const impulse_snapshot_t* snapshot,
    uint16_t index_idx,
    impulse_index_directory_entry_t* out_entry,
    const char** out_name
);
IMPULSE_API impulse_status_t impulse_snapshot_get_index(
    const impulse_snapshot_t* snapshot,
    uint16_t index_idx,
    uint32_t* index_id,
    uint16_t* domain_id,
    uint16_t* relation_id,
    uint16_t* attribute_index,
    uint8_t* index_type,
    const char** index_name,
    const void** index_data,
    uint64_t* index_bytes
);
// Key Catalog Resolution API (Section 4)
IMPULSE_API impulse_status_t impulse_snapshot_resolve_key(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_id,
    const void* key_bytes,
    size_t key_len,
    uint32_t* out_node_id
);

IMPULSE_API impulse_status_t impulse_snapshot_resolve_dense_id(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_id,
    uint32_t node_id,
    const void** out_key_bytes,
    size_t* out_key_len
);

// Live Overlay Delta Layer API (Thread-Safe Concurrent Edge Additions & Tombstones)
IMPULSE_API impulse_delta_layer_t* impulse_delta_layer_create(uint16_t src_domain_id, uint16_t tgt_domain_id, const char* relation_name);
IMPULSE_API impulse_status_t impulse_delta_layer_add_edge(impulse_delta_layer_t* delta, uint64_t src_node, uint64_t tgt_node);
IMPULSE_API impulse_status_t impulse_delta_layer_tombstone_edge(impulse_delta_layer_t* delta, uint64_t src_node, uint64_t tgt_node);
IMPULSE_API bool impulse_delta_layer_is_tombstoned(const impulse_delta_layer_t* delta, uint64_t src_node, uint64_t tgt_node);
IMPULSE_API void impulse_delta_layer_destroy(impulse_delta_layer_t* delta);

/**
 * @brief Compact base snapshot and live delta overlays into a new binary snapshot file on disk.
 * @param base_snapshot Base snapshot handle.
 * @param deltas Array of active delta layer pointers.
 * @param delta_count Number of delta layers.
 * @param output_file_path Target file path for output `.imps` snapshot.
 * @return IMPULSE_OK on success.
 */
IMPULSE_API impulse_status_t impulse_snapshot_compact_to_file(
    const impulse_snapshot_t* base_snapshot,
    impulse_delta_layer_t** deltas,
    size_t delta_count,
    const char* output_file_path
);

IMPULSE_API impulse_status_t impulse_snapshot_compact_to_stream(
    const impulse_snapshot_t* base_snapshot,
    impulse_delta_layer_t** deltas,
    size_t delta_count,
    impulse_write_fn write_cb,
    void* user_data
);

IMPULSE_API impulse_status_t impulse_snapshot_get_metadata(const impulse_snapshot_t* snapshot, const char* key, char* out_val, size_t out_capacity);

/**
 * @brief Get total number of custom UTF-8 key-value metadata entries stored in the snapshot footer.
 * @param snapshot Pointer to snapshot handle.
 * @return Number of metadata entries, or 0 if snapshot is NULL.
 */
IMPULSE_API uint32_t impulse_snapshot_get_metadata_count(const impulse_snapshot_t* snapshot);

/**
 * @brief Enumerate a custom footer metadata entry by index (sorted lexicographically by key).
 * @param snapshot Pointer to snapshot handle.
 * @param metadata_index Zero-based index (0..metadata_count-1).
 * @param out_key Optional output pointer receiving borrowed null-terminated UTF-8 key
 *        (valid until impulse_snapshot_close; caller must not free).
 * @param out_value Optional output pointer receiving borrowed null-terminated UTF-8 value
 *        (valid until impulse_snapshot_close; caller must not free).
 * @return IMPULSE_OK on success, or IMPULSE_ERR_INVALID_ARGUMENT if snapshot is NULL or index is OOB.
 */
IMPULSE_API impulse_status_t impulse_snapshot_get_metadata_entry(
    const impulse_snapshot_t* snapshot,
    uint32_t metadata_index,
    const char** out_key,
    const char** out_value
);

IMPULSE_API const void* impulse_snapshot_get_buffer(
    const impulse_snapshot_t* snapshot,
    uint64_t offset,
    uint64_t size
);

/**
 * @brief Zero-copy SIMD Neighborhood Sampler for GNN feature expansion andReBAC queries.
 * @param snapshot Snapshot handle.
 * @param relation_index Target relation index.
 * @param src_nodes Input array of seed node indices.
 * @param num_nodes Length of src_nodes array.
 * @param k_samples Maximum samples per seed node (-1 for full expansion).
 * @param seed Random seed for sampling.
 * @param out_src Output buffer for sampled edge source nodes.
 * @param out_tgt Output buffer for sampled edge target nodes.
 * @param out_capacity Capacity of out_src and out_tgt buffers.
 * @param out_count Output pointer to store total sampled edge count.
 * @return IMPULSE_OK on success.
 */
IMPULSE_API impulse_status_t impulse_snapshot_sample_neighbors(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    const uint64_t* src_nodes,
    size_t num_nodes,
    int k_samples,
    uint64_t seed,
    uint64_t* out_src,
    uint64_t* out_tgt,
    size_t out_capacity,
    size_t* out_count
);

IMPULSE_API uint64_t impulse_snapshot_max_node_count(const impulse_snapshot_t* snapshot);
IMPULSE_API impulse_status_t impulse_snapshot_get_relation_buffers(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    const uint32_t** out_offsets,
    const uint32_t** out_targets,
    uint64_t* out_node_count,
    uint64_t* out_edge_count
);
IMPULSE_API impulse_status_t impulse_snapshot_get_relation_raw_buffers(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    const void** out_offsets,
    const void** out_targets,
    uint64_t* out_node_count,
    uint64_t* out_edge_count,
    uint8_t* out_node_id_width,
    uint8_t* out_edge_index_width
);
IMPULSE_API impulse_status_t impulse_snapshot_get_relation_csc_buffers(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    const uint32_t** out_csc_offsets,
    const uint32_t** out_csc_targets,
    uint64_t* out_csc_row_count,
    uint64_t* out_csc_edge_count
);
IMPULSE_API impulse_status_t impulse_snapshot_get_relation_csc_raw_buffers(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    const void** out_csc_offsets,
    const void** out_csc_targets,
    uint64_t* out_csc_row_count,
    uint64_t* out_csc_edge_count,
    uint8_t* out_node_id_width,
    uint8_t* out_edge_index_width
);
IMPULSE_API impulse_status_t impulse_snapshot_get_attribute_buffers(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    uint16_t attribute_index,
    const void** out_data,
    uint64_t* out_data_bytes,
    const void** out_offsets,
    uint64_t* out_offsets_bytes,
    uint8_t* out_type_code,
    uint32_t* out_dimension
);
IMPULSE_API impulse_status_t impulse_snapshot_get_attribute_name(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    uint16_t attribute_index,
    const char** out_name
);
/**
 * @brief Inspect full 44-byte edge relation attribute descriptor and optional name.
 * @param snapshot Pointer to snapshot handle.
 * @param relation_index Zero-based relation index.
 * @param attribute_index Zero-based attribute index within the relation.
 * @param out_descriptor Required output struct receiving the 44-byte attribute descriptor.
 * @param out_name Optional output pointer receiving borrowed null-terminated UTF-8 name
 *        (valid until impulse_snapshot_close; caller must not free).
 * @return IMPULSE_OK on success, or IMPULSE_ERR_INVALID_ARGUMENT on NULL args or OOB index.
 */
IMPULSE_API impulse_status_t impulse_snapshot_get_attribute_descriptor(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    uint16_t attribute_index,
    impulse_attribute_descriptor_t* out_descriptor,
    const char** out_name
);
IMPULSE_API impulse_status_t impulse_snapshot_get_attribute_decimal64_meta(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    uint16_t attribute_index,
    uint16_t* out_precision,
    uint8_t* out_scale
);

// Node Domain Attribute Accessors (Spec v0.10.0)
IMPULSE_API uint16_t impulse_snapshot_get_domain_attribute_count(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_index
);
IMPULSE_API uint16_t impulse_snapshot_get_domain_key_attr_index(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_index
);
IMPULSE_API impulse_status_t impulse_snapshot_get_domain_attribute_buffers(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_index,
    uint16_t attribute_index,
    const void** out_data,
    uint64_t* out_data_bytes,
    const void** out_offsets,
    uint64_t* out_offsets_bytes,
    uint8_t* out_type_code,
    uint32_t* out_dimension
);
IMPULSE_API impulse_status_t impulse_snapshot_get_domain_attribute_name(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_index,
    uint16_t attribute_index,
    const char** out_name
);
/**
 * @brief Inspect full 44-byte node domain attribute descriptor and optional name.
 * @param snapshot Pointer to snapshot handle.
 * @param domain_index Zero-based domain index.
 * @param attribute_index Zero-based attribute index within the domain.
 * @param out_descriptor Required output struct receiving the 44-byte attribute descriptor.
 * @param out_name Optional output pointer receiving borrowed null-terminated UTF-8 name
 *        (valid until impulse_snapshot_close; caller must not free).
 * @return IMPULSE_OK on success, or IMPULSE_ERR_INVALID_ARGUMENT on NULL args or OOB index.
 */
IMPULSE_API impulse_status_t impulse_snapshot_get_domain_attribute_descriptor(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_index,
    uint16_t attribute_index,
    impulse_attribute_descriptor_t* out_descriptor,
    const char** out_name
);
IMPULSE_API impulse_status_t impulse_snapshot_get_domain_attribute_decimal64_meta(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_index,
    uint16_t attribute_index,
    uint16_t* out_precision,
    uint8_t* out_scale
);
IMPULSE_API impulse_status_t impulse_snapshot_get_relation_name(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    const char** out_name
);

/** @brief Architecture-Independent SIMD Target Name */
IMPULSE_API const char* impulse_simd_get_target_name(void);
IMPULSE_API float impulse_simd_dot_product_f32(const float* a, const float* b, size_t len);
IMPULSE_API impulse_status_t impulse_simd_vector_sum_f32(const float* a, const float* b, float* out, size_t len);
IMPULSE_API impulse_status_t impulse_simd_intersect_sorted_u32(
    const uint32_t* a, size_t len_a,
    const uint32_t* b, size_t len_b,
    uint32_t* out_intersection,
    size_t* out_count
);

#ifdef __cplusplus
}
#endif

#endif // IMPULSE_GRAPH_H
