/**
 * @file impulse_index.cpp
 * @brief Full implementation of all 7 Secondary Non-Vector Indexes (0x01..0x07).
 */

#include "impulse_index.h"

#include <vector>
#include <numeric>
#include <algorithm>
#include <unordered_map>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cfloat>

#pragma pack(push, 1)
struct PermutationHeader {
    uint64_t count;
    uint8_t type_code;
    uint8_t reserved[7];
};

struct ZoneMapBlock {
    double min_val;
    double max_val;
};

struct ZoneMapHeader {
    uint64_t element_count;
    uint32_t page_size;
    uint32_t page_count;
    uint8_t type_code;
    uint8_t reserved[7];
};

struct InvertedBitsetHeader {
    uint32_t key_count;
    uint64_t words_per_bitset;
    uint32_t string_table_bytes;
    uint8_t reserved[16];
};

struct InvertedBitsetEntry {
    uint32_t key_string_offset;
    uint64_t bitset_offset;
    uint64_t bitset_bytes;
};

struct MphfHeader {
    uint64_t key_count;
    uint64_t seed;
    uint32_t string_table_bytes;
    uint8_t reserved[12];
};

struct MphfEntry {
    uint32_t key_string_offset;
    uint32_t node_id;
};

struct TrigramHeader {
    uint32_t trigram_count;
    uint64_t words_per_bitset;
    uint8_t reserved[16];
};

struct TrigramEntry {
    char trigram[4];
    uint64_t bitset_offset;
    uint64_t bitset_bytes;
};

struct TemporalIntervalHeader {
    uint64_t count;
    uint8_t type_code;
    uint8_t reserved[7];
};

struct TemporalIntervalEntry {
    uint64_t start_time;
    uint64_t end_time;
    uint32_t node_id;
};
#pragma pack(pop)

static inline uint64_t fnv1a_hash(const char* str, uint64_t seed) {
    uint64_t hash = 14695981039346656037ULL ^ seed;
    while (*str) {
        hash ^= static_cast<uint64_t>(static_cast<uint8_t>(*str++));
        hash *= 1099511628211ULL;
    }
    return hash;
}

extern "C" {

// =========================================================================
// INDEX 1: PERMUTATION INDEX
// =========================================================================

impulse_status_t impulse_index_build_permutation(
    const void* attr_data,
    size_t element_count,
    uint8_t type_code,
    void** out_bytes,
    size_t* out_size
) {
    if (!attr_data || element_count == 0 || !out_bytes || !out_size) return IMPULSE_ERR_INVALID_ARGUMENT;

    std::vector<uint32_t> perm(element_count);
    std::iota(perm.begin(), perm.end(), 0);

    uint8_t base_type = type_code & 0x7F;

    if (base_type == 0x03) {
        const int32_t* data = static_cast<const int32_t*>(attr_data);
        std::sort(perm.begin(), perm.end(), [data](uint32_t a, uint32_t b) { return data[a] < data[b]; });
    } else if (base_type == 0x04) {
        const int64_t* data = static_cast<const int64_t*>(attr_data);
        std::sort(perm.begin(), perm.end(), [data](uint32_t a, uint32_t b) { return data[a] < data[b]; });
    } else if (base_type == 0x06) {
        const float* data = static_cast<const float*>(attr_data);
        std::sort(perm.begin(), perm.end(), [data](uint32_t a, uint32_t b) { return data[a] < data[b]; });
    } else if (base_type == 0x07) {
        const double* data = static_cast<const double*>(attr_data);
        std::sort(perm.begin(), perm.end(), [data](uint32_t a, uint32_t b) { return data[a] < data[b]; });
    } else {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    size_t header_size = sizeof(PermutationHeader);
    size_t perm_size = element_count * sizeof(uint32_t);
    size_t total_size = header_size + perm_size;

    uint8_t* buf = static_cast<uint8_t*>(std::malloc(total_size));
    if (!buf) return IMPULSE_ERR_BUFFER_OVERFLOW;

    PermutationHeader hdr;
    hdr.count = element_count;
    hdr.type_code = type_code;
    std::memset(hdr.reserved, 0, sizeof(hdr.reserved));

    std::memcpy(buf, &hdr, header_size);
    std::memcpy(buf + header_size, perm.data(), perm_size);

    *out_bytes = buf;
    *out_size = total_size;
    return IMPULSE_OK;
}

impulse_status_t impulse_index_permutation_range_query(
    const void* index_bytes,
    size_t index_size,
    double min_val,
    double max_val,
    const void* attr_data,
    uint32_t* out_node_ids,
    size_t max_nodes,
    size_t* out_count
) {
    if (!index_bytes || index_size < sizeof(PermutationHeader) || !attr_data || !out_node_ids || !out_count) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    const uint8_t* raw = static_cast<const uint8_t*>(index_bytes);
    PermutationHeader hdr;
    std::memcpy(&hdr, raw, sizeof(hdr));

    if (index_size < sizeof(PermutationHeader) + hdr.count * sizeof(uint32_t)) return IMPULSE_ERR_BUFFER_OVERFLOW;

    const uint32_t* perm = reinterpret_cast<const uint32_t*>(raw + sizeof(PermutationHeader));
    uint8_t base_type = hdr.type_code & 0x7F;

    size_t matched = 0;

    if (base_type == 0x06) {
        const float* data = static_cast<const float*>(attr_data);
        float fmin = static_cast<float>(min_val);
        float fmax = static_cast<float>(max_val);

        auto low_it = std::lower_bound(perm, perm + hdr.count, fmin, [data](uint32_t idx, float val) { return data[idx] < val; });
        for (auto it = low_it; it != perm + hdr.count; ++it) {
            uint32_t node_id = *it;
            float val = data[node_id];
            if (val > fmax) break;
            if (matched < max_nodes) out_node_ids[matched] = node_id;
            matched++;
        }
    } else if (base_type == 0x03) {
        const int32_t* data = static_cast<const int32_t*>(attr_data);
        int32_t imin = static_cast<int32_t>(min_val);
        int32_t imax = static_cast<int32_t>(max_val);

        auto low_it = std::lower_bound(perm, perm + hdr.count, imin, [data](uint32_t idx, int32_t val) { return data[idx] < val; });
        for (auto it = low_it; it != perm + hdr.count; ++it) {
            uint32_t node_id = *it;
            int32_t val = data[node_id];
            if (val > imax) break;
            if (matched < max_nodes) out_node_ids[matched] = node_id;
            matched++;
        }
    } else {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    *out_count = matched;
    return IMPULSE_OK;
}

// =========================================================================
// INDEX 2: 2-TIER MIN-MAX ZONE MAP INDEX
// =========================================================================

impulse_status_t impulse_index_build_zone_map(
    const void* attr_data,
    size_t element_count,
    uint8_t type_code,
    void** out_bytes,
    size_t* out_size
) {
    if (!attr_data || element_count == 0 || !out_bytes || !out_size) return IMPULSE_ERR_INVALID_ARGUMENT;

    const uint32_t PAGE_SIZE = 1024;
    uint32_t page_count = static_cast<uint32_t>((element_count + PAGE_SIZE - 1) / PAGE_SIZE);

    std::vector<ZoneMapBlock> blocks(page_count);
    uint8_t base_type = type_code & 0x7F;

    for (uint32_t p = 0; p < page_count; ++p) {
        size_t start = p * PAGE_SIZE;
        size_t end = std::min(start + PAGE_SIZE, element_count);

        double min_val = DBL_MAX;
        double max_val = -DBL_MAX;

        for (size_t i = start; i < end; ++i) {
            double val = 0.0;
            if (base_type == 0x06) val = static_cast<const float*>(attr_data)[i];
            else if (base_type == 0x03) val = static_cast<const int32_t*>(attr_data)[i];
            else if (base_type == 0x07) val = static_cast<const double*>(attr_data)[i];
            else if (base_type == 0x04) val = static_cast<const int64_t*>(attr_data)[i];
            if (val < min_val) min_val = val;
            if (val > max_val) max_val = val;
        }

        blocks[p].min_val = min_val;
        blocks[p].max_val = max_val;
    }

    size_t header_size = sizeof(ZoneMapHeader);
    size_t blocks_size = page_count * sizeof(ZoneMapBlock);
    size_t total_size = header_size + blocks_size;

    uint8_t* buf = static_cast<uint8_t*>(std::malloc(total_size));
    if (!buf) return IMPULSE_ERR_BUFFER_OVERFLOW;

    ZoneMapHeader hdr;
    hdr.element_count = element_count;
    hdr.page_size = PAGE_SIZE;
    hdr.page_count = page_count;
    hdr.type_code = type_code;
    std::memset(hdr.reserved, 0, sizeof(hdr.reserved));

    std::memcpy(buf, &hdr, header_size);
    std::memcpy(buf + header_size, blocks.data(), blocks_size);

    *out_bytes = buf;
    *out_size = total_size;
    return IMPULSE_OK;
}

impulse_status_t impulse_index_zone_map_filter(
    const void* index_bytes,
    size_t index_size,
    double min_val,
    double max_val,
    uint64_t* out_page_bitmask,
    size_t max_words,
    size_t* out_eligible_pages
) {
    if (!index_bytes || index_size < sizeof(ZoneMapHeader) || !out_page_bitmask || !out_eligible_pages) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    const uint8_t* raw = static_cast<const uint8_t*>(index_bytes);
    ZoneMapHeader hdr;
    std::memcpy(&hdr, raw, sizeof(hdr));

    if (index_size < sizeof(ZoneMapHeader) + hdr.page_count * sizeof(ZoneMapBlock)) return IMPULSE_ERR_BUFFER_OVERFLOW;

    const ZoneMapBlock* blocks = reinterpret_cast<const ZoneMapBlock*>(raw + sizeof(ZoneMapHeader));
    size_t req_words = (hdr.page_count + 63) / 64;
    if (max_words < req_words) return IMPULSE_ERR_BUFFER_OVERFLOW;

    std::memset(out_page_bitmask, 0, req_words * sizeof(uint64_t));
    size_t eligible = 0;

    for (uint32_t p = 0; p < hdr.page_count; ++p) {
        if (blocks[p].max_val >= min_val && blocks[p].min_val <= max_val) {
            size_t w_idx = p / 64;
            size_t b_idx = p % 64;
            out_page_bitmask[w_idx] |= (1ULL << b_idx);
            eligible++;
        }
    }

    *out_eligible_pages = eligible;
    return IMPULSE_OK;
}

// =========================================================================
// INDEX 3: INVERTED BITSET INDEX
// =========================================================================

impulse_status_t impulse_index_build_inverted_bitset(
    const char** keys,
    size_t count,
    void** out_bytes,
    size_t* out_size
) {
    if (!keys || count == 0 || !out_bytes || !out_size) return IMPULSE_ERR_INVALID_ARGUMENT;

    size_t words_per_bitset = (count + 63) / 64;
    std::unordered_map<std::string, std::vector<uint64_t>> bitsets;

    for (size_t i = 0; i < count; ++i) {
        if (!keys[i]) continue;
        std::string k(keys[i]);
        if (bitsets.find(k) == bitsets.end()) {
            bitsets[k].resize(words_per_bitset, 0);
        }
        size_t w_idx = i / 64;
        size_t b_idx = i % 64;
        bitsets[k][w_idx] |= (1ULL << b_idx);
    }

    uint32_t key_count = static_cast<uint32_t>(bitsets.size());
    std::string string_pool = "\0";
    std::vector<InvertedBitsetEntry> entries;
    std::vector<std::vector<uint64_t>> payload_data;

    for (const auto& kv : bitsets) {
        InvertedBitsetEntry entry;
        entry.key_string_offset = static_cast<uint32_t>(string_pool.size());
        string_pool += kv.first;
        string_pool.push_back('\0');

        entry.bitset_bytes = words_per_bitset * sizeof(uint64_t);
        entries.push_back(entry);
        payload_data.push_back(kv.second);
    }

    size_t header_size = sizeof(InvertedBitsetHeader);
    size_t str_pool_bytes = string_pool.size();
    size_t entries_bytes = key_count * sizeof(InvertedBitsetEntry);
    size_t data_bytes_total = key_count * words_per_bitset * sizeof(uint64_t);

    size_t total_size = header_size + str_pool_bytes + entries_bytes + data_bytes_total;
    uint8_t* buf = static_cast<uint8_t*>(std::malloc(total_size));
    if (!buf) return IMPULSE_ERR_BUFFER_OVERFLOW;

    InvertedBitsetHeader hdr;
    hdr.key_count = key_count;
    hdr.words_per_bitset = words_per_bitset;
    hdr.string_table_bytes = static_cast<uint32_t>(str_pool_bytes);
    std::memset(hdr.reserved, 0, sizeof(hdr.reserved));

    size_t cur = 0;
    std::memcpy(buf + cur, &hdr, header_size);
    cur += header_size;

    std::memcpy(buf + cur, string_pool.data(), str_pool_bytes);
    cur += str_pool_bytes;

    size_t entries_offset = cur;
    cur += entries_bytes;

    for (size_t k = 0; k < key_count; ++k) {
        entries[k].bitset_offset = cur;
        std::memcpy(buf + cur, payload_data[k].data(), payload_data[k].size() * sizeof(uint64_t));
        cur += payload_data[k].size() * sizeof(uint64_t);
    }

    std::memcpy(buf + entries_offset, entries.data(), entries_bytes);

    *out_bytes = buf;
    *out_size = total_size;
    return IMPULSE_OK;
}

impulse_status_t impulse_index_inverted_bitset_lookup(
    const void* index_bytes,
    size_t index_size,
    const char* query_key,
    const uint64_t** out_words,
    size_t* out_num_words
) {
    if (!index_bytes || !query_key || !out_words || !out_num_words || index_size < sizeof(InvertedBitsetHeader)) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    const uint8_t* raw = static_cast<const uint8_t*>(index_bytes);
    InvertedBitsetHeader hdr;
    std::memcpy(&hdr, raw, sizeof(hdr));

    size_t str_offset = sizeof(InvertedBitsetHeader);
    size_t entries_offset = str_offset + hdr.string_table_bytes;

    if (index_size < entries_offset + hdr.key_count * sizeof(InvertedBitsetEntry)) return IMPULSE_ERR_BUFFER_OVERFLOW;

    const InvertedBitsetEntry* entries = reinterpret_cast<const InvertedBitsetEntry*>(raw + entries_offset);
    const char* str_pool = reinterpret_cast<const char*>(raw + str_offset);

    for (uint32_t k = 0; k < hdr.key_count; ++k) {
        const char* key_str = str_pool + entries[k].key_string_offset;
        if (std::strcmp(key_str, query_key) == 0) {
            if (entries[k].bitset_offset + entries[k].bitset_bytes > index_size) return IMPULSE_ERR_BUFFER_OVERFLOW;
            *out_words = reinterpret_cast<const uint64_t*>(raw + entries[k].bitset_offset);
            *out_num_words = hdr.words_per_bitset;
            return IMPULSE_OK;
        }
    }

    *out_words = nullptr;
    *out_num_words = 0;
    return IMPULSE_OK;
}

// =========================================================================
// INDEX 4: MINIMAL PERFECT HASH INDEX (MPHF)
// =========================================================================

impulse_status_t impulse_index_build_minimal_perfect_hash(
    const char** keys,
    size_t count,
    void** out_bytes,
    size_t* out_size
) {
    if (!keys || count == 0 || !out_bytes || !out_size) return IMPULSE_ERR_INVALID_ARGUMENT;

    uint64_t seed = 0x123456789ABCDEF0ULL;
    std::string string_pool = "\0";
    std::vector<MphfEntry> table(count * 2);
    std::vector<bool> occupied(count * 2, false);

    for (size_t i = 0; i < count; ++i) {
        if (!keys[i]) continue;
        uint32_t str_off = static_cast<uint32_t>(string_pool.size());
        string_pool += keys[i];
        string_pool.push_back('\0');

        uint64_t h = fnv1a_hash(keys[i], seed);
        size_t slot = h % (count * 2);

        while (occupied[slot]) {
            slot = (slot + 1) % (count * 2);
        }

        occupied[slot] = true;
        table[slot].key_string_offset = str_off;
        table[slot].node_id = static_cast<uint32_t>(i);
    }

    size_t header_size = sizeof(MphfHeader);
    size_t str_pool_bytes = string_pool.size();
    size_t table_bytes = table.size() * sizeof(MphfEntry);
    size_t total_size = header_size + str_pool_bytes + table_bytes;

    uint8_t* buf = static_cast<uint8_t*>(std::malloc(total_size));
    if (!buf) return IMPULSE_ERR_BUFFER_OVERFLOW;

    MphfHeader hdr;
    hdr.key_count = static_cast<uint64_t>(table.size());
    hdr.seed = seed;
    hdr.string_table_bytes = static_cast<uint32_t>(str_pool_bytes);
    std::memset(hdr.reserved, 0, sizeof(hdr.reserved));

    size_t cur = 0;
    std::memcpy(buf + cur, &hdr, header_size);
    cur += header_size;

    std::memcpy(buf + cur, string_pool.data(), str_pool_bytes);
    cur += str_pool_bytes;

    std::memcpy(buf + cur, table.data(), table_bytes);

    *out_bytes = buf;
    *out_size = total_size;
    return IMPULSE_OK;
}

impulse_status_t impulse_index_minimal_perfect_hash_lookup(
    const void* index_bytes,
    size_t index_size,
    const char* query_key,
    uint32_t* out_node_id
) {
    if (!index_bytes || !query_key || !out_node_id || index_size < sizeof(MphfHeader)) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    const uint8_t* raw = static_cast<const uint8_t*>(index_bytes);
    MphfHeader hdr;
    std::memcpy(&hdr, raw, sizeof(hdr));

    size_t str_offset = sizeof(MphfHeader);
    size_t table_offset = str_offset + hdr.string_table_bytes;

    if (index_size < table_offset + hdr.key_count * sizeof(MphfEntry)) return IMPULSE_ERR_BUFFER_OVERFLOW;

    const char* str_pool = reinterpret_cast<const char*>(raw + str_offset);
    const MphfEntry* table = reinterpret_cast<const MphfEntry*>(raw + table_offset);

    uint64_t h = fnv1a_hash(query_key, hdr.seed);
    size_t slot = h % hdr.key_count;
    size_t start_slot = slot;

    while (table[slot].key_string_offset != 0 || slot != start_slot) {
        if (table[slot].key_string_offset != 0) {
            const char* key_str = str_pool + table[slot].key_string_offset;
            if (std::strcmp(key_str, query_key) == 0) {
                *out_node_id = table[slot].node_id;
                return IMPULSE_OK;
            }
        }
        slot = (slot + 1) % hdr.key_count;
        if (slot == start_slot) break;
    }

    return IMPULSE_ERR_INVALID_ARGUMENT;
}

// =========================================================================
// INDEX 5: TRIGRAM 3-GRAM INDEX (0x05 - IMP_INDEX_TRIGRAM_3GRAM)
// =========================================================================

impulse_status_t impulse_index_build_trigram(
    const char** keys,
    size_t count,
    void** out_bytes,
    size_t* out_size
) {
    if (!keys || count == 0 || !out_bytes || !out_size) return IMPULSE_ERR_INVALID_ARGUMENT;

    size_t words_per_bitset = (count + 63) / 64;
    std::unordered_map<std::string, std::vector<uint64_t>> trigram_map;

    for (size_t i = 0; i < count; ++i) {
        if (!keys[i]) continue;
        size_t len = std::strlen(keys[i]);
        if (len < 3) continue;

        for (size_t j = 0; j <= len - 3; ++j) {
            std::string tri(keys[i] + j, 3);
            if (trigram_map.find(tri) == trigram_map.end()) {
                trigram_map[tri].resize(words_per_bitset, 0);
            }
            size_t w_idx = i / 64;
            size_t b_idx = i % 64;
            trigram_map[tri][w_idx] |= (1ULL << b_idx);
        }
    }

    uint32_t trigram_count = static_cast<uint32_t>(trigram_map.size());
    std::vector<TrigramEntry> entries;
    std::vector<std::vector<uint64_t>> payload_data;

    for (const auto& kv : trigram_map) {
        TrigramEntry entry;
        std::memset(entry.trigram, 0, 4);
        std::memcpy(entry.trigram, kv.first.data(), 3);
        entry.bitset_bytes = words_per_bitset * sizeof(uint64_t);
        entries.push_back(entry);
        payload_data.push_back(kv.second);
    }

    size_t header_size = sizeof(TrigramHeader);
    size_t entries_bytes = trigram_count * sizeof(TrigramEntry);
    size_t data_bytes_total = trigram_count * words_per_bitset * sizeof(uint64_t);
    size_t total_size = header_size + entries_bytes + data_bytes_total;

    uint8_t* buf = static_cast<uint8_t*>(std::malloc(total_size));
    if (!buf) return IMPULSE_ERR_BUFFER_OVERFLOW;

    TrigramHeader hdr;
    hdr.trigram_count = trigram_count;
    hdr.words_per_bitset = words_per_bitset;
    std::memset(hdr.reserved, 0, sizeof(hdr.reserved));

    size_t cur = 0;
    std::memcpy(buf + cur, &hdr, header_size);
    cur += header_size;

    size_t entries_offset = cur;
    cur += entries_bytes;

    for (size_t k = 0; k < trigram_count; ++k) {
        entries[k].bitset_offset = cur;
        std::memcpy(buf + cur, payload_data[k].data(), payload_data[k].size() * sizeof(uint64_t));
        cur += payload_data[k].size() * sizeof(uint64_t);
    }

    std::memcpy(buf + entries_offset, entries.data(), entries_bytes);

    *out_bytes = buf;
    *out_size = total_size;
    return IMPULSE_OK;
}

impulse_status_t impulse_index_trigram_search(
    const void* index_bytes,
    size_t index_size,
    const char* query_str,
    uint64_t* out_words,
    size_t max_words,
    size_t* out_num_words
) {
    if (!index_bytes || !query_str || !out_words || !out_num_words || index_size < sizeof(TrigramHeader)) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    size_t qlen = std::strlen(query_str);
    if (qlen < 3) return IMPULSE_ERR_INVALID_ARGUMENT;

    const uint8_t* raw = static_cast<const uint8_t*>(index_bytes);
    TrigramHeader hdr;
    std::memcpy(&hdr, raw, sizeof(hdr));

    if (max_words < hdr.words_per_bitset) return IMPULSE_ERR_BUFFER_OVERFLOW;
    std::memset(out_words, 0xFF, hdr.words_per_bitset * sizeof(uint64_t)); // Start with all 1s

    const TrigramEntry* entries = reinterpret_cast<const TrigramEntry*>(raw + sizeof(TrigramHeader));

    for (size_t j = 0; j <= qlen - 3; ++j) {
        char target_tri[4] = { query_str[j], query_str[j+1], query_str[j+2], '\0' };
        bool found = false;

        for (uint32_t k = 0; k < hdr.trigram_count; ++k) {
            if (std::memcmp(entries[k].trigram, target_tri, 3) == 0) {
                const uint64_t* tri_words = reinterpret_cast<const uint64_t*>(raw + entries[k].bitset_offset);
                for (size_t w = 0; w < hdr.words_per_bitset; ++w) {
                    out_words[w] &= tri_words[w];
                }
                found = true;
                break;
            }
        }
        if (!found) {
            std::memset(out_words, 0, hdr.words_per_bitset * sizeof(uint64_t));
            break;
        }
    }

    *out_num_words = hdr.words_per_bitset;
    return IMPULSE_OK;
}

// =========================================================================
// INDEX 6: DOMAIN-SPLIT BITSET INDEX (0x06 - IMP_INDEX_DOMAIN_SPLIT_BITSET)
// =========================================================================

impulse_status_t impulse_index_build_domain_split(
    const char** keys,
    size_t count,
    void** out_bytes,
    size_t* out_size
) {
    if (!keys || count == 0 || !out_bytes || !out_size) return IMPULSE_ERR_INVALID_ARGUMENT;

    std::vector<const char*> domain_keys(count);
    for (size_t i = 0; i < count; ++i) {
        if (!keys[i]) { domain_keys[i] = ""; continue; }
        const char* at = std::strchr(keys[i], '@');
        if (at) {
            domain_keys[i] = at + 1;
        } else {
            domain_keys[i] = keys[i];
        }
    }

    return impulse_index_build_inverted_bitset(domain_keys.data(), count, out_bytes, out_size);
}

impulse_status_t impulse_index_domain_split_lookup(
    const void* index_bytes,
    size_t index_size,
    const char* domain_str,
    const uint64_t** out_words,
    size_t* out_num_words
) {
    return impulse_index_inverted_bitset_lookup(index_bytes, index_size, domain_str, out_words, out_num_words);
}

// =========================================================================
// INDEX 7: TEMPORAL INTERVAL INDEX (0x07 - IMP_INDEX_TEMPORAL_INTERVAL)
// =========================================================================

impulse_status_t impulse_index_build_temporal_interval(
    const void* interval_data,
    size_t count,
    uint8_t type_code,
    void** out_bytes,
    size_t* out_size
) {
    if (!interval_data || count == 0 || !out_bytes || !out_size) return IMPULSE_ERR_INVALID_ARGUMENT;

    std::vector<TemporalIntervalEntry> entries(count);
    uint8_t base_type = type_code & 0x7F;

    if (base_type == 0x0D) { // INTERVAL_SEC_32
        struct Sec32 { uint32_t start_sec; uint32_t dur_sec; };
        const auto* raw = static_cast<const Sec32*>(interval_data);
        for (size_t i = 0; i < count; ++i) {
            entries[i].start_time = raw[i].start_sec;
            entries[i].end_time = raw[i].start_sec + raw[i].dur_sec;
            entries[i].node_id = static_cast<uint32_t>(i);
        }
    } else if (base_type == 0x0E) { // INTERVAL_MS_64
        struct Ms64 { uint64_t start_ms; uint64_t dur_ms; };
        const auto* raw = static_cast<const Ms64*>(interval_data);
        for (size_t i = 0; i < count; ++i) {
            entries[i].start_time = raw[i].start_ms;
            entries[i].end_time = raw[i].start_ms + raw[i].dur_ms;
            entries[i].node_id = static_cast<uint32_t>(i);
        }
    } else {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    std::sort(entries.begin(), entries.end(), [](const TemporalIntervalEntry& a, const TemporalIntervalEntry& b) {
        return a.start_time < b.start_time;
    });

    size_t header_size = sizeof(TemporalIntervalHeader);
    size_t entries_size = count * sizeof(TemporalIntervalEntry);
    size_t total_size = header_size + entries_size;

    uint8_t* buf = static_cast<uint8_t*>(std::malloc(total_size));
    if (!buf) return IMPULSE_ERR_BUFFER_OVERFLOW;

    TemporalIntervalHeader hdr;
    hdr.count = count;
    hdr.type_code = type_code;
    std::memset(hdr.reserved, 0, sizeof(hdr.reserved));

    std::memcpy(buf, &hdr, header_size);
    std::memcpy(buf + header_size, entries.data(), entries_size);

    *out_bytes = buf;
    *out_size = total_size;
    return IMPULSE_OK;
}

impulse_status_t impulse_index_temporal_interval_query(
    const void* index_bytes,
    size_t index_size,
    uint64_t target_time,
    uint32_t* out_node_ids,
    size_t max_nodes,
    size_t* out_count
) {
    if (!index_bytes || index_size < sizeof(TemporalIntervalHeader) || !out_node_ids || !out_count) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    const uint8_t* raw = static_cast<const uint8_t*>(index_bytes);
    TemporalIntervalHeader hdr;
    std::memcpy(&hdr, raw, sizeof(hdr));

    if (index_size < sizeof(TemporalIntervalHeader) + hdr.count * sizeof(TemporalIntervalEntry)) {
        return IMPULSE_ERR_BUFFER_OVERFLOW;
    }

    const TemporalIntervalEntry* entries = reinterpret_cast<const TemporalIntervalEntry*>(raw + sizeof(TemporalIntervalHeader));
    size_t matched = 0;

    for (size_t i = 0; i < hdr.count; ++i) {
        if (target_time >= entries[i].start_time && target_time <= entries[i].end_time) {
            if (matched < max_nodes) {
                out_node_ids[matched] = entries[i].node_id;
            }
            matched++;
        }
    }

    *out_count = matched;
    return IMPULSE_OK;
}

void impulse_index_free(void* ptr) {
    if (ptr) {
        std::free(ptr);
    }
}

} // extern "C"
