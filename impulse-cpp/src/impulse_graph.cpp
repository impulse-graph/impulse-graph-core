#include "impulse_graph.h"
#include "impulse_format_v2_4.h"
#include "impulse_sha256.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <new>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Platform I/O abstraction
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// Internal implementation details — anonymous namespace prevents symbol export
// ---------------------------------------------------------------------------
namespace {

thread_local std::string g_last_error;

// RAII guard for POSIX file descriptors
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

// RAII guard for mmap regions
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

void align64(std::vector<uint8_t>& buf) {
    size_t rem = buf.size() % 64;
    if (rem != 0) {
        buf.resize(buf.size() + (64 - rem), 0x00);
    }
}

void align4096(std::vector<uint8_t>& buf) {
    size_t rem = buf.size() % 4096;
    if (rem != 0) {
        buf.resize(buf.size() + (4096 - rem), 0x00);
    }
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

} // anonymous namespace

// ---------------------------------------------------------------------------
// Internal C++ types behind opaque C handles
// ---------------------------------------------------------------------------

struct impulse_domain_entry {
    uint16_t domain_id;
    uint8_t key_type;
    std::string name;
};

struct impulse_relation_entry {
    uint16_t src_domain_id;
    uint16_t tgt_domain_id;
    uint8_t encoding_type;
    uint64_t node_count;
    uint64_t edge_count;
    uint64_t section_features;
    std::vector<uint8_t> row_offsets_data;
    std::vector<uint8_t> col_indices_data;
};

struct impulse_delta_layer {
    uint16_t src_domain_id;
    uint16_t tgt_domain_id;
    std::string relation_name;
    mutable std::shared_mutex rw_lock;
    std::unordered_set<uint64_t> tombstones; // (src << 32 | tgt)
    std::unordered_map<uint32_t, std::vector<uint32_t>> additions; // src -> targets
};

struct impulse_writer {
    std::string output_path;
    uint64_t global_features;
    std::vector<impulse_domain_entry> domains;
    std::vector<impulse_relation_entry> relations;
};

struct impulse_snapshot {
    std::string snapshot_path;
    int fd = -1;
    void* mmap_ptr = nullptr;
    size_t mmap_size = 0;
    impulse_snapshot_header_t header;
    std::vector<impulse_relation_directory_entry_t> relations;
};

extern "C" {

// ---------------------------------------------------------------------------
// Snapshot Reader
// ---------------------------------------------------------------------------

IMPULSE_API impulse_snapshot_t* impulse_snapshot_open(const char* file_path, impulse_status_t* out_status) {
    try {
        if (!file_path) {
            g_last_error = "Invalid argument: file_path is null";
            if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
            return nullptr;
        }

#ifdef _WIN32
        // TODO: Implement Windows CreateFileMapping / MapViewOfFile path
        g_last_error = "Windows mmap not yet implemented";
        if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
        return nullptr;
#else
        FdGuard fd_guard(::open(file_path, O_RDONLY));
        if (fd_guard.fd < 0) {
            g_last_error = "Failed to open snapshot file: " + std::string(file_path);
            if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
            return nullptr;
        }

        struct stat st;
        if (::fstat(fd_guard.fd, &st) < 0 || st.st_size < static_cast<off_t>(sizeof(impulse_snapshot_header_t))) {
            g_last_error = "Corrupted file size";
            if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
            return nullptr;
        }

        MmapGuard mmap_guard(
            ::mmap(NULL, static_cast<size_t>(st.st_size), PROT_READ, MAP_SHARED, fd_guard.fd, 0),
            static_cast<size_t>(st.st_size)
        );
        if (mmap_guard.ptr == MAP_FAILED) {
            g_last_error = "mmap failed for snapshot file";
            if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
            return nullptr;
        }

        const auto* hdr = reinterpret_cast<const impulse_snapshot_header_t*>(mmap_guard.ptr);
        if (hdr->magic != IMPULSE_MAGIC) {
            g_last_error = "Corrupted or invalid snapshot header magic bytes";
            if (out_status) *out_status = IMPULSE_ERR_INVALID_MAGIC;
            return nullptr;
        }

        uint16_t version = hdr->version;
        if (version != 1 && version != 2 && version != 0x0204) {
            g_last_error = "Unsupported protocol version number: " + std::to_string(version);
            if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_VERSION;
            return nullptr;
        }

        // Fail-closed: check global required feature flags
        uint64_t known_feats = IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED | IMPULSE_GLOBAL_FEAT_CRYPTO_SIGNED;
        if (hdr->global_required_features & ~known_feats) {
            g_last_error = "Unsupported global feature flag requested";
            if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_GLOBAL_FEATURE;
            return nullptr;
        }
        if (hdr->global_required_features & IMPULSE_GLOBAL_FEAT_CRYPTO_SIGNED) {
            g_last_error = "Snapshot requires cryptographic signature verification which is not yet implemented";
            if (out_status) *out_status = IMPULSE_ERR_SIGNATURE_MISMATCH;
            return nullptr;
        }

        const uint8_t* base = reinterpret_cast<const uint8_t*>(mmap_guard.ptr);
        uint64_t data_off = hdr->data_offset;

        if (data_off > st.st_size) {
            g_last_error = "Data offset points outside file boundaries";
            if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
            return nullptr;
        }

        // SHA-256 payload checksum validation
        if (data_off < static_cast<size_t>(st.st_size)) {
            uint8_t actual_sha[32];
            impulse_sha256(base + data_off, st.st_size - data_off, actual_sha);
            if (std::memcmp(actual_sha, hdr->sha256_checksum, 32) != 0) {
                g_last_error = "SHA-256 checksum mismatch on snapshot binary load";
                if (out_status) *out_status = IMPULSE_ERR_CORRUPT_CHECKSUM;
                return nullptr;
            }
        }

        auto* snap = new impulse_snapshot();
        snap->snapshot_path = file_path;
        snap->fd = fd_guard.release();
        snap->mmap_ptr = mmap_guard.release();
        snap->mmap_size = static_cast<size_t>(st.st_size);
        std::memcpy(&snap->header, hdr, sizeof(impulse_snapshot_header_t));

        // Parse Domain Catalog and Relation Directory
        if (version == 0x0204) {
            size_t dom_catalog_size = hdr->domain_count * 64;
            size_t rel_dir_size = hdr->relation_count * 128;

            if (data_off + dom_catalog_size + rel_dir_size > snap->mmap_size) {
                g_last_error = "Catalog directory size exceeds snapshot file bounds";
                if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                delete snap;
                return nullptr;
            }

            // Validate domain string offsets
            for (uint16_t d = 0; d < hdr->domain_count; ++d) {
                const auto* dentry = reinterpret_cast<const impulse_domain_catalog_entry_v2_4_t*>(base + data_off + d * 64);
                if (dentry->name_length > 0 && dentry->name_offset > 0) {
                    if (dentry->name_offset + dentry->name_length > snap->mmap_size) {
                        g_last_error = "Domain name offset/length points outside file boundaries";
                        if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                        delete snap;
                        return nullptr;
                    }
                }
            }

            // Parse relation entries & validate section alignment/bounds
            size_t rel_dir_pos = data_off + dom_catalog_size;
            for (uint16_t r = 0; r < hdr->relation_count; ++r) {
                const auto* rentry = reinterpret_cast<const impulse_relation_directory_entry_v2_4_t*>(base + rel_dir_pos + r * 128);
                if (rentry->csr_offsets_pos > snap->mmap_size || rentry->csr_targets_pos > snap->mmap_size) {
                    g_last_error = "Catalog section offset points outside file boundaries";
                    if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                    delete snap;
                    return nullptr;
                }
                if (rentry->csr_offsets_pos > 0 && (rentry->csr_offsets_pos % 64 != 0)) {
                    g_last_error = "CSR row offsets array offset not 64-byte aligned";
                    if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                    delete snap;
                    return nullptr;
                }
                if (rentry->csr_targets_pos > 0 && (rentry->csr_targets_pos % 64 != 0)) {
                    g_last_error = "CSR column targets array offset not 64-byte aligned";
                    if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                    delete snap;
                    return nullptr;
                }

                impulse_relation_directory_entry_t entry;
                std::memset(&entry, 0, sizeof(entry));
                entry.src_domain_id = rentry->src_domain_id;
                entry.tgt_domain_id = rentry->tgt_domain_id;
                entry.encoding_type = rentry->encoding_type;
                entry.node_count = rentry->node_count;
                entry.edge_count = rentry->edge_count;
                entry.section_features = rentry->required_features;
                entry.csr_row_off_offset = rentry->csr_offsets_pos;
                entry.csr_row_off_bytes = rentry->csr_offsets_size;
                entry.csr_col_idx_offset = rentry->csr_targets_pos;
                entry.csr_col_idx_bytes = rentry->csr_targets_size;
                snap->relations.push_back(entry);
            }
        } else if (data_off < snap->mmap_size && hdr->relation_count > 0) {
            size_t cur = static_cast<size_t>(data_off);

            // Skip Domain Catalog — with name_len bounds validation
            for (uint16_t d = 0; d < hdr->domain_count; ++d) {
                if (cur + sizeof(impulse_domain_catalog_entry_header_t) > snap->mmap_size) break;
                const auto* dhdr = reinterpret_cast<const impulse_domain_catalog_entry_header_t*>(base + cur);
                size_t entry_size = sizeof(impulse_domain_catalog_entry_header_t) + dhdr->name_len;
                if (cur + entry_size > snap->mmap_size) break;
                cur += entry_size;
            }

            // 64-byte align to relation directory
            size_t rem = cur % 64;
            if (rem != 0) cur += (64 - rem);

            for (uint16_t r = 0; r < hdr->relation_count && cur + sizeof(impulse_relation_directory_entry_t) <= snap->mmap_size; ++r) {
                impulse_relation_directory_entry_t entry;
                std::memcpy(&entry, base + cur, sizeof(entry));
                snap->relations.push_back(entry);
                cur += sizeof(entry);
            }
        }

        if (out_status) *out_status = IMPULSE_OK;
        return snap;
#endif
    } catch (const std::exception& e) {
        g_last_error = std::string("Exception in impulse_snapshot_open: ") + e.what();
        if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
        return nullptr;
    }
}

IMPULSE_API void impulse_snapshot_close(impulse_snapshot_t* snapshot) {
    if (snapshot) {
#ifndef _WIN32
        if (snapshot->mmap_ptr && snapshot->mmap_ptr != MAP_FAILED) {
            ::munmap(snapshot->mmap_ptr, snapshot->mmap_size);
        }
        if (snapshot->fd >= 0) {
            ::close(snapshot->fd);
        }
#endif
        delete snapshot;
    }
}

// ---------------------------------------------------------------------------
// Snapshot Inspection
// ---------------------------------------------------------------------------

IMPULSE_API uint32_t impulse_snapshot_magic(const impulse_snapshot_t* snapshot) {
    return snapshot ? snapshot->header.magic : 0;
}

IMPULSE_API uint16_t impulse_snapshot_version(const impulse_snapshot_t* snapshot) {
    return snapshot ? snapshot->header.version : 0;
}

IMPULSE_API uint16_t impulse_snapshot_domain_count(const impulse_snapshot_t* snapshot) {
    return snapshot ? snapshot->header.domain_count : 0;
}

IMPULSE_API uint16_t impulse_snapshot_relation_count(const impulse_snapshot_t* snapshot) {
    return snapshot ? static_cast<uint16_t>(snapshot->relations.size()) : 0;
}

IMPULSE_API impulse_status_t impulse_snapshot_get_relation_entry(
    const impulse_snapshot_t* snapshot,
    uint16_t index,
    impulse_relation_directory_entry_t* out_entry
) {
    if (!snapshot || !out_entry || index >= snapshot->relations.size()) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    *out_entry = snapshot->relations[index];
    return IMPULSE_OK;
}

IMPULSE_API const void* impulse_snapshot_get_buffer(
    const impulse_snapshot_t* snapshot,
    uint64_t offset,
    uint64_t size
) {
    if (!snapshot || !snapshot->mmap_ptr) return nullptr;
    // Overflow-safe bounds check
    if (offset > snapshot->mmap_size || size > snapshot->mmap_size - offset) {
        return nullptr;
    }
    return reinterpret_cast<const uint8_t*>(snapshot->mmap_ptr) + offset;
}

// ---------------------------------------------------------------------------
// Reachability Query
// ---------------------------------------------------------------------------

IMPULSE_API bool impulse_snapshot_is_reachable(
    const impulse_snapshot_t* snapshot,
    uint16_t src_domain, uint32_t src_id,
    uint16_t tgt_domain, uint32_t tgt_id
) {
    if (!snapshot || !snapshot->mmap_ptr) return false;

    for (const auto& rel : snapshot->relations) {
        if (rel.src_domain_id == src_domain && rel.tgt_domain_id == tgt_domain) {
            if (src_id >= rel.node_count) return false;

            // Validate CSR data regions are within mmap bounds (overflow-safe)
            if (rel.csr_row_off_offset > snapshot->mmap_size ||
                rel.csr_row_off_bytes > snapshot->mmap_size - rel.csr_row_off_offset ||
                rel.csr_col_idx_offset > snapshot->mmap_size ||
                rel.csr_col_idx_bytes > snapshot->mmap_size - rel.csr_col_idx_offset) {
                return false;
            }

            // Validate row_offsets array has room for row_offsets[src_id+1]
            uint64_t required_offset_bytes = (static_cast<uint64_t>(src_id) + 2) * sizeof(uint32_t);
            if (required_offset_bytes > rel.csr_row_off_bytes) return false;

            const uint8_t* base = reinterpret_cast<const uint8_t*>(snapshot->mmap_ptr);
            const uint32_t* row_offsets = reinterpret_cast<const uint32_t*>(base + rel.csr_row_off_offset);
            const uint32_t* col_indices = reinterpret_cast<const uint32_t*>(base + rel.csr_col_idx_offset);

            uint32_t start_idx = row_offsets[src_id];
            uint32_t end_idx = row_offsets[src_id + 1];

            // Validate column index range is within bounds
            if (end_idx > rel.csr_col_idx_bytes / sizeof(uint32_t) || start_idx > end_idx) {
                return false;
            }

            for (uint32_t i = start_idx; i < end_idx; ++i) {
                if (col_indices[i] == tgt_id) {
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Neighbor Sampler
// ---------------------------------------------------------------------------

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
) {
    try {
        if (!snapshot || !snapshot->mmap_ptr || !src_nodes || !out_count || relation_index >= snapshot->relations.size()) {
            g_last_error = "Invalid snapshot, relation index, or NULL buffer pointer";
            return IMPULSE_ERR_INVALID_ARGUMENT;
        }

        const auto& rel = snapshot->relations[relation_index];

        // Overflow-safe bounds validation on CSR data regions
        if (rel.csr_row_off_offset > snapshot->mmap_size ||
            rel.csr_row_off_bytes > snapshot->mmap_size - rel.csr_row_off_offset ||
            rel.csr_col_idx_offset > snapshot->mmap_size ||
            rel.csr_col_idx_bytes > snapshot->mmap_size - rel.csr_col_idx_offset) {
            g_last_error = "Relation directory offset out of bounds";
            return IMPULSE_ERR_CORRUPT_CHECKSUM;
        }

        const uint8_t* base = reinterpret_cast<const uint8_t*>(snapshot->mmap_ptr);
        const uint32_t* row_offsets = reinterpret_cast<const uint32_t*>(base + rel.csr_row_off_offset);
        const uint32_t* col_indices = reinterpret_cast<const uint32_t*>(base + rel.csr_col_idx_offset);

        uint64_t num_offsets = rel.node_count + 1;
        uint64_t max_col_entries = rel.csr_col_idx_bytes / sizeof(uint32_t);

        // Validate row_offsets array size
        if (num_offsets * sizeof(uint32_t) > rel.csr_row_off_bytes) {
            g_last_error = "Row offsets array size mismatch with node_count";
            return IMPULSE_ERR_CORRUPT_CHECKSUM;
        }

        pcg32_fast rng(seed, 54);
        size_t written = 0;

        for (size_t i = 0; i < num_nodes; ++i) {
            uint32_t u = src_nodes[i];
            if (u + 1 >= num_offsets) continue;

            uint32_t start_off = row_offsets[u];
            uint32_t end_off = row_offsets[u + 1];
            if (end_off > max_col_entries || start_off >= end_off) continue;

            uint32_t deg = end_off - start_off;

            if (k_samples < 0 || static_cast<uint32_t>(k_samples) >= deg) {
                for (uint32_t idx = start_off; idx < end_off; ++idx) {
                    if (written >= out_capacity) {
                        *out_count = written;
                        g_last_error = "Output buffer capacity exceeded";
                        return IMPULSE_ERR_BUFFER_OVERFLOW;
                    }
                    if (out_src) out_src[written] = u;
                    if (out_tgt) out_tgt[written] = col_indices[idx];
                    written++;
                }
            } else {
                for (int k = 0; k < k_samples; ++k) {
                    if (written >= out_capacity) {
                        *out_count = written;
                        g_last_error = "Output buffer capacity exceeded";
                        return IMPULSE_ERR_BUFFER_OVERFLOW;
                    }
                    uint32_t pick = start_off + rng.bounded(deg);
                    if (out_src) out_src[written] = u;
                    if (out_tgt) out_tgt[written] = col_indices[pick];
                    written++;
                }
            }
        }

        *out_count = written;
        return IMPULSE_OK;
    } catch (const std::exception& e) {
        g_last_error = std::string("Exception in impulse_snapshot_sample_neighbors: ") + e.what();
        return IMPULSE_ERR_IO_FAILURE;
    }
}

// ---------------------------------------------------------------------------
// Snapshot Writer
// ---------------------------------------------------------------------------

IMPULSE_API impulse_writer_t* impulse_writer_create(const char* output_file_path, uint64_t global_features) {
    try {
        if (!output_file_path) {
            g_last_error = "Output file path cannot be null";
            return nullptr;
        }
        auto* writer = new impulse_writer();
        writer->output_path = output_file_path;
        writer->global_features = global_features | IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED;
        return writer;
    } catch (const std::exception& e) {
        g_last_error = std::string("Exception in impulse_writer_create: ") + e.what();
        return nullptr;
    }
}

IMPULSE_API impulse_status_t impulse_writer_add_domain(impulse_writer_t* writer, uint16_t domain_id, uint8_t key_type, const char* name) {
    try {
        if (!writer || !name) {
            g_last_error = "Invalid writer or domain name";
            return IMPULSE_ERR_INVALID_ARGUMENT;
        }
        writer->domains.push_back({domain_id, key_type, std::string(name)});
        return IMPULSE_OK;
    } catch (const std::exception& e) {
        g_last_error = std::string("Exception in impulse_writer_add_domain: ") + e.what();
        return IMPULSE_ERR_IO_FAILURE;
    }
}

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
) {
    try {
        if (!writer || (!row_offsets_data && row_offsets_bytes > 0) || (!col_indices_data && col_indices_bytes > 0)) {
            g_last_error = "Invalid writer or buffer pointers";
            return IMPULSE_ERR_INVALID_ARGUMENT;
        }

        impulse_relation_entry rel;
        rel.src_domain_id = src_domain_id;
        rel.tgt_domain_id = tgt_domain_id;
        rel.encoding_type = encoding_type;
        rel.node_count = node_count;
        rel.edge_count = edge_count;
        rel.section_features = section_features | (1ULL << encoding_type);

        if (row_offsets_data && row_offsets_bytes > 0) {
            const uint8_t* ptr = reinterpret_cast<const uint8_t*>(row_offsets_data);
            rel.row_offsets_data.assign(ptr, ptr + row_offsets_bytes);
        }
        if (col_indices_data && col_indices_bytes > 0) {
            const uint8_t* ptr = reinterpret_cast<const uint8_t*>(col_indices_data);
            rel.col_indices_data.assign(ptr, ptr + col_indices_bytes);
        }

        writer->relations.push_back(std::move(rel));
        return IMPULSE_OK;
    } catch (const std::exception& e) {
        g_last_error = std::string("Exception in impulse_writer_add_relation: ") + e.what();
        return IMPULSE_ERR_IO_FAILURE;
    }
}

IMPULSE_API impulse_status_t impulse_writer_finalize(impulse_writer_t* writer) {
    try {
        if (!writer) {
            g_last_error = "Null writer pointer";
            return IMPULSE_ERR_INVALID_ARGUMENT;
        }

        std::vector<uint8_t> payload;

        uint64_t base_file_offset = IMPULSE_DEFAULT_DATA_OFFSET;
        uint16_t domain_count = static_cast<uint16_t>(writer->domains.size());
        uint16_t relation_count = static_cast<uint16_t>(writer->relations.size());

        size_t dom_catalog_bytes = domain_count * 64;
        size_t rel_dir_bytes = relation_count * 128;
        size_t string_table_pos = base_file_offset + dom_catalog_bytes + rel_dir_bytes;

        std::vector<uint8_t> string_table;

        // Section 2 Part A: 64-byte Domain Catalog Table
        for (const auto& dom : writer->domains) {
            impulse_domain_catalog_entry_v2_4_t dentry;
            std::memset(&dentry, 0, sizeof(dentry));
            dentry.domain_id = dom.domain_id;
            dentry.key_type = dom.key_type;

            if (!dom.name.empty()) {
                dentry.name_offset = static_cast<uint32_t>(string_table_pos + string_table.size());
                dentry.name_length = static_cast<uint16_t>(dom.name.size());
                string_table.insert(string_table.end(), dom.name.begin(), dom.name.end());
            }

            const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&dentry);
            payload.insert(payload.end(), ptr, ptr + 64);
        }

        // Section 2 Part B: 128-byte Relation Directory Table
        size_t directory_start_offset = payload.size();
        std::vector<impulse_relation_directory_entry_v2_4_t> dir_table(relation_count);
        payload.insert(payload.end(), rel_dir_bytes, 0x00);

        // String Table
        payload.insert(payload.end(), string_table.begin(), string_table.end());
        align64(payload);

        for (size_t i = 0; i < writer->relations.size(); ++i) {
            const auto& rel = writer->relations[i];
            auto& entry = dir_table[i];
            std::memset(&entry, 0, sizeof(entry));

            entry.src_domain_id = rel.src_domain_id;
            entry.tgt_domain_id = rel.tgt_domain_id;
            entry.encoding_type = rel.encoding_type;
            entry.node_count = rel.node_count;
            entry.edge_count = rel.edge_count;
            entry.required_features = rel.section_features;

            // RowOffsets Stream
            align64(payload);
            entry.csr_offsets_pos = base_file_offset + payload.size();
            entry.csr_offsets_size = rel.row_offsets_data.size();
            payload.insert(payload.end(), rel.row_offsets_data.begin(), rel.row_offsets_data.end());

            // ColumnIndices Stream
            align64(payload);
            entry.csr_targets_pos = base_file_offset + payload.size();
            entry.csr_targets_size = rel.col_indices_data.size();
            payload.insert(payload.end(), rel.col_indices_data.begin(), rel.col_indices_data.end());
        }

        std::memcpy(payload.data() + directory_start_offset, dir_table.data(), rel_dir_bytes);
        align4096(payload);

        // Compute SHA-256 Digest
        uint8_t payload_sha256[32];
        impulse_sha256(payload.data(), payload.size(), payload_sha256);

        // Construct 4KB Header
        uint8_t header_buf[4096];
        std::memset(header_buf, 0, 4096);

        impulse_snapshot_header_t hdr;
        std::memset(&hdr, 0, sizeof(hdr));
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = 0x0204;
        hdr.data_offset = IMPULSE_DEFAULT_DATA_OFFSET;
        hdr.domain_count = domain_count;
        hdr.relation_count = relation_count;
        hdr.kafka_offset = static_cast<uint64_t>(std::time(nullptr));
        hdr.timestamp_ms = static_cast<uint64_t>(std::time(nullptr)) * 1000ULL;
        std::memcpy(hdr.sha256_checksum, payload_sha256, 32);
        hdr.global_required_features = writer->global_features;

        std::memcpy(header_buf, &hdr, sizeof(hdr));

        // Compute Header CRC-32C at offset 0x458 (1112)
        std::vector<uint8_t> crc_data;
        crc_data.insert(crc_data.end(), header_buf, header_buf + 0x48);
        crc_data.insert(crc_data.end(), header_buf + 0x448, header_buf + 0x458);

        uint32_t crc32c = 0xFFFFFFFF;
        for (uint8_t b : crc_data) {
            crc32c ^= b;
            for (int k = 0; k < 8; ++k) {
                crc32c = (crc32c >> 1) ^ ((crc32c & 1) ? 0x82F63B78 : 0);
            }
        }
        crc32c ^= 0xFFFFFFFF;
        std::memcpy(header_buf + 0x458, &crc32c, 4);

        std::ofstream ofs(writer->output_path, std::ios::binary);
        if (!ofs.is_open()) {
            g_last_error = "Failed to open output file for writing: " + writer->output_path;
            return IMPULSE_ERR_IO_FAILURE;
        }

        ofs.write(reinterpret_cast<const char*>(header_buf), 4096);
        ofs.write(reinterpret_cast<const char*>(payload.data()), payload.size());
        ofs.close();

        return IMPULSE_OK;
    } catch (const std::exception& e) {
        g_last_error = std::string("Exception in impulse_writer_finalize: ") + e.what();
        return IMPULSE_ERR_IO_FAILURE;
    }
}

IMPULSE_API void impulse_writer_destroy(impulse_writer_t* writer) {
    delete writer;
}

// ---------------------------------------------------------------------------
// Live Delta Layer & Compaction Implementation
// ---------------------------------------------------------------------------

IMPULSE_API impulse_delta_layer_t* impulse_delta_layer_create(uint16_t src_domain_id, uint16_t tgt_domain_id, const char* relation_name) {
    try {
        auto* delta = new impulse_delta_layer();
        delta->src_domain_id = src_domain_id;
        delta->tgt_domain_id = tgt_domain_id;
        delta->relation_name = relation_name ? relation_name : "";
        return delta;
    } catch (...) {
        return nullptr;
    }
}

IMPULSE_API impulse_status_t impulse_delta_layer_add_edge(impulse_delta_layer_t* delta, uint32_t src_node, uint32_t tgt_node) {
    if (!delta) return IMPULSE_ERR_INVALID_ARGUMENT;
    std::unique_lock<std::shared_mutex> lock(delta->rw_lock);
    uint64_t key = (static_cast<uint64_t>(src_node) << 32) | tgt_node;
    delta->tombstones.erase(key);
    auto& list = delta->additions[src_node];
    if (std::find(list.begin(), list.end(), tgt_node) == list.end()) {
        list.push_back(tgt_node);
    }
    return IMPULSE_OK;
}

IMPULSE_API impulse_status_t impulse_delta_layer_tombstone_edge(impulse_delta_layer_t* delta, uint32_t src_node, uint32_t tgt_node) {
    if (!delta) return IMPULSE_ERR_INVALID_ARGUMENT;
    std::unique_lock<std::shared_mutex> lock(delta->rw_lock);
    uint64_t key = (static_cast<uint64_t>(src_node) << 32) | tgt_node;
    delta->tombstones.insert(key);
    auto it = delta->additions.find(src_node);
    if (it != delta->additions.end()) {
        auto& list = it->second;
        list.erase(std::remove(list.begin(), list.end(), tgt_node), list.end());
    }
    return IMPULSE_OK;
}

IMPULSE_API bool impulse_delta_layer_is_tombstoned(const impulse_delta_layer_t* delta, uint32_t src_node, uint32_t tgt_node) {
    if (!delta) return false;
    std::shared_lock<std::shared_mutex> lock(delta->rw_lock);
    uint64_t key = (static_cast<uint64_t>(src_node) << 32) | tgt_node;
    return delta->tombstones.find(key) != delta->tombstones.end();
}

IMPULSE_API void impulse_delta_layer_destroy(impulse_delta_layer_t* delta) {
    delete delta;
}

IMPULSE_API impulse_status_t impulse_snapshot_compact_to_file(
    const impulse_snapshot_t* base_snapshot,
    impulse_delta_layer_t** deltas,
    size_t delta_count,
    const char* output_file_path
) {
    try {
        if (!base_snapshot || !output_file_path) {
            g_last_error = "Invalid base snapshot or output file path";
            return IMPULSE_ERR_INVALID_ARGUMENT;
        }

        impulse_writer_t* writer = impulse_writer_create(output_file_path, base_snapshot->header.global_required_features);
        if (!writer) return IMPULSE_ERR_IO_FAILURE;

        // Map delta layers by relation index / name
        std::unordered_map<size_t, impulse_delta_layer_t*> delta_map;
        for (size_t d = 0; d < delta_count; ++d) {
            if (deltas && deltas[d]) {
                delta_map[d] = deltas[d];
            }
        }

        // Process each relation in base_snapshot
        uint16_t rel_count = impulse_snapshot_relation_count(base_snapshot);
        const uint8_t* base = reinterpret_cast<const uint8_t*>(base_snapshot->mmap_ptr);

        for (uint16_t r = 0; r < rel_count; ++r) {
            impulse_relation_directory_entry_t rentry;
            impulse_status_t st = impulse_snapshot_get_relation_entry(base_snapshot, r, &rentry);
            if (st != IMPULSE_OK) {
                impulse_writer_destroy(writer);
                return st;
            }

            impulse_delta_layer_t* delta = delta_map.count(r) ? delta_map[r] : nullptr;

            const uint32_t* row_offs = (rentry.csr_row_off_offset > 0 && rentry.csr_row_off_bytes >= 4) ?
                reinterpret_cast<const uint32_t*>(base + rentry.csr_row_off_offset) : nullptr;
            const uint32_t* col_tgts = (rentry.csr_col_idx_offset > 0 && rentry.csr_col_idx_bytes >= 4) ?
                reinterpret_cast<const uint32_t*>(base + rentry.csr_col_idx_offset) : nullptr;

            uint32_t num_row_offs = static_cast<uint32_t>(rentry.csr_row_off_bytes / 4);
            uint32_t num_nodes = rentry.node_count;

            std::vector<uint32_t> final_row_offs;
            std::vector<uint32_t> final_cols;
            final_row_offs.push_back(0);

            for (uint32_t src = 0; src < num_nodes; ++src) {
                if (row_offs && col_tgts && src + 1 < num_row_offs) {
                    uint32_t start = row_offs[src];
                    uint32_t end = row_offs[src + 1];
                    for (uint32_t i = start; i < end; ++i) {
                        uint32_t tgt = col_tgts[i];
                        bool tomb = delta ? impulse_delta_layer_is_tombstoned(delta, src, tgt) : false;
                        if (!tomb) {
                            final_cols.push_back(tgt);
                        }
                    }
                }

                if (delta) {
                    std::shared_lock<std::shared_mutex> lock(delta->rw_lock);
                    auto it = delta->additions.find(src);
                    if (it != delta->additions.end()) {
                        for (uint32_t add_tgt : it->second) {
                            if (std::find(final_cols.begin() + final_row_offs.back(), final_cols.end(), add_tgt) == final_cols.end()) {
                                final_cols.push_back(add_tgt);
                            }
                        }
                    }
                }
                final_row_offs.push_back(static_cast<uint32_t>(final_cols.size()));
            }

            st = impulse_writer_add_relation(
                writer,
                rentry.src_domain_id, rentry.tgt_domain_id,
                rentry.encoding_type,
                num_nodes, final_cols.size(),
                rentry.section_features,
                final_row_offs.data(), final_row_offs.size() * sizeof(uint32_t),
                final_cols.data(), final_cols.size() * sizeof(uint32_t)
            );
            if (st != IMPULSE_OK) {
                impulse_writer_destroy(writer);
                return st;
            }
        }

        impulse_status_t comp_st = impulse_writer_finalize(writer);
        impulse_writer_destroy(writer);
        return comp_st;
    } catch (const std::exception& e) {
        g_last_error = std::string("Exception in impulse_snapshot_compact_to_file: ") + e.what();
        return IMPULSE_ERR_IO_FAILURE;
    }
}

// ---------------------------------------------------------------------------
// Cryptographic Signing & Verification (Stubs — Fail-Closed)
// ---------------------------------------------------------------------------

IMPULSE_API impulse_status_t impulse_snapshot_sign_ed25519(
    const char* snapshot_path,
    const uint8_t secret_key[64],
    const uint8_t public_key[32],
    uint16_t sig_flags
) {
    (void)snapshot_path;
    (void)secret_key;
    (void)public_key;
    (void)sig_flags;
    g_last_error = "Ed25519 signing is not yet implemented — link a real Ed25519 library";
    return IMPULSE_ERR_UNSUPPORTED_GLOBAL_FEATURE;
}

IMPULSE_API impulse_status_t impulse_snapshot_verify_ed25519(const impulse_snapshot_t* snapshot) {
    if (!snapshot) {
        g_last_error = "Invalid snapshot handle";
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    const impulse_snapshot_header_t* hdr = &snapshot->header;
    if (!(hdr->global_required_features & IMPULSE_GLOBAL_FEAT_CRYPTO_SIGNED)) {
        // No signature present — nothing to verify
        return IMPULSE_OK;
    }
    // Fail-closed: cannot verify without a real Ed25519 implementation
    g_last_error = "Ed25519 verification is not yet implemented — link a real Ed25519 library";
    return IMPULSE_ERR_SIGNATURE_MISMATCH;
}

// ---------------------------------------------------------------------------
// Error Reporting
// ---------------------------------------------------------------------------

IMPULSE_API const char* impulse_get_last_error(void) {
    return g_last_error.c_str();
}

} // extern "C"
