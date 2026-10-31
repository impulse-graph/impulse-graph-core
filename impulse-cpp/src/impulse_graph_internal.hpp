/**
 * @file impulse_graph_internal.hpp
 * @brief Internal data structures and alignment/checksum helpers for Impulse Graph Snapshot Reader & Writer.
 */

#ifndef IMPULSE_GRAPH_INTERNAL_HPP
#define IMPULSE_GRAPH_INTERNAL_HPP

#include "impulse_index.h"
#include <string>

#include "impulse_graph.h"
#include "impulse_assert.h"
#include "impulse_format_v0_9.h"
#include "impulse_sha256.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif


extern thread_local std::string g_last_error;

struct FdGuard {
    int fd;
    explicit FdGuard(int f) : fd(f) {}
    ~FdGuard() {
#ifndef _WIN32
        if (fd >= 0) ::close(fd);
#endif
    }
    int release() { int f = fd; fd = -1; return f; }
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;
};

struct MmapGuard {
    void* ptr;
    size_t size;
    MmapGuard(void* p, size_t s) : ptr(p), size(s) {}
    ~MmapGuard() {
#ifndef _WIN32
        if (ptr && ptr != MAP_FAILED) ::munmap(ptr, size);
#endif
    }
    void* release() { void* p = ptr; ptr = nullptr; return p; }
    MmapGuard(const MmapGuard&) = delete;
    MmapGuard& operator=(const MmapGuard&) = delete;
};

inline void align128(std::vector<uint8_t>& buf) {
    size_t rem = buf.size() % 128;
    if (rem != 0) {
        buf.resize(buf.size() + (128 - rem), 0x00);
    }
}

inline void align4096(std::vector<uint8_t>& buf) {
    size_t rem = buf.size() % 4096;
    if (rem != 0) {
        buf.resize(buf.size() + (4096 - rem), 0x00);
    }
}

inline uint16_t compute_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (static_cast<uint16_t>(data[i]) << 8);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

struct pcg32_fast {
    uint64_t state;
    uint64_t inc;
    pcg32_fast(uint64_t initstate, uint64_t initseq) {
        state = 0U;
        inc = (initseq << 1u) | 1u;
        next();
        state += initstate;
        next();
    }
    inline uint32_t next() {
        uint64_t oldstate = state;
        state = oldstate * 6364136223846793005ULL + inc;
        uint32_t xorshifted = static_cast<uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
        uint32_t rot = static_cast<uint32_t>(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }
    inline uint32_t bounded(uint32_t bound) {
        if (bound == 0) return 0;
        uint32_t threshold = -bound % bound;
        for (;;) {
            uint32_t r = next();
            if (r >= threshold) return r % bound;
        }
    }
};


struct impulse_writer_index {
    uint32_t index_id;
    uint16_t domain_id;
    uint16_t relation_id;
    uint16_t attribute_index;
    uint8_t index_type;
    std::string index_name;
    std::vector<uint8_t> data;
    uint64_t payload_feature_mask;
};

struct impulse_snapshot {
    void* mmap_ptr = nullptr;
    size_t file_size = 0;
    impulse_snapshot_header_t header;
    std::vector<impulse_domain_catalog_entry_t> domains;
    std::vector<std::string> domain_names;
    std::vector<impulse_relation_directory_entry_t> relations;
    std::vector<std::vector<impulse_attribute_descriptor_t>> relation_attributes;
    std::vector<impulse_index_directory_entry_v0_9_t> indexes;
    std::vector<std::string> index_names;
    std::unordered_map<std::string, std::string> metadata;
};

struct impulse_writer_attr {
    std::string name;
    uint8_t type_code;
    uint32_t dimension;
    std::vector<uint8_t> data;
    std::vector<uint8_t> offsets;
};

struct impulse_writer_relation {
    uint16_t relation_id;
    uint16_t src_domain_id;
    uint16_t tgt_domain_id;
    uint8_t encoding_id;
    uint8_t node_id_width;
    uint8_t edge_index_width;
    uint64_t node_count;
    uint64_t edge_count;
    uint64_t section_features;
    std::vector<uint8_t> row_offsets;
    std::vector<uint8_t> col_indices;
    std::vector<impulse_writer_attr> attributes;
};

struct impulse_writer {
    std::string output_path;
    impulse_write_fn write_cb = nullptr;
    void* user_data = nullptr;
    uint64_t global_features = 0;
    std::vector<impulse_domain_catalog_entry_t> domains;
    std::vector<std::string> domain_names;
    std::vector<impulse_writer_relation> relations;
    std::vector<impulse_writer_index> indexes;
    std::unordered_map<std::string, std::string> metadata;
};

struct impulse_delta_layer {
    uint16_t src_domain_id;
    uint16_t tgt_domain_id;
    std::string relation_name;
    mutable std::shared_mutex mutex;
    std::unordered_set<uint64_t> tombstones;
    std::unordered_map<uint64_t, std::vector<uint64_t>> additions;
};


#endif // IMPULSE_GRAPH_INTERNAL_HPP
