#include "impulse_graph.h"
#include "impulse_format_v0_9.h"
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

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

namespace {

thread_local std::string g_last_error;

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

void align128(std::vector<uint8_t>& buf) {
    size_t rem = buf.size() % 128;
    if (rem != 0) {
        buf.resize(buf.size() + (128 - rem), 0x00);
    }
}

void align4096(std::vector<uint8_t>& buf) {
    size_t rem = buf.size() % 4096;
    if (rem != 0) {
        buf.resize(buf.size() + (4096 - rem), 0x00);
    }
}

static uint16_t compute_crc16(const uint8_t* data, size_t len) {
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

} // anonymous namespace

struct impulse_snapshot {
    void* mmap_ptr = nullptr;
    size_t file_size = 0;
    impulse_snapshot_header_t header;
    std::vector<impulse_domain_catalog_entry_t> domains;
    std::vector<std::string> domain_names;
    std::vector<impulse_relation_directory_entry_t> relations;
    std::vector<std::vector<impulse_attribute_descriptor_t>> relation_attributes;
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

extern "C" {

const char* impulse_get_last_error(void) {
    return g_last_error.c_str();
}

impulse_snapshot_t* impulse_snapshot_open(const char* file_path, impulse_status_t* out_status) {
    if (!file_path) {
        if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
        return nullptr;
    }

#ifdef _WIN32
    HANDLE hFile = CreateFileA(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        g_last_error = "Failed to open file on Windows";
        if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
        return nullptr;
    }
    LARGE_INTEGER sz;
    GetFileSizeEx(hFile, &sz);
    size_t file_size = static_cast<size_t>(sz.QuadPart);
    HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap) {
        CloseHandle(hFile);
        if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
        return nullptr;
    }
    void* mmap_ptr = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hMap);
    CloseHandle(hFile);
    if (!mmap_ptr) {
        if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
        return nullptr;
    }
#else
    int fd = ::open(file_path, O_RDONLY);
    if (fd < 0) {
        g_last_error = "Failed to open file";
        if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
        return nullptr;
    }
    FdGuard fd_guard(fd);
    struct stat st;
    if (::fstat(fd, &st) < 0) {
        if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
        return nullptr;
    }
    size_t file_size = static_cast<size_t>(st.st_size);
    if (file_size < 4096) {
        g_last_error = "File size smaller than 4KB Page 0 header";
        if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
        return nullptr;
    }
    void* mmap_ptr = ::mmap(nullptr, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if (mmap_ptr == MAP_FAILED) {
        g_last_error = "mmap failed";
        if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
        return nullptr;
    }
#endif

    const uint8_t* raw = static_cast<const uint8_t*>(mmap_ptr);

    auto snap = std::make_unique<impulse_snapshot_t>();
    snap->mmap_ptr = mmap_ptr;
    snap->file_size = file_size;

    std::memcpy(&snap->header, raw, sizeof(impulse_snapshot_header_t));

    if (snap->header.magic != IMPULSE_MAGIC) {
        g_last_error = "Invalid magic bytes";
        if (out_status) *out_status = IMPULSE_ERR_INVALID_MAGIC;
        return nullptr;
    }

    uint16_t ver = snap->header.version;
    if (ver != IMPULSE_SPEC_VERSION_PACKED && (ver >> 8) != IMPULSE_SPEC_VERSION_MAJOR && ver != 2 && ver != 0x0204) {
        g_last_error = "Unsupported version";
        if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_VERSION;
        return nullptr;
    }

    if (ver == 9 || ver == 0x0009) {
        const uint64_t known_features = IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED |
                                        IMPULSE_GLOBAL_FEAT_CRYPTO_SIGNED |
                                        IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG;
        if ((snap->header.required_features & ~known_features) != 0) {
            g_last_error = "Unsupported global feature bitmask";
            if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_GLOBAL_FEATURE;
            return nullptr;
        }

        uint16_t expected_crc = compute_crc16(raw, 0x3E);
        if (expected_crc != snap->header.header_checksum) {
            g_last_error = "Header CRC-16 checksum mismatch";
            if (out_status) *out_status = IMPULSE_ERR_CORRUPT_CHECKSUM;
            return nullptr;
        }
    } else {
        uint8_t zeros[32] = {0};
        uint8_t leg_sha[32];
        std::memcpy(leg_sha, raw + 30, 32);
        if (std::memcmp(leg_sha, zeros, 32) != 0) {
            uint8_t calc_sha[32];
            impulse_sha256(raw + 4096, file_size - 4096, calc_sha);
            if (std::memcmp(calc_sha, leg_sha, 32) != 0) {
                g_last_error = "SHA-256 payload checksum mismatch";
                if (out_status) *out_status = IMPULSE_ERR_CORRUPT_CHECKSUM;
                return nullptr;
            }
        }

        if (file_size >= 72) {
            uint64_t leg_feat = 0;
            std::memcpy(&leg_feat, raw + 64, 8);
            if ((leg_feat & ~0x00000000000000FFULL) != 0) {
                g_last_error = "Unsupported global feature bitmask in legacy snapshot";
                if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_GLOBAL_FEATURE;
                return nullptr;
            }
        }
    }

    size_t dir_offset = static_cast<size_t>(snap->header.data_offset);
    if (ver == 9 || ver == 0x0009) {
        if (snap->header.footer_directory_offset > 0 && snap->header.footer_directory_offset < file_size) {
            dir_offset = static_cast<size_t>(snap->header.footer_directory_offset);
        } else if ((snap->header.required_features & IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG) && file_size >= 16) {
            impulse_footer_trailer_t trailer;
            std::memcpy(&trailer, raw + file_size - 16, 16);
            if (trailer.footer_magic == IMPULSE_MAGIC && file_size >= 16 + trailer.footer_length) {
                dir_offset = file_size - static_cast<size_t>(trailer.footer_length);
            }
        }
    }

    if (dir_offset > file_size || (dir_offset == file_size && (snap->header.domain_count > 0 || snap->header.relation_count > 0))) {
        g_last_error = "Directory offset out of bounds";
        if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
        return nullptr;
    }

    size_t cur = dir_offset;
    uint16_t domain_count = snap->header.domain_count;
    snap->domains.reserve(domain_count);
    snap->domain_names.reserve(domain_count);

    if (ver == 9 || ver == 0x0009) {
        // Read String Table Header & Pool
        if (cur + 4 > file_size) {
            if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
            return nullptr;
        }
        uint32_t string_table_bytes = 0;
        std::memcpy(&string_table_bytes, raw + cur, 4);
        cur += 4;

        if (cur + string_table_bytes > file_size) {
            if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
            return nullptr;
        }
        const char* string_pool = reinterpret_cast<const char*>(raw + cur);
        cur += string_table_bytes;

        if (string_table_bytes == 0 || string_pool[0] != '\0') {
            g_last_error = "Invalid String Table: string_table_bytes must be >= 1 and string_pool[0] must be '\\0'";
            if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
            return nullptr;
        }

        auto is_valid_utf8 = [](const char* str, size_t len) -> bool {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(str);
            size_t i = 0;
            while (i < len) {
                if (bytes[i] <= 0x7F) {
                    i++;
                } else if ((bytes[i] & 0xE0) == 0xC0) {
                    if (i + 1 >= len || (bytes[i + 1] & 0xC0) != 0x80) return false;
                    i += 2;
                } else if ((bytes[i] & 0xF0) == 0xE0) {
                    if (i + 2 >= len || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80) return false;
                    i += 3;
                } else if ((bytes[i] & 0xF8) == 0xF0) {
                    if (i + 3 >= len || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80 || (bytes[i + 3] & 0xC0) != 0x80) return false;
                    i += 4;
                } else {
                    return false;
                }
            }
            return true;
        };

        auto validate_and_get_string = [&](uint32_t name_off, std::string& out_str) -> impulse_status_t {
            if (string_table_bytes == 0) {
                if (name_off == 0) { out_str = ""; return IMPULSE_OK; }
                return IMPULSE_ERR_BUFFER_OVERFLOW;
            }
            if (name_off >= string_table_bytes) {
                return IMPULSE_ERR_BUFFER_OVERFLOW;
            }
            const char* str_start = string_pool + name_off;
            size_t max_len = string_table_bytes - name_off;
            const void* null_ptr = std::memchr(str_start, '\0', max_len);
            if (!null_ptr) {
                return IMPULSE_ERR_BUFFER_OVERFLOW;
            }
            size_t len = static_cast<const char*>(null_ptr) - str_start;
            if (!is_valid_utf8(str_start, len)) {
                return IMPULSE_ERR_INVALID_ARGUMENT;
            }
            out_str = std::string(str_start, len);
            return IMPULSE_OK;
        };

        size_t rem = cur % 128;
        if (rem != 0) cur += 128 - rem;

        // Domain Catalog
        for (uint16_t i = 0; i < domain_count; ++i) {
            if (cur + sizeof(impulse_domain_catalog_entry_t) > file_size) {
                g_last_error = "Buffer overflow parsing domain catalog";
                if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                return nullptr;
            }
            impulse_domain_catalog_entry_t dom_entry;
            std::memcpy(&dom_entry, raw + cur, sizeof(dom_entry));
            cur += sizeof(dom_entry);

            std::string dname;
            impulse_status_t str_status = validate_and_get_string(dom_entry.name_offset, dname);
            if (str_status != IMPULSE_OK) {
                g_last_error = "Corrupt domain name in Section 2 Shared String Table";
                if (out_status) *out_status = str_status;
                return nullptr;
            }

            snap->domains.push_back(dom_entry);
            snap->domain_names.push_back(dname);
        }

        rem = cur % 128;
        if (rem != 0) cur += 128 - rem;

        uint16_t rel_count = snap->header.relation_count;
        snap->relations.reserve(rel_count);
        snap->relation_attributes.resize(rel_count);

        for (uint16_t j = 0; j < rel_count; ++j) {
            if (cur + sizeof(impulse_relation_directory_entry_t) > file_size) {
                g_last_error = "Buffer overflow parsing relation directory";
                if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                return nullptr;
            }
            impulse_relation_directory_entry_t rel_entry;
            std::memcpy(&rel_entry, raw + cur, sizeof(rel_entry));
            cur += sizeof(rel_entry);

            uint16_t attr_count = rel_entry.attr_count;
            snap->relation_attributes[j].resize(attr_count);
            for (uint16_t a = 0; a < attr_count; ++a) {
                if (cur + sizeof(impulse_attribute_descriptor_t) > file_size) {
                    if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                    return nullptr;
                }
                std::memcpy(&snap->relation_attributes[j][a], raw + cur, sizeof(impulse_attribute_descriptor_t));
                cur += sizeof(impulse_attribute_descriptor_t);
            }

            if ((rel_entry.csr_row_off_offset > 0 && rel_entry.csr_row_off_offset % 128 != 0) ||
                (rel_entry.csr_col_idx_offset > 0 && rel_entry.csr_col_idx_offset % 128 != 0)) {
                g_last_error = "Unaligned section offset (must be 128B aligned)";
                if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE;
                return nullptr;
            }

            snap->relations.push_back(rel_entry);
        }
    } else {
        // Legacy v2.4 parsing
        for (uint16_t i = 0; i < domain_count; ++i) {
            impulse_domain_catalog_entry_t dom_hdr{};
            std::string dname;
            if (cur + 64 > file_size) {
                g_last_error = "Buffer overflow parsing legacy domain catalog";
                if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                return nullptr;
            }
            std::memcpy(&dom_hdr.domain_id, raw + cur, 2);
            dom_hdr.key_type = raw[cur + 2];
            uint32_t name_off = 0;
            uint16_t name_len = 0;
            std::memcpy(&name_off, raw + cur + 44, 4);
            std::memcpy(&name_len, raw + cur + 48, 2);
            cur += 64;

            if (name_len > 0) {
                if (name_off == 0 || name_off + name_len > file_size) {
                    g_last_error = "Domain name offset out of bounds";
                    if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                    return nullptr;
                }
                dname.assign(reinterpret_cast<const char*>(raw + name_off), name_len);
            }
            snap->domains.push_back(dom_hdr);
            snap->domain_names.push_back(dname);
        }

        size_t rem = cur % 128;
        if (rem != 0) cur += 128 - rem;

        uint16_t rel_count = snap->header.relation_count;
        snap->relations.reserve(rel_count);

        for (uint16_t j = 0; j < rel_count; ++j) {
            impulse_relation_directory_entry_t rel_entry{};
            size_t entry_size = (ver == 0x0204) ? 128 : 109;
            if (cur + entry_size > file_size) {
                g_last_error = "Buffer overflow parsing legacy relation directory";
                if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                return nullptr;
            }
            std::memcpy(&rel_entry.src_domain_id, raw + cur, 2);
            std::memcpy(&rel_entry.tgt_domain_id, raw + cur + 2, 2);
            rel_entry.encoding_id = raw[cur + 4];
            std::memcpy(&rel_entry.node_count, raw + cur + 5, 8);
            std::memcpy(&rel_entry.edge_count, raw + cur + 13, 8);
            std::memcpy(&rel_entry.section_features, raw + cur + 21, 8);
            std::memcpy(&rel_entry.csr_row_off_offset, raw + cur + 37, 8);
            std::memcpy(&rel_entry.csr_row_off_bytes, raw + cur + 45, 8);
            std::memcpy(&rel_entry.csr_col_idx_offset, raw + cur + 53, 8);
            std::memcpy(&rel_entry.csr_col_idx_bytes, raw + cur + 61, 8);
            uint64_t id_map_offset = 0;
            std::memcpy(&id_map_offset, raw + cur + 69, 8);
            if (id_map_offset > 0) {
                g_last_error = "Deprecated Section 4 ID mapping section present";
                if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE;
                return nullptr;
            }
            cur += entry_size;

            if ((rel_entry.csr_row_off_offset > 0 && rel_entry.csr_row_off_offset % 128 != 0) ||
                (rel_entry.csr_col_idx_offset > 0 && rel_entry.csr_col_idx_offset % 128 != 0)) {
                g_last_error = "Unaligned section offset (must be 128B aligned)";
                if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE;
                return nullptr;
            }

            snap->relations.push_back(rel_entry);
        }
    }

    // Read Footer Block Metadata if available at EOF - 16
    if (file_size >= 16) {
        size_t trailer_pos = file_size - 16;
        impulse_footer_trailer_t trailer;
        std::memcpy(&trailer, raw + trailer_pos, sizeof(trailer));

        if (trailer.footer_magic == IMPULSE_MAGIC && trailer.footer_length <= file_size) {
            size_t meta_offset = file_size - static_cast<size_t>(trailer.footer_length);
            size_t meta_bytes = static_cast<size_t>(trailer.footer_length) - 16;

            if (snap->header.footer_directory_offset > 0 && snap->header.footer_directory_offset == meta_offset) {
                meta_offset += static_cast<size_t>(snap->header.footer_directory_bytes);
                if (meta_bytes >= static_cast<size_t>(snap->header.footer_directory_bytes)) {
                    meta_bytes -= static_cast<size_t>(snap->header.footer_directory_bytes);
                }
            }

            if (meta_offset + meta_bytes <= file_size && meta_bytes >= 4) {
                size_t mcur = meta_offset;
                uint32_t count = 0;
                std::memcpy(&count, raw + mcur, 4);
                mcur += 4;

                for (uint32_t k = 0; k < count; ++k) {
                    if (mcur + 2 > meta_offset + meta_bytes) break;
                    uint16_t klen = 0;
                    std::memcpy(&klen, raw + mcur, 2);
                    mcur += 2;
                    if (mcur + klen > meta_offset + meta_bytes) break;
                    std::string key(reinterpret_cast<const char*>(raw + mcur), klen);
                    mcur += klen;

                    if (mcur + 4 > meta_offset + meta_bytes) break;
                    uint32_t vlen = 0;
                    std::memcpy(&vlen, raw + mcur, 4);
                    mcur += 4;
                    if (mcur + vlen > meta_offset + meta_bytes) break;
                    std::string val(reinterpret_cast<const char*>(raw + mcur), vlen);
                    mcur += vlen;

                    snap->metadata[key] = val;
                }
            }
        }
    }

    if (out_status) *out_status = IMPULSE_OK;
    return snap.release();
}

void impulse_snapshot_close(impulse_snapshot_t* snapshot) {
    if (!snapshot) return;
    if (snapshot->mmap_ptr && snapshot->file_size > 0) {
#ifdef _WIN32
        UnmapViewOfFile(snapshot->mmap_ptr);
#else
        ::munmap(snapshot->mmap_ptr, snapshot->file_size);
#endif
    }
    delete snapshot;
}

uint32_t impulse_snapshot_magic(const impulse_snapshot_t* snapshot) {
    return snapshot ? snapshot->header.magic : 0;
}

uint16_t impulse_snapshot_version(const impulse_snapshot_t* snapshot) {
    return snapshot ? snapshot->header.version : 0;
}

uint16_t impulse_snapshot_domain_count(const impulse_snapshot_t* snapshot) {
    return snapshot ? snapshot->header.domain_count : 0;
}

uint16_t impulse_snapshot_relation_count(const impulse_snapshot_t* snapshot) {
    return snapshot ? snapshot->header.relation_count : 0;
}

impulse_status_t impulse_snapshot_get_relation_entry(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    impulse_relation_directory_entry_t* out_entry
) {
    if (!snapshot || !out_entry || relation_index >= snapshot->relations.size()) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    *out_entry = snapshot->relations[relation_index];
    return IMPULSE_OK;
}

bool impulse_snapshot_is_reachable(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    uint64_t src_id,
    uint64_t tgt_id
) {
    if (!snapshot || relation_index >= snapshot->relations.size()) return false;
    const auto& rel = snapshot->relations[relation_index];

    if (src_id >= rel.node_count) return false;
    if (rel.csr_row_off_offset == 0 || rel.csr_col_idx_offset == 0) return false;

    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);
    if (rel.csr_row_off_offset + rel.csr_row_off_bytes > snapshot->file_size) return false;
    if (rel.csr_col_idx_offset + rel.csr_col_idx_bytes > snapshot->file_size) return false;

    const uint32_t* row_offsets = reinterpret_cast<const uint32_t*>(raw + rel.csr_row_off_offset);
    const uint32_t* col_indices = reinterpret_cast<const uint32_t*>(raw + rel.csr_col_idx_offset);

    size_t num_row_offsets = rel.csr_row_off_bytes / 4;
    if (src_id + 1 >= num_row_offsets) return false;

    uint32_t start_idx = row_offsets[src_id];
    uint32_t end_idx = row_offsets[src_id + 1];

    size_t num_col_indices = rel.csr_col_idx_bytes / 4;
    if (start_idx > end_idx || end_idx > num_col_indices) return false;

    for (uint32_t idx = start_idx; idx < end_idx; ++idx) {
        if (static_cast<uint64_t>(col_indices[idx]) == tgt_id) {
            return true;
        }
    }
    return false;
}

const void* impulse_snapshot_get_buffer(
    const impulse_snapshot_t* snapshot,
    uint64_t offset,
    uint64_t size
) {
    if (!snapshot || !snapshot->mmap_ptr) return nullptr;
    if (offset > snapshot->file_size || size > snapshot->file_size - offset) return nullptr;
    return static_cast<const uint8_t*>(snapshot->mmap_ptr) + offset;
}

impulse_status_t impulse_snapshot_get_metadata(const impulse_snapshot_t* snapshot, const char* key, char* out_val, size_t out_capacity) {
    if (!snapshot || !key || !out_val || out_capacity == 0) return IMPULSE_ERR_INVALID_ARGUMENT;
    auto it = snapshot->metadata.find(key);
    if (it == snapshot->metadata.end()) return IMPULSE_ERR_INVALID_ARGUMENT;

    if (it->second.size() >= out_capacity) return IMPULSE_ERR_BUFFER_OVERFLOW;
    std::memcpy(out_val, it->second.c_str(), it->second.size() + 1);
    return IMPULSE_OK;
}

impulse_writer_t* impulse_writer_create(const char* output_file_path, uint64_t global_features) {
    if (!output_file_path) return nullptr;
    auto w = std::make_unique<impulse_writer_t>();
    w->output_path = output_file_path;
    w->global_features = global_features | IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED;
    return w.release();
}

impulse_writer_t* impulse_writer_create_stream(impulse_write_fn write_cb, void* user_data, uint64_t global_features) {
    if (!write_cb) return nullptr;
    auto w = std::make_unique<impulse_writer_t>();
    w->write_cb = write_cb;
    w->user_data = user_data;
    w->global_features = global_features | IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED | IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG;
    return w.release();
}

impulse_status_t impulse_writer_add_domain(impulse_writer_t* writer, uint16_t domain_id, uint8_t key_type, const char* name) {
    if (!writer || !name) return IMPULSE_ERR_INVALID_ARGUMENT;
    impulse_domain_catalog_entry_t dom{};
    dom.domain_id = domain_id;
    dom.key_type = key_type;
    dom.reserved = 0;
    dom.name_offset = 0; // Set during finalize via string table
    dom.node_count = 0;

    writer->domains.push_back(dom);
    writer->domain_names.push_back(name);
    return IMPULSE_OK;
}

impulse_status_t impulse_writer_add_relation(
    impulse_writer_t* writer,
    uint16_t src_domain_id,
    uint16_t tgt_domain_id,
    uint8_t encoding_id,
    uint64_t node_count,
    uint64_t edge_count,
    uint64_t section_features,
    const void* row_offsets_data, uint64_t row_offsets_bytes,
    const void* col_indices_data, uint64_t col_indices_bytes
) {
    if (!writer || !row_offsets_data || !col_indices_data) return IMPULSE_ERR_INVALID_ARGUMENT;

    impulse_writer_relation rel;
    rel.relation_id = static_cast<uint16_t>(writer->relations.size());
    rel.src_domain_id = src_domain_id;
    rel.tgt_domain_id = tgt_domain_id;
    rel.encoding_id = encoding_id;
    rel.node_id_width = 4;
    rel.edge_index_width = 4;
    rel.node_count = node_count;
    rel.edge_count = edge_count;
    rel.section_features = section_features;

    rel.row_offsets.resize(row_offsets_bytes);
    std::memcpy(rel.row_offsets.data(), row_offsets_data, row_offsets_bytes);

    rel.col_indices.resize(col_indices_bytes);
    std::memcpy(rel.col_indices.data(), col_indices_data, col_indices_bytes);

    writer->relations.push_back(std::move(rel));
    return IMPULSE_OK;
}

impulse_status_t impulse_writer_add_attribute(
    impulse_writer_t* writer,
    uint16_t relation_index,
    const char* name,
    uint8_t type_code,
    uint32_t dimension,
    const void* data, uint64_t data_bytes,
    const void* offsets, uint64_t offsets_bytes
) {
    if (!writer || !name || relation_index >= writer->relations.size()) return IMPULSE_ERR_INVALID_ARGUMENT;
    auto& rel = writer->relations[relation_index];

    impulse_writer_attr attr;
    attr.name = name;
    attr.type_code = type_code;
    attr.dimension = dimension;

    if (data && data_bytes > 0) {
        attr.data.resize(data_bytes);
        std::memcpy(attr.data.data(), data, data_bytes);
    }
    if (offsets && offsets_bytes > 0) {
        attr.offsets.resize(offsets_bytes);
        std::memcpy(attr.offsets.data(), offsets, offsets_bytes);
    }

    rel.attributes.push_back(std::move(attr));
    return IMPULSE_OK;
}

impulse_status_t impulse_writer_set_metadata(impulse_writer_t* writer, const char* key, const char* value) {
    if (!writer || !key || !value) return IMPULSE_ERR_INVALID_ARGUMENT;
    writer->metadata[key] = value;
    return IMPULSE_OK;
}

impulse_status_t impulse_writer_finalize(impulse_writer_t* writer) {
    if (!writer) return IMPULSE_ERR_INVALID_ARGUMENT;

    bool footer_catalog = (writer->global_features & IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG) != 0 || (writer->write_cb != nullptr);
    if (footer_catalog) {
        writer->global_features |= IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG;
    }

    // Sort relations by src_domain_id primary, tgt_domain_id secondary
    std::sort(writer->relations.begin(), writer->relations.end(), [](const impulse_writer_relation& a, const impulse_writer_relation& b) {
        if (a.src_domain_id != b.src_domain_id) return a.src_domain_id < b.src_domain_id;
        return a.tgt_domain_id < b.tgt_domain_id;
    });
    for (size_t idx = 0; idx < writer->relations.size(); ++idx) {
        writer->relations[idx].relation_id = static_cast<uint16_t>(idx);
    }

    // 1. Build Shared String Table & String Pool
    std::vector<uint8_t> string_table;
    string_table.push_back('\0'); // Offset 0 = empty string ""
    std::unordered_map<std::string, uint32_t> string_map;
    string_map[""] = 0;

    auto get_or_add_string = [&](const std::string& str) -> uint32_t {
        if (str.empty()) return 0;
        auto it = string_map.find(str);
        if (it != string_map.end()) return it->second;
        uint32_t off = static_cast<uint32_t>(string_table.size());
        string_table.insert(string_table.end(), str.begin(), str.end());
        string_table.push_back('\0');
        string_map[str] = off;
        return off;
    };

    // Collect all strings
    for (size_t i = 0; i < writer->domain_names.size(); ++i) {
        writer->domains[i].name_offset = get_or_add_string(writer->domain_names[i]);
    }

    std::vector<uint8_t> dir_table;

    // String Table Header & Pool Blob
    uint32_t str_bytes = static_cast<uint32_t>(string_table.size());
    dir_table.resize(4);
    std::memcpy(dir_table.data(), &str_bytes, 4);
    dir_table.insert(dir_table.end(), string_table.begin(), string_table.end());

    align128(dir_table);

    // Domain Catalog Array
    for (const auto& dom : writer->domains) {
        size_t pos = dir_table.size();
        dir_table.resize(pos + sizeof(dom));
        std::memcpy(dir_table.data() + pos, &dom, sizeof(dom));
    }

    align128(dir_table);

    // Calculate directory table size including Relation Entries and Attribute Descriptors
    size_t rel_dir_size = 0;
    for (const auto& rel : writer->relations) {
        rel_dir_size += sizeof(impulse_relation_directory_entry_t);
        rel_dir_size += rel.attributes.size() * sizeof(impulse_attribute_descriptor_t);
    }
    size_t total_dir_table_len = dir_table.size() + rel_dir_size;
    size_t aligned_dir_table_len = (total_dir_table_len + 4095) & ~4095;

    uint64_t rel_blocks_base_offset = footer_catalog
        ? IMPULSE_DEFAULT_DATA_OFFSET
        : (IMPULSE_DEFAULT_DATA_OFFSET + aligned_dir_table_len);

    // 2. Serialize Relation Blocks
    std::vector<uint8_t> payload;
    for (auto& rel : writer->relations) {
        align4096(payload);

        align128(payload);
        uint64_t csr_row_off_offset = rel_blocks_base_offset + payload.size();
        uint64_t csr_row_off_bytes = rel.row_offsets.size();
        payload.insert(payload.end(), rel.row_offsets.begin(), rel.row_offsets.end());

        align128(payload);
        uint64_t csr_col_idx_offset = rel_blocks_base_offset + payload.size();
        uint64_t csr_col_idx_bytes = rel.col_indices.size();
        payload.insert(payload.end(), rel.col_indices.begin(), rel.col_indices.end());

        struct TempAttr {
            uint32_t name_off;
            uint64_t data_off, data_bytes;
            uint64_t offs_off, offs_bytes;
        };
        std::vector<TempAttr> temp_attrs;
        temp_attrs.reserve(rel.attributes.size());

        for (auto& attr : rel.attributes) {
            TempAttr ta{};
            ta.name_off = get_or_add_string(attr.name);
            align128(payload);
            ta.data_off = rel_blocks_base_offset + payload.size();
            ta.data_bytes = attr.data.size();
            payload.insert(payload.end(), attr.data.begin(), attr.data.end());

            if (!attr.offsets.empty()) {
                align128(payload);
                ta.offs_off = rel_blocks_base_offset + payload.size();
                ta.offs_bytes = attr.offsets.size();
                payload.insert(payload.end(), attr.offsets.begin(), attr.offsets.end());
            } else {
                ta.offs_off = 0;
                ta.offs_bytes = 0;
            }
            temp_attrs.push_back(ta);
        }

        // Relation Directory Entry
        impulse_relation_directory_entry_t entry{};
        entry.relation_id = rel.relation_id;
        entry.src_domain_id = rel.src_domain_id;
        entry.tgt_domain_id = rel.tgt_domain_id;
        entry.encoding_id = rel.encoding_id;
        entry.node_id_width = rel.node_id_width;
        entry.edge_index_width = rel.edge_index_width;
        entry.name_offset = get_or_add_string("");
        entry.node_count = rel.node_count;
        entry.edge_count = rel.edge_count;
        entry.section_features = rel.section_features;
        entry.csr_row_off_offset = csr_row_off_offset;
        entry.csr_row_off_bytes = csr_row_off_bytes;
        entry.csr_col_idx_offset = csr_col_idx_offset;
        entry.csr_col_idx_bytes = csr_col_idx_bytes;
        entry.csc_row_off_offset = 0;
        entry.csc_row_off_bytes = 0;
        entry.csc_col_idx_offset = 0;
        entry.csc_col_idx_bytes = 0;
        entry.attr_count = static_cast<uint16_t>(rel.attributes.size());

        dir_table.resize(dir_table.size() + sizeof(entry));
        std::memcpy(dir_table.data() + dir_table.size() - sizeof(entry), &entry, sizeof(entry));

        for (size_t a = 0; a < rel.attributes.size(); ++a) {
            const auto& attr = rel.attributes[a];
            const auto& ta = temp_attrs[a];

            impulse_attribute_descriptor_t desc{};
            desc.name_offset = ta.name_off;
            desc.type_code = attr.type_code;
            desc.reserved1 = 0;
            desc.reserved2 = 0;
            desc.dimension = attr.dimension;
            desc.data_offset = ta.data_off;
            desc.data_bytes = ta.data_bytes;
            desc.offsets_offset = ta.offs_off;
            desc.offsets_bytes = ta.offs_bytes;

            dir_table.resize(dir_table.size() + sizeof(desc));
            std::memcpy(dir_table.data() + dir_table.size() - sizeof(desc), &desc, sizeof(desc));
        }
    }

    dir_table.resize(aligned_dir_table_len, 0x00);

    std::vector<uint8_t> footer_payload;
    uint64_t footer_dir_offset = 0;
    uint64_t footer_dir_bytes = 0;

    if (footer_catalog) {
        align4096(payload);
        footer_dir_offset = IMPULSE_DEFAULT_DATA_OFFSET + payload.size();
        footer_dir_bytes = dir_table.size();
        footer_payload.insert(footer_payload.end(), dir_table.begin(), dir_table.end());
    } else {
        std::vector<uint8_t> combined;
        combined.insert(combined.end(), dir_table.begin(), dir_table.end());
        combined.insert(combined.end(), payload.begin(), payload.end());
        payload = std::move(combined);
    }

    // 3. Serialize Footer Block at EOF
    align4096(footer_payload);
    size_t footer_start = footer_payload.size();

    // Metadata Stream
    uint32_t meta_count = static_cast<uint32_t>(writer->metadata.size());
    size_t mpos = footer_payload.size();
    footer_payload.resize(mpos + 4);
    std::memcpy(footer_payload.data() + mpos, &meta_count, 4);

    for (const auto& kv : writer->metadata) {
        uint16_t klen = static_cast<uint16_t>(kv.first.size());
        mpos = footer_payload.size();
        footer_payload.resize(mpos + 2);
        std::memcpy(footer_payload.data() + mpos, &klen, 2);
        footer_payload.insert(footer_payload.end(), kv.first.begin(), kv.first.end());

        uint32_t vlen = static_cast<uint32_t>(kv.second.size());
        mpos = footer_payload.size();
        footer_payload.resize(mpos + 4);
        std::memcpy(footer_payload.data() + mpos, &vlen, 4);
        footer_payload.insert(footer_payload.end(), kv.second.begin(), kv.second.end());
    }

    // 16-Byte Footer Trailer
    uint64_t footer_len = static_cast<uint64_t>(footer_payload.size() + 16 - footer_start);
    impulse_footer_trailer_t trailer;
    trailer.footer_length = footer_len;
    trailer.spec_version = IMPULSE_SPEC_VERSION_PACKED;
    trailer.footer_magic = IMPULSE_MAGIC;

    mpos = footer_payload.size();
    footer_payload.resize(mpos + sizeof(trailer));
    std::memcpy(footer_payload.data() + mpos, &trailer, sizeof(trailer));

    // 4. Build Header Page 0 (4096 Bytes)
    impulse_snapshot_header_t header{};
    header.magic = IMPULSE_MAGIC;
    header.version = IMPULSE_SPEC_VERSION_PACKED;
    header.data_offset = IMPULSE_DEFAULT_DATA_OFFSET;
    header.domain_count = static_cast<uint16_t>(writer->domains.size());
    header.relation_count = static_cast<uint16_t>(writer->relations.size());
    header.timestamp_ms = 1700000000000ULL;
    header.required_features = writer->global_features;
    header.footer_directory_offset = footer_dir_offset;
    header.footer_directory_bytes = footer_dir_bytes;

    const uint8_t* header_raw = reinterpret_cast<const uint8_t*>(&header);
    header.header_checksum = compute_crc16(header_raw, 0x3E);

    if (writer->write_cb) {
        if (writer->write_cb(reinterpret_cast<const void*>(&header), sizeof(header), writer->user_data) != 0) {
            g_last_error = "Write callback returned error for header";
            return IMPULSE_ERR_IO_FAILURE;
        }
        if (!payload.empty()) {
            if (writer->write_cb(payload.data(), payload.size(), writer->user_data) != 0) {
                g_last_error = "Write callback returned error for relation payload";
                return IMPULSE_ERR_IO_FAILURE;
            }
        }
        if (!footer_payload.empty()) {
            if (writer->write_cb(footer_payload.data(), footer_payload.size(), writer->user_data) != 0) {
                g_last_error = "Write callback returned error for footer payload";
                return IMPULSE_ERR_IO_FAILURE;
            }
        }
        return IMPULSE_OK;
    } else {
        std::ofstream ofs(writer->output_path, std::ios::binary);
        if (!ofs) {
            g_last_error = "Failed to create output snapshot file";
            return IMPULSE_ERR_IO_FAILURE;
        }

        ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!payload.empty()) {
            ofs.write(reinterpret_cast<const char*>(payload.data()), payload.size());
        }
        if (!footer_payload.empty()) {
            ofs.write(reinterpret_cast<const char*>(footer_payload.data()), footer_payload.size());
        }
        return IMPULSE_OK;
    }
}

void impulse_writer_destroy(impulse_writer_t* writer) {
    delete writer;
}

impulse_delta_layer_t* impulse_delta_layer_create(uint16_t src_domain_id, uint16_t tgt_domain_id, const char* relation_name) {
    auto dl = std::make_unique<impulse_delta_layer_t>();
    dl->src_domain_id = src_domain_id;
    dl->tgt_domain_id = tgt_domain_id;
    dl->relation_name = relation_name ? relation_name : "";
    return dl.release();
}

impulse_status_t impulse_delta_layer_add_edge(impulse_delta_layer_t* delta, uint64_t src_node, uint64_t tgt_node) {
    if (!delta) return IMPULSE_ERR_INVALID_ARGUMENT;
    std::unique_lock<std::shared_mutex> lock(delta->mutex);
    uint64_t key = (src_node << 32) | (tgt_node & 0xFFFFFFFF);
    delta->tombstones.erase(key);
    delta->additions[src_node].push_back(tgt_node);
    return IMPULSE_OK;
}

impulse_status_t impulse_delta_layer_tombstone_edge(impulse_delta_layer_t* delta, uint64_t src_node, uint64_t tgt_node) {
    if (!delta) return IMPULSE_ERR_INVALID_ARGUMENT;
    std::unique_lock<std::shared_mutex> lock(delta->mutex);
    uint64_t key = (src_node << 32) | (tgt_node & 0xFFFFFFFF);
    delta->tombstones.insert(key);
    return IMPULSE_OK;
}

bool impulse_delta_layer_is_tombstoned(const impulse_delta_layer_t* delta, uint64_t src_node, uint64_t tgt_node) {
    if (!delta) return false;
    std::shared_lock<std::shared_mutex> lock(delta->mutex);
    uint64_t key = (src_node << 32) | (tgt_node & 0xFFFFFFFF);
    return delta->tombstones.find(key) != delta->tombstones.end();
}

void impulse_delta_layer_destroy(impulse_delta_layer_t* delta) {
    delete delta;
}

impulse_status_t impulse_snapshot_compact_to_file(
    const impulse_snapshot_t* base_snapshot,
    impulse_delta_layer_t** deltas,
    size_t delta_count,
    const char* output_file_path
) {
    (void)deltas;
    (void)delta_count;
    if (!base_snapshot || !output_file_path) return IMPULSE_ERR_INVALID_ARGUMENT;

    uint64_t gfeat = 0;
    if (base_snapshot->header.version == 9 || base_snapshot->header.version == 0x0009) {
        gfeat = base_snapshot->header.required_features;
    }
    impulse_writer_t* writer = impulse_writer_create(output_file_path, gfeat);
    if (!writer) return IMPULSE_ERR_IO_FAILURE;

    for (size_t i = 0; i < base_snapshot->domains.size(); ++i) {
        const auto& dom = base_snapshot->domains[i];
        const auto& name = base_snapshot->domain_names[i];
        impulse_writer_add_domain(writer, dom.domain_id, dom.key_type, name.c_str());
    }

    const uint8_t* raw = static_cast<const uint8_t*>(base_snapshot->mmap_ptr);

    for (size_t r = 0; r < base_snapshot->relations.size(); ++r) {
        const auto& rel = base_snapshot->relations[r];

        const void* row_ptr = raw + rel.csr_row_off_offset;
        const void* col_ptr = raw + rel.csr_col_idx_offset;

        impulse_writer_add_relation(
            writer, rel.src_domain_id, rel.tgt_domain_id, rel.encoding_id,
            rel.node_count, rel.edge_count, rel.section_features,
            row_ptr, rel.csr_row_off_bytes,
            col_ptr, rel.csr_col_idx_bytes
        );
    }

    for (const auto& kv : base_snapshot->metadata) {
        impulse_writer_set_metadata(writer, kv.first.c_str(), kv.second.c_str());
    }

    impulse_status_t st = impulse_writer_finalize(writer);
    impulse_writer_destroy(writer);
    return st;
}

impulse_status_t impulse_snapshot_compact_to_stream(
    const impulse_snapshot_t* base_snapshot,
    impulse_delta_layer_t** deltas,
    size_t delta_count,
    impulse_write_fn write_cb,
    void* user_data
) {
    (void)deltas;
    (void)delta_count;
    if (!base_snapshot || !write_cb) return IMPULSE_ERR_INVALID_ARGUMENT;

    uint64_t gfeat = IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG;
    if (base_snapshot->header.version == 9 || base_snapshot->header.version == 0x0009) {
        gfeat |= base_snapshot->header.required_features;
    }
    impulse_writer_t* writer = impulse_writer_create_stream(write_cb, user_data, gfeat);
    if (!writer) return IMPULSE_ERR_IO_FAILURE;

    for (size_t i = 0; i < base_snapshot->domains.size(); ++i) {
        const auto& dom = base_snapshot->domains[i];
        const auto& name = base_snapshot->domain_names[i];
        impulse_writer_add_domain(writer, dom.domain_id, dom.key_type, name.c_str());
    }

    const uint8_t* raw = static_cast<const uint8_t*>(base_snapshot->mmap_ptr);

    for (size_t r = 0; r < base_snapshot->relations.size(); ++r) {
        const auto& rel = base_snapshot->relations[r];

        const void* row_ptr = raw + rel.csr_row_off_offset;
        const void* col_ptr = raw + rel.csr_col_idx_offset;

        impulse_writer_add_relation(
            writer, rel.src_domain_id, rel.tgt_domain_id, rel.encoding_id,
            rel.node_count, rel.edge_count, rel.section_features,
            row_ptr, rel.csr_row_off_bytes,
            col_ptr, rel.csr_col_idx_bytes
        );
    }

    for (const auto& kv : base_snapshot->metadata) {
        impulse_writer_set_metadata(writer, kv.first.c_str(), kv.second.c_str());
    }

    impulse_status_t st = impulse_writer_finalize(writer);
    impulse_writer_destroy(writer);
    return st;
}

impulse_status_t impulse_snapshot_sample_neighbors(
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
) {
    if (!snapshot || !src_nodes || !out_src || !out_tgt || !out_count || relation_index >= snapshot->relations.size()) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    const auto& rel = snapshot->relations[relation_index];
    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);

    const uint32_t* row_offsets = reinterpret_cast<const uint32_t*>(raw + rel.csr_row_off_offset);
    const uint32_t* col_indices = reinterpret_cast<const uint32_t*>(raw + rel.csr_col_idx_offset);

    size_t total_written = 0;
    pcg32_fast rng(seed, 54u);

    for (size_t i = 0; i < num_nodes; ++i) {
        uint64_t u = src_nodes[i];
        if (u >= rel.node_count) continue;

        uint32_t start_idx = row_offsets[u];
        uint32_t end_idx = row_offsets[u + 1];
        uint32_t deg = end_idx - start_idx;

        if (deg == 0) continue;

        int num_to_sample = (k_samples < 0) ? static_cast<int>(deg) : std::min(static_cast<int>(deg), k_samples);

        for (int k = 0; k < num_to_sample; ++k) {
            if (total_written >= out_capacity) {
                *out_count = total_written;
                return IMPULSE_ERR_BUFFER_OVERFLOW;
            }

            uint32_t sample_idx = (k_samples < 0 || num_to_sample == static_cast<int>(deg))
                ? (start_idx + k)
                : (start_idx + rng.bounded(deg));

            out_src[total_written] = u;
            out_tgt[total_written] = static_cast<uint64_t>(col_indices[sample_idx]);
            total_written++;
        }
    }

    *out_count = total_written;
    return IMPULSE_OK;
}

uint64_t impulse_snapshot_max_node_count(const impulse_snapshot_t* snapshot) {
    if (!snapshot) return 0;
    uint64_t max_nodes = 0;
    for (const auto& d : snapshot->domains) {
        if (d.node_count > max_nodes) max_nodes = d.node_count;
    }
    for (const auto& r : snapshot->relations) {
        if (r.node_count > max_nodes) max_nodes = r.node_count;
    }
    return max_nodes;
}

impulse_status_t impulse_snapshot_get_relation_buffers(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    const uint32_t** out_offsets,
    const uint32_t** out_targets,
    uint64_t* out_node_count,
    uint64_t* out_edge_count
) {
    if (!snapshot || relation_index >= snapshot->relations.size() || !out_offsets || !out_targets) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    const auto& rel = snapshot->relations[relation_index];
    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);
    *out_offsets = reinterpret_cast<const uint32_t*>(raw + rel.csr_row_off_offset);
    *out_targets = reinterpret_cast<const uint32_t*>(raw + rel.csr_col_idx_offset);
    if (out_node_count) *out_node_count = rel.node_count;
    if (out_edge_count) *out_edge_count = rel.edge_count;
    return IMPULSE_OK;
}

impulse_status_t impulse_snapshot_get_attribute_buffers(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    uint16_t attribute_index,
    const void** out_data,
    uint64_t* out_data_bytes,
    const void** out_offsets,
    uint64_t* out_offsets_bytes,
    uint8_t* out_type_code,
    uint32_t* out_dimension
) {
    if (!snapshot || relation_index >= snapshot->relation_attributes.size()) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    const auto& attrs = snapshot->relation_attributes[relation_index];
    if (attribute_index >= attrs.size()) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    const auto& attr = attrs[attribute_index];
    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);
    if (out_data) *out_data = raw + attr.data_offset;
    if (out_data_bytes) *out_data_bytes = attr.data_bytes;
    if (out_offsets) *out_offsets = (attr.offsets_offset > 0) ? (raw + attr.offsets_offset) : nullptr;
    if (out_offsets_bytes) *out_offsets_bytes = attr.offsets_bytes;
    if (out_type_code) *out_type_code = attr.type_code;
    if (out_dimension) *out_dimension = attr.dimension;
    return IMPULSE_OK;
}

} // extern "C"
