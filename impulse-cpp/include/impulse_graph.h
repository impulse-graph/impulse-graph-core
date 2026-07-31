#ifndef IMPULSE_GRAPH_H
#define IMPULSE_GRAPH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IMPULSE_MAGIC 0x494D5053
#define IMPULSE_VERSION_MAJOR 2
#define IMPULSE_VERSION_MINOR 4
#define IMPULSE_DEFAULT_DATA_OFFSET 4096

// Global Feature Flags (Header Offset 0x40..0x47)
#define IMPULSE_GLOBAL_FEAT_64BIT_NODES        (1ULL << 0)
#define IMPULSE_GLOBAL_FEAT_ZSTD_DICT_EMBEDDED (1ULL << 1)
#define IMPULSE_GLOBAL_FEAT_DELTA_LOG_PRESENT  (1ULL << 2)
#define IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED   (1ULL << 3)

// Section 3 CSR Relation Topology Encodings (Bits 0..8)
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

typedef struct impulse_snapshot impulse_snapshot_t;
typedef struct impulse_graph impulse_graph_t;
typedef struct impulse_query impulse_query_t;

// Snapshot C-ABI API
impulse_snapshot_t* impulse_snapshot_open(const char* file_path);
void impulse_snapshot_close(impulse_snapshot_t* snapshot);
bool impulse_snapshot_is_reachable(
    const impulse_snapshot_t* snapshot,
    uint16_t src_domain, uint32_t src_id,
    uint16_t tgt_domain, uint32_t tgt_id
);

#ifdef __cplusplus
}
#endif

#endif // IMPULSE_GRAPH_H
