/**
 * @file impulse_index.h
 * @brief Secondary Non-Vector Index Construction & Query API for Impulse Graph Engine.
 */

#ifndef IMPULSE_INDEX_H
#define IMPULSE_INDEX_H

#include "impulse_graph.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =========================================================================
// INDEX 1: PERMUTATION INDEX (0x01 - IMP_INDEX_PERMUTATION)
// =========================================================================

IMPULSE_API impulse_status_t impulse_index_build_permutation(
    const void* attr_data,
    size_t element_count,
    uint8_t type_code,
    void** out_bytes,
    size_t* out_size
);

IMPULSE_API impulse_status_t impulse_index_permutation_range_query(
    const void* index_bytes,
    size_t index_size,
    double min_val,
    double max_val,
    const void* attr_data,
    uint32_t* out_node_ids,
    size_t max_nodes,
    size_t* out_count
);

// =========================================================================
// INDEX 2: 2-TIER MIN-MAX ZONE MAP (0x02 - IMP_INDEX_ZONE_MAP)
// =========================================================================

IMPULSE_API impulse_status_t impulse_index_build_zone_map(
    const void* attr_data,
    size_t element_count,
    uint8_t type_code,
    void** out_bytes,
    size_t* out_size
);

IMPULSE_API impulse_status_t impulse_index_zone_map_filter(
    const void* index_bytes,
    size_t index_size,
    double min_val,
    double max_val,
    uint64_t* out_page_bitmask,
    size_t max_words,
    size_t* out_eligible_pages
);

// =========================================================================
// INDEX 3: INVERTED BITSET INDEX (0x03 - IMP_INDEX_INVERTED_BITSET)
// =========================================================================

IMPULSE_API impulse_status_t impulse_index_build_inverted_bitset(
    const char** keys,
    size_t count,
    void** out_bytes,
    size_t* out_size
);

IMPULSE_API impulse_status_t impulse_index_inverted_bitset_lookup(
    const void* index_bytes,
    size_t index_size,
    const char* query_key,
    const uint64_t** out_words,
    size_t* out_num_words
);

// =========================================================================
// INDEX 4: MINIMAL PERFECT HASH INDEX (0x04 - IMP_INDEX_MINIMAL_PERFECT_HASH)
// =========================================================================

IMPULSE_API impulse_status_t impulse_index_build_minimal_perfect_hash(
    const char** keys,
    size_t count,
    void** out_bytes,
    size_t* out_size
);

IMPULSE_API impulse_status_t impulse_index_minimal_perfect_hash_lookup(
    const void* index_bytes,
    size_t index_size,
    const char* query_key,
    uint32_t* out_node_id
);

// =========================================================================
// INDEX 5: TRIGRAM 3-GRAM INDEX (0x05 - IMP_INDEX_TRIGRAM_3GRAM)
// =========================================================================

IMPULSE_API impulse_status_t impulse_index_build_trigram(
    const char** keys,
    size_t count,
    void** out_bytes,
    size_t* out_size
);

IMPULSE_API impulse_status_t impulse_index_trigram_search(
    const void* index_bytes,
    size_t index_size,
    const char* query_str,
    uint64_t* out_words,
    size_t max_words,
    size_t* out_num_words
);

// =========================================================================
// INDEX 6: DOMAIN-SPLIT BITSET INDEX (0x06 - IMP_INDEX_DOMAIN_SPLIT_BITSET)
// =========================================================================

IMPULSE_API impulse_status_t impulse_index_build_domain_split(
    const char** keys,
    size_t count,
    void** out_bytes,
    size_t* out_size
);

IMPULSE_API impulse_status_t impulse_index_domain_split_lookup(
    const void* index_bytes,
    size_t index_size,
    const char* domain_str,
    const uint64_t** out_words,
    size_t* out_num_words
);

// =========================================================================
// INDEX 7: TEMPORAL INTERVAL INDEX (0x07 - IMP_INDEX_TEMPORAL_INTERVAL)
// =========================================================================

IMPULSE_API impulse_status_t impulse_index_build_temporal_interval(
    const void* interval_data,
    size_t count,
    uint8_t type_code,
    void** out_bytes,
    size_t* out_size
);

IMPULSE_API impulse_status_t impulse_index_temporal_interval_query(
    const void* index_bytes,
    size_t index_size,
    uint64_t target_time,
    uint32_t* out_node_ids,
    size_t max_nodes,
    size_t* out_count
);

/**
 * @brief Frees memory allocated by index builder functions.
 * @param ptr Pointer to index byte buffer.
 */
IMPULSE_API void impulse_index_free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif // IMPULSE_INDEX_H
