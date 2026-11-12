/**
 * @file impulse_graph.cpp
 * @brief Zero-copy memory-mapped Impulse Binary Snapshot (.imps) Reader & Buffer Accessors.
 */

#include "impulse_graph_internal.hpp"

thread_local std::string g_last_error;

extern "C" {


const char* impulse_get_last_error(void) {
    return g_last_error.c_str();
}

static inline bool check_alignment(uint64_t offset, uint64_t align) noexcept {
    if (offset == 0 || align <= 1) return true;
    return (offset & (align - 1)) == 0;
}

static inline bool check_section_bounds(uint64_t offset, uint64_t bytes, uint64_t file_size, uint64_t align = 128) noexcept {
    if (bytes == 0) {
        if (offset == 0) return true;
        if (align > 0 && !check_alignment(offset, align)) return false;
        return offset <= file_size;
    }
    if (offset < 4096) return false;
    if (align > 0 && !check_alignment(offset, align)) return false;
    if (offset > file_size) return false;
    if (bytes > file_size - offset) return false;
    return true;
}

static inline bool safe_add(uint64_t a, uint64_t b, uint64_t* out) noexcept {
#if defined(__has_builtin) && __has_builtin(__builtin_add_overflow)
    return !__builtin_add_overflow(a, b, out);
#elif defined(__GNUC__) || defined(__clang__)
    return !__builtin_add_overflow(a, b, out);
#else
    if (a > UINT64_MAX - b) return false;
    *out = a + b;
    return true;
#endif
}

static inline bool safe_mul(uint64_t a, uint64_t b, uint64_t* out) noexcept {
#if defined(__has_builtin) && __has_builtin(__builtin_mul_overflow)
    return !__builtin_mul_overflow(a, b, out);
#elif defined(__GNUC__) || defined(__clang__)
    return !__builtin_mul_overflow(a, b, out);
#else
    if (a == 0 || b == 0) {
        *out = 0;
        return true;
    }
    if (a > UINT64_MAX / b) return false;
    *out = a * b;
    return true;
#endif
}

static inline bool compute_padded_validity_bytes(uint64_t row_count, uint64_t* out_raw_bm, uint64_t* out_padded_bm) noexcept {
    uint64_t words = (row_count >> 6) + (((row_count & 63ULL) != 0ULL) ? 1ULL : 0ULL);
    uint64_t raw_bm = 0;
    if (!safe_mul(words, 8ULL, &raw_bm)) return false;
    uint64_t padded_bm = 0;
    if (!safe_add(raw_bm, 127ULL, &padded_bm)) return false;
    padded_bm &= ~127ULL;
    if (out_raw_bm) *out_raw_bm = raw_bm;
    if (out_padded_bm) *out_padded_bm = padded_bm;
    return true;
}

static inline uint64_t fixed_attr_type_width(uint8_t base_type) noexcept {
    switch (base_type) {
        case 0x01:
        case 0x0A:
        case 0x11:
            return 1ULL;
        case 0x02:
        case 0x05:
        case 0x12:
            return 2ULL;
        case 0x03:
        case 0x06:
        case 0x13:
            return 4ULL;
        case 0x04:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0D:
        case 0x14:
            return 8ULL;
        case 0x0E:
            return 16ULL;
        default:
            return 0ULL;
    }
}

static impulse_status_t validate_attribute_payload(
    const uint8_t* raw,
    uint64_t file_size,
    const impulse_attribute_descriptor_t& attr,
    uint64_t row_count,
    bool is_domain_attr,
    bool is_v10,
    uint64_t req_align,
    std::string& err_msg
) noexcept {
    if (!check_section_bounds(attr.data_offset, attr.data_bytes, file_size, req_align) ||
        !check_section_bounds(attr.offsets_offset, attr.offsets_bytes, file_size, req_align)) {
        err_msg = "Attribute section bounds check failed";
        return IMPULSE_ERR_BUFFER_OVERFLOW;
    }

    uint8_t base_type = attr.type_code & 0x7F;
    bool is_nullable = (attr.type_code & 0x80) != 0;
    uint64_t payload_bytes = attr.data_bytes;

    if (is_v10 && is_nullable && attr.data_offset > 0 && row_count > 0) {
        uint64_t bm_rows = is_domain_attr ? row_count : 1ULL;
        uint64_t padded_bm = 0;
        if (!compute_padded_validity_bytes(bm_rows, nullptr, &padded_bm) || attr.data_bytes < padded_bm) {
            err_msg = "Nullable attribute data_bytes smaller than 128B-padded validity bitmap";
            return IMPULSE_ERR_BUFFER_OVERFLOW;
        }
        if (is_domain_attr) {
            payload_bytes = attr.data_bytes - padded_bm;
        }
    }

    if (base_type == 0x0B || base_type == 0x0C) {
        // Variable-length strings/bytes when offsets_offset > 0 (or dimension == 0 on non-empty domain attr);
        // fixed-width inline strings/bytes when offsets_offset == 0 && dimension > 0.
        bool is_var_len = (attr.offsets_offset > 0) || (is_domain_attr && attr.dimension == 0 && attr.data_offset > 0);
        if (is_var_len && row_count > 0) {
            if (attr.offsets_offset == 0) {
                err_msg = "Variable-length attribute missing offsets_offset";
                return IMPULSE_ERR_BUFFER_OVERFLOW;
            }
            if (is_domain_attr) {
                uint64_t rows_plus_one = 0;
                uint64_t req_off_bytes = 0;
                if (!safe_add(row_count, 1ULL, &rows_plus_one) ||
                    !safe_mul(rows_plus_one, 4ULL, &req_off_bytes) ||
                    attr.offsets_bytes < req_off_bytes) {
                    err_msg = "Variable-length attribute offsets_bytes smaller than (row_count + 1) * 4";
                    return IMPULSE_ERR_BUFFER_OVERFLOW;
                }
            } else if (attr.offsets_bytes < 4ULL || (attr.offsets_bytes & 3ULL) != 0ULL) {
                err_msg = "Variable-length relation attribute offsets_bytes invalid";
                return IMPULSE_ERR_BUFFER_OVERFLOW;
            }
            uint64_t num_entries = is_domain_attr ? row_count : ((attr.offsets_bytes / 4ULL) - 1ULL);
            const auto* offs = reinterpret_cast<const uint32_t*>(raw + attr.offsets_offset);
            if (offs[0] != 0) {
                err_msg = "Variable-length attribute offsets[0] must be 0";
                return IMPULSE_ERR_INVALID_ARGUMENT;
            }
            uint32_t prev = 0;
            for (uint64_t r = 0; r < num_entries; ++r) {
                uint32_t cur_off = offs[r + 1];
                if (cur_off < prev) {
                    err_msg = "Variable-length attribute offsets must be monotonically non-decreasing";
                    return IMPULSE_ERR_INVALID_ARGUMENT;
                }
                prev = cur_off;
            }
            uint64_t max_payload = (attr.data_offset > 0) ? payload_bytes : 0ULL;
            if (static_cast<uint64_t>(prev) > max_payload) {
                err_msg = "Variable-length attribute terminal offset exceeds payload byte size";
                return IMPULSE_ERR_BUFFER_OVERFLOW;
            }
        } else if (is_domain_attr && is_v10 && attr.data_offset > 0 && row_count > 0 && attr.dimension > 0) {
            uint64_t req_payload = 0;
            if (!safe_mul(row_count, static_cast<uint64_t>(attr.dimension), &req_payload) || payload_bytes < req_payload) {
                err_msg = "Fixed-width string/bytes attribute payload smaller than row_count * dimension";
                return IMPULSE_ERR_BUFFER_OVERFLOW;
            }
        }
    } else if (is_domain_attr && is_v10 && attr.data_offset > 0 && row_count > 0) {
        uint64_t type_w = fixed_attr_type_width(base_type);
        if (type_w == 0ULL) {
            err_msg = "Unsupported fixed-width attribute type_code";
            return IMPULSE_ERR_INVALID_ARGUMENT;
        }
        uint64_t dim = (attr.dimension == 0) ? 1ULL : static_cast<uint64_t>(attr.dimension);
        uint64_t elem_bytes = 0;
        uint64_t req_payload = 0;
        if (!safe_mul(type_w, dim, &elem_bytes) ||
            !safe_mul(row_count, elem_bytes, &req_payload) ||
            payload_bytes < req_payload) {
            err_msg = "Fixed-width attribute payload smaller than row_count * elem_bytes";
            return IMPULSE_ERR_BUFFER_OVERFLOW;
        }
    }

    return IMPULSE_OK;
}

static std::string resolve_snapshot_path(const char* file_path) {
    if (!file_path || file_path[0] == '\0') return "";
    std::string path_str(file_path);

#ifndef _WIN32
    struct stat st;
    if (::stat(path_str.c_str(), &st) == 0) {
        return path_str;
    }

    const char* env_dir = std::getenv("IMPULSE_DATASETS_DIR");
    if (!env_dir || env_dir[0] == '\0') {
        env_dir = std::getenv("IMPULSEGRAPH_DATA_DIR");
    }
    if (!env_dir || env_dir[0] == '\0') {
        env_dir = std::getenv("IMPULSE_DATA_DIR");
    }

    if (env_dir) {
        std::string base(env_dir);
        if (!base.empty() && base.back() != '/') {
            base += '/';
        }

        std::string candidate1 = base + path_str;
        if (::stat(candidate1.c_str(), &st) == 0) {
            return candidate1;
        }

        size_t dot_pos = path_str.find('.');
        if (dot_pos != std::string::npos) {
            std::string dataset_name = path_str.substr(0, dot_pos);
            std::string candidate2 = base + dataset_name + "/" + path_str;
            if (::stat(candidate2.c_str(), &st) == 0) {
                return candidate2;
            }
        }
    }

    // Check standard local relative search paths
    const char* local_prefixes[] = {"datasets/", "../datasets/", "../../datasets/", "../../../datasets/"};
    for (const char* prefix : local_prefixes) {
        std::string local_cand1 = std::string(prefix) + path_str;
        if (::stat(local_cand1.c_str(), &st) == 0) {
            return local_cand1;
        }
        size_t dot_pos = path_str.find('.');
        if (dot_pos != std::string::npos) {
            std::string dataset_name = path_str.substr(0, dot_pos);
            std::string local_cand2 = std::string(prefix) + dataset_name + "/" + path_str;
            if (::stat(local_cand2.c_str(), &st) == 0) {
                return local_cand2;
            }
        }
    }
#endif

    return path_str;
}

impulse_snapshot_t* impulse_snapshot_open(const char* file_path, impulse_status_t* out_status) {
    if (!file_path) {
        if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
        return nullptr;
    }
    if (impulse_faultsim_step() != 0) {
        g_last_error = "Simulated I/O fault in impulse_snapshot_open";
        if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
        return nullptr;
    }

    std::string resolved = resolve_snapshot_path(file_path);
    const char* target_path = resolved.c_str();

#ifdef _WIN32
    HANDLE hFile = CreateFileA(target_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
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
    int fd = ::open(target_path, O_RDONLY);
    if (fd < 0) {
        g_last_error = "Failed to open file";
        if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
        return nullptr;
    }
    FdGuard fd_guard(fd);
    struct stat st;
    if (::fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size < 0) {
        g_last_error = "Snapshot path is not a readable regular file";
        if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
        return nullptr;
    }
    size_t file_size = static_cast<size_t>(st.st_size);
    IMPULSE_TESTCASE(file_size == 4096);
    IMPULSE_TESTCASE(file_size < 4096);
    if (file_size < 4096) {
        IMPULSE_WELL_FORMED_ASSERT(file_size >= 4096);
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

    // Asynchronously prefetch the graph into RAM to eliminate soft page fault latency
#if defined(MADV_WILLNEED)
    ::madvise(mmap_ptr, file_size, MADV_WILLNEED);
#endif
#endif

    const uint8_t* raw = static_cast<const uint8_t*>(mmap_ptr);

    auto snap = std::make_unique<impulse_snapshot_t>();
    snap->mmap_ptr = mmap_ptr;
    snap->file_size = file_size;

    std::memcpy(&snap->header, raw, sizeof(impulse_snapshot_header_t));

    IMPULSE_WELL_FORMED_ASSERT(snap->header.magic == IMPULSE_MAGIC);
    if (snap->header.magic != IMPULSE_MAGIC) {
        g_last_error = "Invalid magic bytes";
        if (out_status) *out_status = IMPULSE_ERR_INVALID_MAGIC;
        return nullptr;
    }

    uint16_t ver = snap->header.version;
    const bool is_v09 = (ver == 9 || ver == 0x0009);
    const bool is_v10 = (ver == 10 || ver == 0x000A);
    const bool is_modern = (is_v09 || is_v10);
    if (!is_modern && ver != 2 && ver != 0x0204) {
        g_last_error = "Unsupported version";
        if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_VERSION;
        return nullptr;
    }

    if (is_modern) {
        const uint64_t known_features = IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED |
                                        IMPULSE_GLOBAL_FEAT_CRYPTO_SIGNED |
                                        IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG;
        if ((snap->header.required_features & ~known_features) != 0) {
            g_last_error = "Unsupported global feature bitmask";
            if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_GLOBAL_FEATURE;
            return nullptr;
        }

        uint16_t expected_crc = compute_crc16(raw, 0x3E);
        IMPULSE_WELL_FORMED_ASSERT(expected_crc == snap->header.header_checksum);
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
    if (is_modern) {
        if (snap->header.footer_directory_offset > 0) {
            if (snap->header.footer_directory_offset > file_size ||
                snap->header.footer_directory_bytes > file_size - snap->header.footer_directory_offset) {
                g_last_error = "Footer directory offset or byte length out of bounds";
                if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                return nullptr;
            }
            dir_offset = static_cast<size_t>(snap->header.footer_directory_offset);
        } else if ((snap->header.required_features & IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG) && file_size >= 16) {
            impulse_footer_trailer_t trailer;
            std::memcpy(&trailer, raw + file_size - 16, 16);
            if (trailer.footer_magic == IMPULSE_MAGIC) {
                if (trailer.footer_length < 16 || trailer.footer_length > file_size) {
                    g_last_error = "Footer trailer length out of bounds";
                    if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                    return nullptr;
                }
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
    snap->domain_attributes.resize(domain_count);
    snap->domain_attribute_names.resize(domain_count);

    if (is_modern) {
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

        IMPULSE_TESTCASE(string_table_bytes == 1);
        IMPULSE_WELL_FORMED_ASSERT(string_table_bytes >= 1 && string_pool[0] == '\0' && string_pool[string_table_bytes - 1] == '\0');
        if (string_table_bytes == 0 || string_pool[0] != '\0' || string_pool[string_table_bytes - 1] != '\0') {
            g_last_error = "Invalid String Table: string_table_bytes must be >= 1, string_pool[0] must be '\\0', and must be NUL-terminated";
            if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
            return nullptr;
        }

        // Strict RFC-3629 UTF-8 validation (rejects C0/C1/E0/F0 overlongs, UTF-16 surrogates, and > U+10FFFF)
        auto is_valid_utf8 = [](const char* str, size_t len) -> bool {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(str);
            size_t i = 0;
            while (i < len) {
                uint8_t b0 = bytes[i];
                if (b0 <= 0x7F) {
                    i += 1;
                } else if (b0 >= 0xC2 && b0 <= 0xDF) {
                    if (i + 1 >= len || (bytes[i + 1] & 0xC0) != 0x80) return false;
                    i += 2;
                } else if (b0 >= 0xE0 && b0 <= 0xEF) {
                    if (i + 2 >= len) return false;
                    uint8_t b1 = bytes[i + 1];
                    uint8_t b2 = bytes[i + 2];
                    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return false;
                    if (b0 == 0xE0 && b1 < 0xA0) return false; // Overlong 3-byte
                    if (b0 == 0xED && b1 >= 0xA0) return false; // UTF-16 surrogate U+D800..U+DFFF
                    i += 3;
                } else if (b0 >= 0xF0 && b0 <= 0xF4) {
                    if (i + 3 >= len) return false;
                    uint8_t b1 = bytes[i + 1];
                    uint8_t b2 = bytes[i + 2];
                    uint8_t b3 = bytes[i + 3];
                    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return false;
                    if (b0 == 0xF0 && b1 < 0x90) return false; // Overlong 4-byte
                    if (b0 == 0xF4 && b1 >= 0x90) return false; // > U+10FFFF
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

        // Domain Catalog (v0.10.0 = 32B + inline 44B attr descriptors; v0.9.0 = 16B)
        for (uint16_t i = 0; i < domain_count; ++i) {
            impulse_domain_catalog_entry_t dom_entry{};
            if (is_v10) {
                if (cur + sizeof(impulse_domain_catalog_entry_v0_10_t) > file_size) {
                    g_last_error = "Buffer overflow parsing v0.10.0 domain catalog";
                    if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                    return nullptr;
                }
                std::memcpy(&dom_entry, raw + cur, sizeof(impulse_domain_catalog_entry_v0_10_t));
                cur += sizeof(impulse_domain_catalog_entry_v0_10_t);
                if (dom_entry.attr_count == 0 && dom_entry.key_attr_index == 0) {
                    dom_entry.key_attr_index = 0xFFFF;
                }
                if (dom_entry.key_attr_index != 0xFFFF && dom_entry.key_attr_index >= dom_entry.attr_count) {
                    g_last_error = "Domain key_attr_index out of bounds for domain attr_count";
                    if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                    return nullptr;
                }
            } else {
                if (cur + sizeof(impulse_domain_catalog_entry_v0_9_t) > file_size) {
                    g_last_error = "Buffer overflow parsing v0.9.0 domain catalog";
                    if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                    return nullptr;
                }
                impulse_domain_catalog_entry_v0_9_t dom_v09{};
                std::memcpy(&dom_v09, raw + cur, sizeof(dom_v09));
                cur += sizeof(dom_v09);
                dom_entry.domain_id = dom_v09.domain_id;
                dom_entry.key_type = dom_v09.key_type;
                dom_entry.reserved1 = dom_v09.reserved;
                dom_entry.name_offset = dom_v09.name_offset;
                dom_entry.node_count = dom_v09.node_count;
                dom_entry.attr_count = 0;
                dom_entry.key_attr_index = 0xFFFF;
            }

            std::string dname;
            impulse_status_t str_status = validate_and_get_string(dom_entry.name_offset, dname);
            if (str_status != IMPULSE_OK) {
                g_last_error = "Corrupt domain name in Section 2 Shared String Table";
                if (out_status) *out_status = str_status;
                return nullptr;
            }

            if (is_v10 && dom_entry.attr_count > 0) {
                uint16_t d_attr_count = dom_entry.attr_count;
                snap->domain_attributes[i].resize(d_attr_count);
                snap->domain_attribute_names[i].resize(d_attr_count);
                for (uint16_t a = 0; a < d_attr_count; ++a) {
                    if (cur + sizeof(impulse_attribute_descriptor_t) > file_size) {
                        g_last_error = "Buffer overflow parsing domain attribute descriptor";
                        if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                        return nullptr;
                    }
                    std::memcpy(&snap->domain_attributes[i][a], raw + cur, sizeof(impulse_attribute_descriptor_t));
                    cur += sizeof(impulse_attribute_descriptor_t);

                    std::string aname;
                    impulse_status_t astr_st = validate_and_get_string(snap->domain_attributes[i][a].name_offset, aname);
                    if (astr_st != IMPULSE_OK) {
                        g_last_error = "Corrupt domain attribute name in Section 2 Shared String Table";
                        if (out_status) *out_status = astr_st;
                        return nullptr;
                    }
                    snap->domain_attribute_names[i][a] = aname;

                    if (!check_alignment(snap->domain_attributes[i][a].data_offset, 128) ||
                        !check_alignment(snap->domain_attributes[i][a].offsets_offset, 128)) {
                        g_last_error = "Unaligned domain attribute offset (must be 128B aligned)";
                        if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE;
                        return nullptr;
                    }
                    impulse_status_t attr_st = validate_attribute_payload(
                        raw, file_size, snap->domain_attributes[i][a], dom_entry.node_count, true, is_v10, 128, g_last_error
                    );
                    if (attr_st != IMPULSE_OK) {
                        if (out_status) *out_status = attr_st;
                        return nullptr;
                    }
                }
            }

            snap->domains.push_back(dom_entry);
            snap->domain_names.push_back(dname);
        }

        rem = cur % 128;
        if (rem != 0) cur += 128 - rem;

        uint16_t rel_count = snap->header.relation_count;
        snap->relations.reserve(rel_count);
        snap->relation_names.reserve(rel_count);
        snap->relation_attributes.resize(rel_count);
        snap->relation_attribute_names.resize(rel_count);

        for (uint16_t j = 0; j < rel_count; ++j) {
            if (cur + sizeof(impulse_relation_directory_entry_t) > file_size) {
                g_last_error = "Buffer overflow parsing relation directory";
                if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                return nullptr;
            }
            impulse_relation_directory_entry_t rel_entry;
            std::memcpy(&rel_entry, raw + cur, sizeof(rel_entry));
            cur += sizeof(rel_entry);

            std::string rname;
            impulse_status_t rstr_st = validate_and_get_string(rel_entry.name_offset, rname);
            if (rstr_st != IMPULSE_OK) {
                g_last_error = "Corrupt relation name in Section 2 Shared String Table";
                if (out_status) *out_status = rstr_st;
                return nullptr;
            }
            snap->relation_names.push_back(rname);

            uint16_t attr_count = rel_entry.attr_count;
            snap->relation_attributes[j].resize(attr_count);
            snap->relation_attribute_names[j].resize(attr_count);
            for (uint16_t a = 0; a < attr_count; ++a) {
                if (cur + sizeof(impulse_attribute_descriptor_t) > file_size) {
                    if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                    return nullptr;
                }
                std::memcpy(&snap->relation_attributes[j][a], raw + cur, sizeof(impulse_attribute_descriptor_t));
                std::string aname;
                impulse_status_t rastr_st = validate_and_get_string(snap->relation_attributes[j][a].name_offset, aname);
                if (rastr_st != IMPULSE_OK) {
                    g_last_error = "Corrupt relation attribute name in Section 2 Shared String Table";
                    if (out_status) *out_status = rastr_st;
                    return nullptr;
                }
                snap->relation_attribute_names[j][a] = aname;
                cur += sizeof(impulse_attribute_descriptor_t);
            }

            if (!check_alignment(rel_entry.csr_row_off_offset, 128) ||
                !check_alignment(rel_entry.csr_col_idx_offset, 128) ||
                !check_alignment(rel_entry.csc_row_off_offset, 128) ||
                !check_alignment(rel_entry.csc_col_idx_offset, 128)) {
                g_last_error = "Unaligned section offset (must be 128B aligned)";
                if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE;
                return nullptr;
            }

            for (uint16_t a = 0; a < attr_count; ++a) {
                if (!check_alignment(snap->relation_attributes[j][a].data_offset, 128) ||
                    !check_alignment(snap->relation_attributes[j][a].offsets_offset, 128)) {
                    g_last_error = "Unaligned attribute offset (must be 128B aligned)";
                    if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE;
                    return nullptr;
                }
            }

            snap->relations.push_back(rel_entry);
        }

        rem = cur % 128;
        if (rem != 0) cur += 128 - rem;

        uint16_t idx_count = snap->header.index_count;
        snap->indexes.reserve(idx_count);
        snap->index_names.resize(idx_count);

        for (uint16_t k = 0; k < idx_count; ++k) {
            if (cur + sizeof(impulse_index_directory_entry_t) > file_size) {
                g_last_error = "Buffer overflow parsing index directory";
                if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                return nullptr;
            }
            impulse_index_directory_entry_t idx_entry;
            std::memcpy(&idx_entry, raw + cur, sizeof(idx_entry));
            cur += sizeof(idx_entry);

            std::string iname;
            impulse_status_t str_status = validate_and_get_string(idx_entry.name_offset, iname);
            if (str_status != IMPULSE_OK) {
                g_last_error = "Corrupt secondary index name in Section 2 Shared String Table";
                if (out_status) *out_status = str_status;
                return nullptr;
            }
            snap->index_names[k] = iname;

            if (idx_entry.data_offset > 0 && idx_entry.data_offset % 128 != 0) {
                g_last_error = "Unaligned index data offset (must be 128B aligned)";
                if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE;
                return nullptr;
            }

            snap->indexes.push_back(idx_entry);
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
                uint64_t name_end = 0;
                if (name_off == 0 || !safe_add(static_cast<uint64_t>(name_off), static_cast<uint64_t>(name_len), &name_end) ||
                    name_end > file_size) {
                    g_last_error = "Domain name offset out of bounds";
                    if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                    return nullptr;
                }
                dname.assign(reinterpret_cast<const char*>(raw + name_off), name_len);
            }
            snap->domains.push_back(dom_hdr);
            snap->domain_names.push_back(dname);
        }

        size_t rem = cur % 64;
        if (rem != 0) cur += 64 - rem;

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
            uint64_t aux_offset = 0;
            uint64_t aux_bytes = 0;
            std::memcpy(&aux_offset, raw + cur + 69, 8);
            std::memcpy(&aux_bytes, raw + cur + 77, 8);

            if (aux_offset > 0) {
                if (aux_offset > file_size || aux_bytes > file_size - aux_offset) {
                    g_last_error = "Legacy auxiliary directory offset or byte length out of bounds";
                    if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                    return nullptr;
                }
                if (aux_bytes >= 24) {
                    size_t aux_count = aux_bytes / 24;
                    for (size_t a = 0; a < aux_count; ++a) {
                        size_t a_cur = aux_offset + a * 24;
                        uint16_t sec_type = 0;
                        std::memcpy(&sec_type, raw + a_cur, 2);
                        uint64_t sec_off = 0;
                        std::memcpy(&sec_off, raw + a_cur + 8, 8);
                        uint64_t sec_size = 0;
                        std::memcpy(&sec_size, raw + a_cur + 16, 8);

                        if (sec_type == 0x0001) { // CSC Row Offsets
                            rel_entry.csc_row_off_offset = sec_off;
                            rel_entry.csc_row_off_bytes = sec_size;
                        } else if (sec_type == 0x0002) { // CSC Col Targets
                            rel_entry.csc_col_idx_offset = sec_off;
                            rel_entry.csc_col_idx_bytes = sec_size;
                        } else if (sec_type != 0) {
                            g_last_error = "Unsupported auxiliary section feature type";
                            if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE;
                            return nullptr;
                        }
                    }
                }
            }
            cur += entry_size;

            if ((rel_entry.csr_row_off_offset > 0 && rel_entry.csr_row_off_offset % 64 != 0) ||
                (rel_entry.csr_col_idx_offset > 0 && rel_entry.csr_col_idx_offset % 64 != 0) ||
                (rel_entry.csc_row_off_offset > 0 && rel_entry.csc_row_off_offset % 64 != 0) ||
                (rel_entry.csc_col_idx_offset > 0 && rel_entry.csc_col_idx_offset % 64 != 0)) {
                g_last_error = "Unaligned section offset in legacy snapshot (must be 64B aligned)";
                if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE;
                return nullptr;
            }

            snap->relations.push_back(rel_entry);
        }
    }

    uint64_t req_align = is_modern ? 128 : 64;
    std::vector<std::vector<uint16_t>> virtual_children_graph(snap->relations.size());
    bool has_any_virtual_relation = false;

    for (size_t r = 0; r < snap->relations.size(); ++r) {
        const auto& rel = snap->relations[r];
        if (is_modern && !snap->domains.empty()) {
            if (rel.src_domain_id >= snap->domains.size() || rel.tgt_domain_id >= snap->domains.size()) {
                g_last_error = "Relation src_domain_id or tgt_domain_id out of bounds for domain catalog";
                if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                return nullptr;
            }
        }
        if (rel.node_id_width != 0 && rel.node_id_width != 2 && rel.node_id_width != 4 && rel.node_id_width != 8 &&
            rel.node_id_width != 16 && rel.node_id_width != 32 && rel.node_id_width != 64) {
            g_last_error = "Unsupported relation node_id_width";
            if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE;
            return nullptr;
        }
        if (rel.edge_index_width != 0 && rel.edge_index_width != 4 && rel.edge_index_width != 8 &&
            rel.edge_index_width != 32 && rel.edge_index_width != 64) {
            g_last_error = "Unsupported relation edge_index_width";
            if (out_status) *out_status = IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE;
            return nullptr;
        }

        uint64_t src_node_count = rel.node_count;
        uint64_t tgt_node_count = rel.node_count;
        if (rel.src_domain_id < snap->domains.size()) {
            src_node_count = snap->domains[rel.src_domain_id].node_count;
        }
        if (rel.tgt_domain_id < snap->domains.size()) {
            tgt_node_count = snap->domains[rel.tgt_domain_id].node_count;
        }

        bool is_edge64 = (rel.edge_index_width == 8 || rel.edge_index_width == 64);
        bool is_node16 = (rel.node_id_width == 2 || rel.node_id_width == 16);
        bool is_node64 = (rel.node_id_width == 8 || rel.node_id_width == 64);

        uint64_t edge_width_bytes = is_edge64 ? 8 : 4;
        uint64_t tgt_node_width_bytes = is_node16 ? 2 : (is_node64 ? 8 : 4);
        uint64_t src_node_width_bytes = tgt_node_width_bytes;
        uint64_t max_node_sentinel = is_node16 ? 0xFFFFULL : (is_node64 ? UINT64_MAX : 0xFFFFFFFFULL);

        bool is_virtual = is_modern && ((rel.section_features & 0x04ULL) != 0);
        uint64_t multiplicity = is_modern ? ((rel.section_features >> 3) & 0x3ULL) : 0ULL;
        bool flat_forward = ((multiplicity == 1ULL || multiplicity == 3ULL) &&
                             rel.csr_row_off_offset == 0 && rel.csr_row_off_bytes == 0);
        bool flat_reverse = ((multiplicity == 2ULL || multiplicity == 3ULL) &&
                             rel.csc_row_off_offset == 0 && rel.csc_row_off_bytes == 0 &&
                             rel.csc_col_idx_bytes > 0);

        // CSR section bounds
        if (!check_section_bounds(rel.csr_row_off_offset, rel.csr_row_off_bytes, file_size, req_align) ||
            !check_section_bounds(rel.csr_col_idx_offset, rel.csr_col_idx_bytes, file_size, req_align)) {
            g_last_error = "CSR section bounds or alignment check failed";
            if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
            return nullptr;
        }

        if (is_virtual) {
            has_any_virtual_relation = true;
            uint16_t comp_count = 0;
            std::memcpy(&comp_count, &rel.reserved2[0], sizeof(uint16_t));
            uint16_t tag_or_first = 0;
            std::memcpy(&tag_or_first, &rel.reserved2[2], sizeof(uint16_t));

            if (rel.csr_col_idx_offset > 0 && (rel.csr_col_idx_bytes & 1ULL) != 0ULL) {
                g_last_error = "Virtual super-relation overflow component list must have an even byte length";
                if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                return nullptr;
            }
            if (comp_count > 0) {
                bool use_overflow_slab = (comp_count > 10) ||
                    (tag_or_first == 0xFFFF && rel.csr_col_idx_bytes >= static_cast<uint64_t>(comp_count) * 2ULL);
                if (use_overflow_slab) {
                    if (rel.csr_col_idx_offset == 0 || rel.csr_col_idx_bytes < static_cast<uint64_t>(comp_count) * 2ULL) {
                        g_last_error = "Virtual super-relation overflow component list truncated or missing";
                        if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                        return nullptr;
                    }
                    const auto* ext_ids = reinterpret_cast<const uint16_t*>(raw + rel.csr_col_idx_offset);
                    virtual_children_graph[r].reserve(comp_count);
                    for (uint16_t i = 0; i < comp_count; ++i) {
                        uint16_t cid = ext_ids[i];
                        if (cid >= snap->relations.size() || cid == r) {
                            g_last_error = "Virtual super-relation references out-of-bounds or self-referential child relation";
                            if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                            return nullptr;
                        }
                        virtual_children_graph[r].push_back(cid);
                    }
                } else {
                    virtual_children_graph[r].reserve(comp_count);
                    for (uint16_t i = 0; i < comp_count; ++i) {
                        uint16_t cid = 0;
                        std::memcpy(&cid, &rel.reserved2[2 + 2 * i], sizeof(uint16_t));
                        if (cid >= snap->relations.size() || cid == r) {
                            g_last_error = "Virtual super-relation inline child relation ID out of bounds or self-loop";
                            if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                            return nullptr;
                        }
                        virtual_children_graph[r].push_back(cid);
                    }
                }
            }
            continue;
        }

        bool is_raw_coo = false;
        uint64_t coo_row_bytes = 0;
        if (is_modern && rel.encoding_id == 3 && !flat_forward &&
            safe_mul(rel.edge_count, 4ULL, &coo_row_bytes) && rel.csr_row_off_bytes == coo_row_bytes) {
            uint64_t n_plus_1 = 0;
            uint64_t std_csr_bytes = 0;
            bool std_csr_ok = safe_add(rel.node_count, 1ULL, &n_plus_1) &&
                              safe_mul(n_plus_1, edge_width_bytes, &std_csr_bytes);
            if (!std_csr_ok || rel.csr_row_off_bytes != std_csr_bytes) {
                is_raw_coo = true;
            } else if (rel.csr_row_off_offset > 0 && rel.edge_count > 0) {
                uint64_t first_off = is_edge64
                    ? reinterpret_cast<const uint64_t*>(raw + rel.csr_row_off_offset)[0]
                    : reinterpret_cast<const uint32_t*>(raw + rel.csr_row_off_offset)[0];
                uint64_t last_off = is_edge64
                    ? reinterpret_cast<const uint64_t*>(raw + rel.csr_row_off_offset)[rel.node_count]
                    : reinterpret_cast<const uint32_t*>(raw + rel.csr_row_off_offset)[rel.node_count];
                if (first_off != 0 || last_off != rel.edge_count) {
                    is_raw_coo = true;
                }
            }
        }

        if (flat_forward) {
            if (rel.csr_col_idx_offset > 0 || rel.csr_col_idx_bytes > 0 || rel.node_count > 0) {
                uint64_t req_col_bytes = 0;
                if (!safe_mul(rel.node_count, tgt_node_width_bytes, &req_col_bytes) || rel.csr_col_idx_bytes < req_col_bytes) {
                    g_last_error = "Flat M:1 / 1:1 col indices buffer smaller than required minimum byte size";
                    if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                    return nullptr;
                }
                if (rel.csr_col_idx_offset > 0 && tgt_node_count > 0) {
                    for (uint64_t u = 0; u < rel.node_count; ++u) {
                        uint64_t target_val = is_node16
                            ? reinterpret_cast<const uint16_t*>(raw + rel.csr_col_idx_offset)[u]
                            : (is_node64
                                ? reinterpret_cast<const uint64_t*>(raw + rel.csr_col_idx_offset)[u]
                                : reinterpret_cast<const uint32_t*>(raw + rel.csr_col_idx_offset)[u]);
                        if (target_val != max_node_sentinel && target_val >= tgt_node_count) {
                            g_last_error = "Flat M:1 / 1:1 target node ID exceeds target domain node_count";
                            if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                            return nullptr;
                        }
                    }
                }
            }
        } else if (is_raw_coo) {
            uint64_t req_col_bytes = 0;
            if (!safe_mul(rel.edge_count, tgt_node_width_bytes, &req_col_bytes) || rel.csr_col_idx_bytes < req_col_bytes) {
                g_last_error = "Raw COO col indices buffer smaller than required minimum byte size";
                if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                return nullptr;
            }
            if (rel.csr_row_off_offset > 0 && src_node_count > 0) {
                const auto* coo_srcs = reinterpret_cast<const uint32_t*>(raw + rel.csr_row_off_offset);
                for (uint64_t e = 0; e < rel.edge_count; ++e) {
                    if (coo_srcs[e] >= src_node_count) {
                        g_last_error = "Raw COO source node ID exceeds source domain node_count";
                        if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                        return nullptr;
                    }
                }
            }
            if (rel.csr_col_idx_offset > 0 && tgt_node_count > 0) {
                for (uint64_t e = 0; e < rel.edge_count; ++e) {
                    uint64_t target_val = is_node16
                        ? reinterpret_cast<const uint16_t*>(raw + rel.csr_col_idx_offset)[e]
                        : (is_node64
                            ? reinterpret_cast<const uint64_t*>(raw + rel.csr_col_idx_offset)[e]
                            : reinterpret_cast<const uint32_t*>(raw + rel.csr_col_idx_offset)[e]);
                    if (target_val >= tgt_node_count) {
                        g_last_error = "Raw COO target node ID exceeds target domain node_count";
                        if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                        return nullptr;
                    }
                }
            }
        } else if (rel.csr_row_off_offset > 0 || rel.csr_row_off_bytes > 0 ||
            rel.csr_col_idx_offset > 0 || rel.csr_col_idx_bytes > 0 ||
            rel.node_count > 0 || rel.edge_count > 0) {
            uint64_t n_plus_1 = 0;
            uint64_t req_row_bytes = 0;
            if (!safe_add(rel.node_count, 1ULL, &n_plus_1) ||
                !safe_mul(n_plus_1, edge_width_bytes, &req_row_bytes) ||
                rel.csr_row_off_bytes < req_row_bytes) {
                g_last_error = "CSR row offsets buffer smaller than required minimum byte size";
                if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                return nullptr;
            }

            uint64_t req_col_bytes = 0;
            if (!safe_mul(rel.edge_count, tgt_node_width_bytes, &req_col_bytes) || rel.csr_col_idx_bytes < req_col_bytes) {
                g_last_error = "CSR col indices buffer smaller than required minimum byte size";
                if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                return nullptr;
            }

            // Monotonicity of CSR row offsets and col_indices < tgt_node_count
            if (rel.csr_row_off_offset > 0) {
                uint64_t prev_off = is_edge64
                    ? reinterpret_cast<const uint64_t*>(raw + rel.csr_row_off_offset)[0]
                    : reinterpret_cast<const uint32_t*>(raw + rel.csr_row_off_offset)[0];
                if (prev_off != 0) {
                    g_last_error = "CSR row_offsets[0] must be 0";
                    if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                    return nullptr;
                }
                for (uint64_t u = 0; u < rel.node_count; ++u) {
                    uint64_t next_off = is_edge64
                        ? reinterpret_cast<const uint64_t*>(raw + rel.csr_row_off_offset)[u + 1]
                        : reinterpret_cast<const uint32_t*>(raw + rel.csr_row_off_offset)[u + 1];
                    if (next_off < prev_off || next_off > rel.edge_count) {
                        g_last_error = "CSR row_offsets non-monotonic or exceeding edge_count";
                        if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                        return nullptr;
                    }
                    prev_off = next_off;
                }
            }

            if (rel.csr_col_idx_offset > 0 && tgt_node_count > 0) {
                for (uint64_t e = 0; e < rel.edge_count; ++e) {
                    uint64_t target_val = is_node16
                        ? reinterpret_cast<const uint16_t*>(raw + rel.csr_col_idx_offset)[e]
                        : (is_node64
                            ? reinterpret_cast<const uint64_t*>(raw + rel.csr_col_idx_offset)[e]
                            : reinterpret_cast<const uint32_t*>(raw + rel.csr_col_idx_offset)[e]);
                    if (target_val >= tgt_node_count) {
                        g_last_error = "CSR target node ID exceeds target domain node_count";
                        if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                        return nullptr;
                    }
                }
            }
        }

        // CSC section bounds and monotonicity (if present)
        if (rel.csc_row_off_bytes > 0 || rel.csc_col_idx_bytes > 0 || rel.csc_row_off_offset > 0 || rel.csc_col_idx_offset > 0) {
            if (!check_section_bounds(rel.csc_row_off_offset, rel.csc_row_off_bytes, file_size, req_align) ||
                !check_section_bounds(rel.csc_col_idx_offset, rel.csc_col_idx_bytes, file_size, req_align)) {
                g_last_error = "CSC section bounds or alignment check failed";
                if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                return nullptr;
            }

            if (flat_reverse) {
                uint64_t req_csc_col_bytes = 0;
                if (!safe_mul(tgt_node_count, src_node_width_bytes, &req_csc_col_bytes) || rel.csc_col_idx_bytes < req_csc_col_bytes) {
                    g_last_error = "Flat 1:M / 1:1 reverse col indices buffer smaller than required minimum byte size";
                    if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                    return nullptr;
                }
                if (rel.csc_col_idx_offset > 0 && src_node_count > 0) {
                    for (uint64_t v = 0; v < tgt_node_count; ++v) {
                        uint64_t src_val = is_node16
                            ? reinterpret_cast<const uint16_t*>(raw + rel.csc_col_idx_offset)[v]
                            : (is_node64
                                ? reinterpret_cast<const uint64_t*>(raw + rel.csc_col_idx_offset)[v]
                                : reinterpret_cast<const uint32_t*>(raw + rel.csc_col_idx_offset)[v]);
                        if (src_val != max_node_sentinel && src_val >= src_node_count) {
                            g_last_error = "Flat 1:M / 1:1 reverse source node ID exceeds source domain node_count";
                            if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                            return nullptr;
                        }
                    }
                }
            } else {
                uint64_t tgt_plus_1 = 0;
                uint64_t req_csc_row_bytes = 0;
                if (!safe_add(tgt_node_count, 1ULL, &tgt_plus_1) ||
                    !safe_mul(tgt_plus_1, edge_width_bytes, &req_csc_row_bytes) ||
                    rel.csc_row_off_bytes < req_csc_row_bytes) {
                    g_last_error = "CSC row offsets buffer smaller than required minimum byte size";
                    if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                    return nullptr;
                }

                uint64_t req_csc_col_bytes = 0;
                if (!safe_mul(rel.edge_count, src_node_width_bytes, &req_csc_col_bytes) || rel.csc_col_idx_bytes < req_csc_col_bytes) {
                    g_last_error = "CSC col indices buffer smaller than required minimum byte size";
                    if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                    return nullptr;
                }

                if (rel.csc_row_off_offset > 0) {
                    uint64_t prev_off = is_edge64
                        ? reinterpret_cast<const uint64_t*>(raw + rel.csc_row_off_offset)[0]
                        : reinterpret_cast<const uint32_t*>(raw + rel.csc_row_off_offset)[0];
                    if (prev_off != 0) {
                        g_last_error = "CSC row_offsets[0] must be 0";
                        if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                        return nullptr;
                    }
                    for (uint64_t v = 0; v < tgt_node_count; ++v) {
                        uint64_t next_off = is_edge64
                            ? reinterpret_cast<const uint64_t*>(raw + rel.csc_row_off_offset)[v + 1]
                            : reinterpret_cast<const uint32_t*>(raw + rel.csc_row_off_offset)[v + 1];
                        if (next_off < prev_off || next_off > rel.edge_count) {
                            g_last_error = "CSC row_offsets non-monotonic or exceeding edge_count";
                            if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                            return nullptr;
                        }
                        prev_off = next_off;
                    }
                }

                if (rel.csc_col_idx_offset > 0 && src_node_count > 0) {
                    for (uint64_t e = 0; e < rel.edge_count; ++e) {
                        uint64_t src_val = is_node16
                            ? reinterpret_cast<const uint16_t*>(raw + rel.csc_col_idx_offset)[e]
                            : (is_node64
                                ? reinterpret_cast<const uint64_t*>(raw + rel.csc_col_idx_offset)[e]
                                : reinterpret_cast<const uint32_t*>(raw + rel.csc_col_idx_offset)[e]);
                        if (src_val >= src_node_count) {
                            g_last_error = "CSC source node ID exceeds source domain node_count";
                            if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                            return nullptr;
                        }
                    }
                }
            }
        }

        // Validate attribute descriptors & payloads
        if (r < snap->relation_attributes.size()) {
            for (const auto& attr : snap->relation_attributes[r]) {
                uint64_t rel_rows = (rel.edge_count > 0 || (attr.data_bytes == 0 && attr.offsets_bytes <= 4))
                    ? rel.edge_count
                    : rel.node_count;
                impulse_status_t attr_st = validate_attribute_payload(
                    raw, file_size, attr, rel_rows, false, is_v10, req_align, g_last_error
                );
                if (attr_st != IMPULSE_OK) {
                    if (out_status) *out_status = attr_st;
                    return nullptr;
                }
            }
        }
    }

    // 3-color DFS cycle & max-depth (<= 16) validation across virtual super-relations
    if (has_any_virtual_relation) {
        std::vector<uint8_t> color(snap->relations.size(), 0);
        auto dfs_virtual = [&](auto& self, size_t u, uint32_t depth) -> bool {
            if (depth > 16) return false;
            color[u] = 1; // visiting
            for (uint16_t cid : virtual_children_graph[u]) {
                if (cid >= snap->relations.size()) return false;
                if (color[cid] == 1) return false; // cycle detected
                if (color[cid] == 0 && !virtual_children_graph[cid].empty()) {
                    if (!self(self, cid, depth + 1)) return false;
                }
            }
            color[u] = 2; // done
            return true;
        };
        for (size_t r = 0; r < snap->relations.size(); ++r) {
            if (!virtual_children_graph[r].empty() && color[r] == 0) {
                if (!dfs_virtual(dfs_virtual, r, 0)) {
                    g_last_error = "Virtual super-relation graph contains a cycle or exceeds max nesting depth (16)";
                    if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
                    return nullptr;
                }
            }
        }
    }

    // Validate index descriptors
    for (const auto& idx : snap->indexes) {
        if (!check_section_bounds(idx.data_offset, idx.data_bytes, file_size, req_align)) {
            g_last_error = "Index section bounds check failed";
            if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
            return nullptr;
        }
        if (is_modern && !snap->domains.empty() && idx.domain_id != 0xFFFF && idx.domain_id >= snap->domains.size()) {
            g_last_error = "Secondary index domain_id out of bounds";
            if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
            return nullptr;
        }
        if (is_modern && idx.relation_id != 0xFFFF && idx.relation_id >= snap->relations.size()) {
            g_last_error = "Secondary index relation_id out of bounds";
            if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
            return nullptr;
        }
    }

    // Read Footer Block Metadata if available at EOF - 16
    if (file_size >= 16) {
        size_t trailer_pos = file_size - 16;
        impulse_footer_trailer_t trailer;
        std::memcpy(&trailer, raw + trailer_pos, sizeof(trailer));

        if (trailer.footer_magic == IMPULSE_MAGIC && trailer.footer_length >= 16 && trailer.footer_length <= file_size) {
            size_t meta_offset = file_size - static_cast<size_t>(trailer.footer_length);
            size_t meta_bytes = static_cast<size_t>(trailer.footer_length) - 16;

            if (snap->header.footer_directory_offset > 0 && snap->header.footer_directory_offset == meta_offset) {
                if (snap->header.footer_directory_bytes <= meta_bytes) {
                    meta_offset += static_cast<size_t>(snap->header.footer_directory_bytes);
                    meta_bytes -= static_cast<size_t>(snap->header.footer_directory_bytes);
                } else {
                    meta_bytes = 0;
                }
            }

            if (meta_offset <= file_size && meta_bytes <= file_size - meta_offset && meta_bytes >= 4) {
                size_t mcur = meta_offset;
                size_t meta_end = meta_offset + meta_bytes;
                uint32_t count = 0;
                std::memcpy(&count, raw + mcur, 4);
                mcur += 4;

                for (uint32_t k = 0; k < count; ++k) {
                    if (meta_end - mcur < 2) break;
                    uint16_t klen = 0;
                    std::memcpy(&klen, raw + mcur, 2);
                    mcur += 2;
                    if (meta_end - mcur < klen) break;
                    std::string key(reinterpret_cast<const char*>(raw + mcur), klen);
                    mcur += klen;

                    if (meta_end - mcur < 4) break;
                    uint32_t vlen = 0;
                    std::memcpy(&vlen, raw + mcur, 4);
                    mcur += 4;
                    if (meta_end - mcur < vlen) break;
                    std::string val(reinterpret_cast<const char*>(raw + mcur), vlen);
                    mcur += vlen;

                    snap->metadata[key] = val;
                }
            }
        }
    }

    if (out_status) *out_status = IMPULSE_OK;
    IMPULSE_ASSERT(snap != nullptr);
    IMPULSE_AUDIT_ASSERT(snap->mmap_ptr != nullptr);
    IMPULSE_AUDIT_ASSERT(snap->file_size >= 64);
    return snap.release();
}

void impulse_snapshot_close(impulse_snapshot_t* snapshot) {
    if (!snapshot) return;
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

impulse_status_t impulse_snapshot_get_domain_entry(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_index,
    impulse_domain_catalog_entry_t* out_entry,
    const char** out_name
) {
    if (!snapshot || !out_entry || domain_index >= snapshot->domains.size()) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    *out_entry = snapshot->domains[domain_index];
    if (out_name) {
        if (domain_index < snapshot->domain_names.size()) {
            *out_name = snapshot->domain_names[domain_index].c_str();
        } else {
            *out_name = "";
        }
    }
    return IMPULSE_OK;
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
    if (!check_section_bounds(rel.csr_row_off_offset, rel.csr_row_off_bytes, snapshot->file_size, 0) ||
        !check_section_bounds(rel.csr_col_idx_offset, rel.csr_col_idx_bytes, snapshot->file_size, 0)) {
        return false;
    }

    bool is_edge64 = (rel.edge_index_width == 8 || rel.edge_index_width == 64);
    bool is_node16 = (rel.node_id_width == 2 || rel.node_id_width == 16);
    bool is_node64 = (rel.node_id_width == 8 || rel.node_id_width == 64);

    size_t edge_width_bytes = is_edge64 ? 8 : 4;
    size_t node_width_bytes = is_node16 ? 2 : (is_node64 ? 8 : 4);

    size_t num_row_offsets = rel.csr_row_off_bytes / edge_width_bytes;
    if (src_id + 1 >= num_row_offsets) return false;

    uint64_t start_idx = is_edge64 ? reinterpret_cast<const uint64_t*>(raw + rel.csr_row_off_offset)[src_id]
                                   : reinterpret_cast<const uint32_t*>(raw + rel.csr_row_off_offset)[src_id];
    uint64_t end_idx = is_edge64 ? reinterpret_cast<const uint64_t*>(raw + rel.csr_row_off_offset)[src_id + 1]
                                 : reinterpret_cast<const uint32_t*>(raw + rel.csr_row_off_offset)[src_id + 1];

    size_t num_col_indices = rel.csr_col_idx_bytes / node_width_bytes;
    if (start_idx > end_idx || end_idx > num_col_indices) return false;

    for (uint64_t idx = start_idx; idx < end_idx; ++idx) {
        uint64_t target_val = is_node16 ? reinterpret_cast<const uint16_t*>(raw + rel.csr_col_idx_offset)[idx]
                            : (is_node64 ? reinterpret_cast<const uint64_t*>(raw + rel.csr_col_idx_offset)[idx]
                                         : reinterpret_cast<const uint32_t*>(raw + rel.csr_col_idx_offset)[idx]);
        if (target_val == tgt_id) {
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


uint16_t impulse_snapshot_get_index_count(const impulse_snapshot_t* snapshot) {
    if (!snapshot) return 0;
    return static_cast<uint16_t>(snapshot->indexes.size());
}

impulse_status_t impulse_snapshot_get_index(
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
) {
    if (!snapshot || index_idx >= snapshot->indexes.size()) return IMPULSE_ERR_INVALID_ARGUMENT;

    const auto& entry = snapshot->indexes[index_idx];
    if (index_id) *index_id = entry.index_id;
    if (domain_id) *domain_id = entry.domain_id;
    if (relation_id) *relation_id = entry.relation_id;
    if (attribute_index) *attribute_index = entry.attribute_index;
    if (index_type) *index_type = entry.index_type;
    if (index_name && index_idx < snapshot->index_names.size()) *index_name = snapshot->index_names[index_idx].c_str();
    if (index_data) *index_data = (entry.data_offset > 0) ? (reinterpret_cast<const uint8_t*>(snapshot->mmap_ptr) + entry.data_offset) : nullptr;
    if (index_bytes) *index_bytes = entry.data_bytes;

    return IMPULSE_OK;
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
    if (!snapshot || !src_nodes || !out_count || relation_index >= snapshot->relations.size()) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    const auto& rel = snapshot->relations[relation_index];
    if (rel.csr_row_off_offset == 0 || rel.csr_col_idx_offset == 0) {
        *out_count = 0;
        return IMPULSE_OK;
    }
    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);
    if (!check_section_bounds(rel.csr_row_off_offset, rel.csr_row_off_bytes, snapshot->file_size, 0) ||
        !check_section_bounds(rel.csr_col_idx_offset, rel.csr_col_idx_bytes, snapshot->file_size, 0)) {
        return IMPULSE_ERR_BUFFER_OVERFLOW;
    }

    bool is_edge64 = (rel.edge_index_width == 8 || rel.edge_index_width == 64);
    bool is_node16 = (rel.node_id_width == 2 || rel.node_id_width == 16);
    bool is_node64 = (rel.node_id_width == 8 || rel.node_id_width == 64);
    size_t edge_w = is_edge64 ? 8 : 4;
    size_t node_w = is_node16 ? 2 : (is_node64 ? 8 : 4);
    size_t num_row_offsets = rel.csr_row_off_bytes / edge_w;
    size_t num_col_indices = rel.csr_col_idx_bytes / node_w;

    auto read_row_off = [&](uint64_t idx) -> uint64_t {
        return is_edge64 ? reinterpret_cast<const uint64_t*>(raw + rel.csr_row_off_offset)[idx]
                         : reinterpret_cast<const uint32_t*>(raw + rel.csr_row_off_offset)[idx];
    };
    auto read_col_tgt = [&](uint64_t idx) -> uint64_t {
        return is_node16 ? reinterpret_cast<const uint16_t*>(raw + rel.csr_col_idx_offset)[idx]
                         : (is_node64 ? reinterpret_cast<const uint64_t*>(raw + rel.csr_col_idx_offset)[idx]
                                      : reinterpret_cast<const uint32_t*>(raw + rel.csr_col_idx_offset)[idx]);
    };

    size_t total_written = 0;
    pcg32_fast rng(seed, 54u);

    // Dry-run mode: count total samples needed
    if (!out_src || !out_tgt) {
        for (size_t i = 0; i < num_nodes; ++i) {
            uint64_t u = src_nodes[i];
            if (u >= rel.node_count || u + 1 >= num_row_offsets) continue;

            uint64_t start_idx = read_row_off(u);
            uint64_t end_idx = read_row_off(u + 1);
            if (start_idx >= end_idx || end_idx > num_col_indices) continue;
            uint64_t deg64 = end_idx - start_idx;
            uint32_t deg = (deg64 > static_cast<uint64_t>(INT32_MAX)) ? static_cast<uint32_t>(INT32_MAX) : static_cast<uint32_t>(deg64);

            size_t num_to_sample = (k_samples < 0)
                ? static_cast<size_t>(deg)
                : static_cast<size_t>((std::min)(static_cast<int>(deg), k_samples));
            total_written += num_to_sample;
        }
        *out_count = total_written;
        return IMPULSE_OK;
    }

    for (size_t i = 0; i < num_nodes; ++i) {
        uint64_t u = src_nodes[i];
        if (u >= rel.node_count || u + 1 >= num_row_offsets) continue;

        uint64_t start_idx = read_row_off(u);
        uint64_t end_idx = read_row_off(u + 1);
        if (start_idx >= end_idx || end_idx > num_col_indices) continue;
        uint64_t deg64 = end_idx - start_idx;
        uint32_t deg = (deg64 > static_cast<uint64_t>(INT32_MAX)) ? static_cast<uint32_t>(INT32_MAX) : static_cast<uint32_t>(deg64);

        int num_to_sample = (k_samples < 0) ? static_cast<int>(deg) : (std::min)(static_cast<int>(deg), k_samples);

        for (int k = 0; k < num_to_sample; ++k) {
            if (total_written >= out_capacity) {
                *out_count = total_written;
                return IMPULSE_ERR_BUFFER_OVERFLOW;
            }

            uint64_t sample_idx = (k_samples < 0 || num_to_sample == static_cast<int>(deg))
                ? (start_idx + static_cast<uint64_t>(k))
                : (start_idx + static_cast<uint64_t>(rng.bounded(deg)));

            out_src[total_written] = u;
            out_tgt[total_written] = read_col_tgt(sample_idx);
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
    *out_offsets = (rel.csr_row_off_offset > 0) ? reinterpret_cast<const uint32_t*>(raw + rel.csr_row_off_offset) : nullptr;
    *out_targets = (rel.csr_col_idx_offset > 0) ? reinterpret_cast<const uint32_t*>(raw + rel.csr_col_idx_offset) : nullptr;
    if (out_node_count) *out_node_count = rel.node_count;
    if (out_edge_count) *out_edge_count = rel.edge_count;
    return IMPULSE_OK;
}

impulse_status_t impulse_snapshot_get_relation_raw_buffers(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    const void** out_offsets,
    const void** out_targets,
    uint64_t* out_node_count,
    uint64_t* out_edge_count,
    uint8_t* out_node_id_width,
    uint8_t* out_edge_index_width
) {
    if (!snapshot || relation_index >= snapshot->relations.size() || !out_offsets || !out_targets) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    const auto& rel = snapshot->relations[relation_index];
    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);
    *out_offsets = (rel.csr_row_off_offset > 0) ? (raw + rel.csr_row_off_offset) : nullptr;
    *out_targets = (rel.csr_col_idx_offset > 0) ? (raw + rel.csr_col_idx_offset) : nullptr;
    if (out_node_count) *out_node_count = rel.node_count;
    if (out_edge_count) *out_edge_count = rel.edge_count;
    if (out_node_id_width) *out_node_id_width = rel.node_id_width ? rel.node_id_width : 4;
    if (out_edge_index_width) *out_edge_index_width = rel.edge_index_width ? rel.edge_index_width : 4;
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
    if (out_data) *out_data = (attr.data_offset > 0) ? (raw + attr.data_offset) : nullptr;
    if (out_data_bytes) *out_data_bytes = attr.data_bytes;
    if (out_offsets) *out_offsets = (attr.offsets_offset > 0) ? (raw + attr.offsets_offset) : nullptr;
    if (out_offsets_bytes) *out_offsets_bytes = attr.offsets_bytes;
    if (out_type_code) *out_type_code = attr.type_code;
    if (out_dimension) *out_dimension = attr.dimension;
    return IMPULSE_OK;
}

impulse_status_t impulse_snapshot_get_attribute_name(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    uint16_t attribute_index,
    const char** out_name
) {
    if (!snapshot || !out_name || relation_index >= snapshot->relation_attribute_names.size()) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    const auto& names = snapshot->relation_attribute_names[relation_index];
    if (attribute_index >= names.size()) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    *out_name = names[attribute_index].c_str();
    return IMPULSE_OK;
}

impulse_status_t impulse_snapshot_get_relation_csc_buffers(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    const uint32_t** out_csc_offsets,
    const uint32_t** out_csc_targets,
    uint64_t* out_csc_row_count,
    uint64_t* out_csc_edge_count
) {
    if (!snapshot || relation_index >= snapshot->relations.size() || !out_csc_offsets || !out_csc_targets) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    const auto& rel = snapshot->relations[relation_index];
    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);
    if (rel.csc_row_off_offset > 0) {
        *out_csc_offsets = reinterpret_cast<const uint32_t*>(raw + rel.csc_row_off_offset);
    } else {
        *out_csc_offsets = nullptr;
    }
    if (rel.csc_col_idx_offset > 0) {
        *out_csc_targets = reinterpret_cast<const uint32_t*>(raw + rel.csc_col_idx_offset);
    } else {
        *out_csc_targets = nullptr;
    }
    uint8_t ew = (rel.edge_index_width == 8 || rel.edge_index_width == 64) ? 8 : 4;
    uint64_t csc_rows = (rel.csc_row_off_bytes >= ew)
        ? ((rel.csc_row_off_bytes / ew) - 1)
        : ((rel.tgt_domain_id < snapshot->domains.size() && snapshot->domains[rel.tgt_domain_id].node_count > 0)
            ? snapshot->domains[rel.tgt_domain_id].node_count
            : rel.node_count);
    if (out_csc_row_count) *out_csc_row_count = csc_rows;
    if (out_csc_edge_count) *out_csc_edge_count = rel.edge_count;
    return IMPULSE_OK;
}

impulse_status_t impulse_snapshot_get_relation_csc_raw_buffers(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    const void** out_csc_offsets,
    const void** out_csc_targets,
    uint64_t* out_csc_row_count,
    uint64_t* out_csc_edge_count,
    uint8_t* out_node_id_width,
    uint8_t* out_edge_index_width
) {
    if (!snapshot || relation_index >= snapshot->relations.size() || !out_csc_offsets || !out_csc_targets) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    const auto& rel = snapshot->relations[relation_index];
    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);
    *out_csc_offsets = (rel.csc_row_off_offset > 0) ? (raw + rel.csc_row_off_offset) : nullptr;
    *out_csc_targets = (rel.csc_col_idx_offset > 0) ? (raw + rel.csc_col_idx_offset) : nullptr;
    uint8_t ew = (rel.edge_index_width == 8 || rel.edge_index_width == 64) ? 8 : 4;
    uint64_t csc_rows = (rel.csc_row_off_bytes >= ew)
        ? ((rel.csc_row_off_bytes / ew) - 1)
        : ((rel.tgt_domain_id < snapshot->domains.size() && snapshot->domains[rel.tgt_domain_id].node_count > 0)
            ? snapshot->domains[rel.tgt_domain_id].node_count
            : rel.node_count);
    if (out_csc_row_count) *out_csc_row_count = csc_rows;
    if (out_csc_edge_count) *out_csc_edge_count = rel.edge_count;
    if (out_node_id_width) *out_node_id_width = rel.node_id_width ? rel.node_id_width : 4;
    if (out_edge_index_width) *out_edge_index_width = rel.edge_index_width ? rel.edge_index_width : 4;
    return IMPULSE_OK;
}

uint16_t impulse_snapshot_get_domain_attribute_count(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_index
) {
    if (!snapshot || domain_index >= snapshot->domain_attributes.size()) return 0;
    return static_cast<uint16_t>(snapshot->domain_attributes[domain_index].size());
}

uint16_t impulse_snapshot_get_domain_key_attr_index(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_index
) {
    if (!snapshot || domain_index >= snapshot->domains.size()) return 0xFFFF;
    return snapshot->domains[domain_index].key_attr_index;
}

impulse_status_t impulse_snapshot_get_domain_attribute_buffers(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_index,
    uint16_t attribute_index,
    const void** out_data,
    uint64_t* out_data_bytes,
    const void** out_offsets,
    uint64_t* out_offsets_bytes,
    uint8_t* out_type_code,
    uint32_t* out_dimension
) {
    if (!snapshot || domain_index >= snapshot->domain_attributes.size()) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    const auto& attrs = snapshot->domain_attributes[domain_index];
    if (attribute_index >= attrs.size()) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    const auto& attr = attrs[attribute_index];
    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);
    if (out_data) *out_data = (attr.data_offset > 0) ? (raw + attr.data_offset) : nullptr;
    if (out_data_bytes) *out_data_bytes = attr.data_bytes;
    if (out_offsets) *out_offsets = (attr.offsets_offset > 0) ? (raw + attr.offsets_offset) : nullptr;
    if (out_offsets_bytes) *out_offsets_bytes = attr.offsets_bytes;
    if (out_type_code) *out_type_code = attr.type_code;
    if (out_dimension) *out_dimension = attr.dimension;
    return IMPULSE_OK;
}

impulse_status_t impulse_snapshot_get_domain_attribute_name(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_index,
    uint16_t attribute_index,
    const char** out_name
) {
    if (!snapshot || !out_name || domain_index >= snapshot->domain_attribute_names.size()) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    const auto& names = snapshot->domain_attribute_names[domain_index];
    if (attribute_index >= names.size()) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    *out_name = names[attribute_index].c_str();
    return IMPULSE_OK;
}

impulse_status_t impulse_snapshot_get_relation_name(
    const impulse_snapshot_t* snapshot,
    uint16_t relation_index,
    const char** out_name
) {
    if (!snapshot || !out_name || relation_index >= snapshot->relation_names.size()) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    *out_name = snapshot->relation_names[relation_index].c_str();
    return IMPULSE_OK;
}

static bool resolve_domain_key_slice_internal(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_id,
    uint32_t node_id,
    const void** out_key_bytes,
    size_t* out_key_len
) {
    if (domain_id >= snapshot->domains.size() || domain_id >= snapshot->domain_attributes.size()) {
        return false;
    }
    const auto& dom = snapshot->domains[domain_id];
    if (node_id >= dom.node_count) return false;
    uint16_t kidx = dom.key_attr_index;
    const auto& attrs = snapshot->domain_attributes[domain_id];
    if (kidx == 0xFFFF || kidx >= attrs.size()) return false;

    const auto& attr = attrs[kidx];
    if (attr.data_offset == 0 && attr.data_bytes == 0) return false;
    if (attr.data_offset == 0 && attr.offsets_offset == 0) return false;

    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);
    const uint8_t* val_ptr = (attr.data_offset > 0) ? (raw + attr.data_offset) : nullptr;
    uint64_t val_bytes = (attr.data_offset > 0) ? attr.data_bytes : 0ULL;

    if ((attr.type_code & 0x80) != 0) {
        if (!val_ptr) return false;
        uint64_t raw_bm = 0;
        uint64_t padded_bm = 0;
        if (!compute_padded_validity_bytes(dom.node_count, &raw_bm, &padded_bm)) return false;
        if (val_bytes < padded_bm || static_cast<uint64_t>(node_id >> 3) >= raw_bm) return false;
        if ((val_ptr[node_id >> 3] & (1u << (node_id & 7u))) == 0) return false;
        val_ptr += padded_bm;
        val_bytes -= padded_bm;
    }

    uint8_t base_type = attr.type_code & 0x7F;
    uint64_t dim = (attr.dimension == 0) ? 1ULL : static_cast<uint64_t>(attr.dimension);

    if (base_type == 0x0B || base_type == 0x0C) {
        if (attr.offsets_offset == 0) return false;
        uint64_t req_off_bytes = 0;
        if (!safe_mul(static_cast<uint64_t>(node_id) + 2ULL, 4ULL, &req_off_bytes) ||
            req_off_bytes > attr.offsets_bytes) {
            return false;
        }
        const uint32_t* offsets = reinterpret_cast<const uint32_t*>(raw + attr.offsets_offset);
        uint32_t start = offsets[node_id];
        uint32_t end = offsets[node_id + 1];
        if (start > end || static_cast<uint64_t>(end) > val_bytes) return false;
        if (end > start && !val_ptr) return false;
        if (base_type == 0x0B && end > start && val_ptr[end - 1] == '\0') {
            end -= 1;
        }
        static const uint8_t kEmptyKey = 0;
        *out_key_bytes = val_ptr ? (val_ptr + start) : &kEmptyKey;
        *out_key_len = static_cast<size_t>(end - start);
        return true;
    }

    if (!val_ptr) return false;
    uint64_t type_w = fixed_attr_type_width(base_type);
    if (type_w == 0ULL) return false;
    uint64_t elem_bytes = 0;
    uint64_t req_end = 0;
    if (!safe_mul(type_w, dim, &elem_bytes) ||
        !safe_mul(static_cast<uint64_t>(node_id) + 1ULL, elem_bytes, &req_end) ||
        req_end > val_bytes) {
        return false;
    }
    *out_key_bytes = val_ptr + static_cast<uint64_t>(node_id) * elem_bytes;
    *out_key_len = static_cast<size_t>(elem_bytes);
    return true;
}

impulse_status_t impulse_snapshot_resolve_key(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_id,
    const void* key_bytes,
    size_t key_len,
    uint32_t* out_node_id
) {
    if (!snapshot || !key_bytes || !out_node_id) return IMPULSE_ERR_INVALID_ARGUMENT;
    if (domain_id >= snapshot->domains.size()) return IMPULSE_ERR_INVALID_ARGUMENT;

    const auto& dom = snapshot->domains[domain_id];
    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);

    for (const auto& idx : snapshot->indexes) {
        if (idx.domain_id == domain_id && idx.relation_id == 0xFFFF && idx.data_offset > 0) {
            if (dom.key_attr_index != 0xFFFF && idx.attribute_index != 0xFFFF &&
                idx.attribute_index != dom.key_attr_index && idx.index_type != 4) {
                continue;
            }
            const void* index_data = raw + idx.data_offset;

            if (idx.index_type == 4) { // IMP_INDEX_MINIMAL_PERFECT_HASH
                std::string null_term_key(static_cast<const char*>(key_bytes), key_len);
                impulse_status_t st = impulse_index_minimal_perfect_hash_lookup(
                    index_data, idx.data_bytes, null_term_key.c_str(), out_node_id
                );
                if (st == IMPULSE_OK) return IMPULSE_OK;
            } else if (idx.index_type == 1 && dom.key_attr_index != 0xFFFF && dom.node_count > 0) {
                // IMP_INDEX_SORTED_PERMUTATION binary search over key_attr_index
                uint64_t count = idx.data_bytes / 4ULL;
                if (count >= dom.node_count) {
                    const uint32_t* perm = reinterpret_cast<const uint32_t*>(index_data);
                    uint8_t base_type = 0;
                    if (domain_id < snapshot->domain_attributes.size() &&
                        dom.key_attr_index < snapshot->domain_attributes[domain_id].size()) {
                        base_type = snapshot->domain_attributes[domain_id][dom.key_attr_index].type_code & 0x7F;
                    }
                    auto cmp_keys = [&](const void* a, size_t alen, const void* b, size_t blen) -> int {
                        if ((base_type == 0x01 || base_type == 0x02) && alen == 1 && blen == 1) {
                            if (dom.key_type == 5) {
                                uint8_t va = *static_cast<const uint8_t*>(a);
                                uint8_t vb = *static_cast<const uint8_t*>(b);
                                return (va < vb) ? -1 : ((va > vb) ? 1 : 0);
                            }
                            int8_t va = *static_cast<const int8_t*>(a);
                            int8_t vb = *static_cast<const int8_t*>(b);
                            return (va < vb) ? -1 : ((va > vb) ? 1 : 0);
                        }
                        if (base_type == 0x11 && alen == 1 && blen == 1) {
                            uint8_t va = *static_cast<const uint8_t*>(a);
                            uint8_t vb = *static_cast<const uint8_t*>(b);
                            return (va < vb) ? -1 : ((va > vb) ? 1 : 0);
                        }
                        if (base_type == 0x02 && alen == 2 && blen == 2) {
                            if (dom.key_type == 6) {
                                uint16_t va = 0, vb = 0;
                                std::memcpy(&va, a, 2);
                                std::memcpy(&vb, b, 2);
                                return (va < vb) ? -1 : ((va > vb) ? 1 : 0);
                            }
                            int16_t va = 0, vb = 0;
                            std::memcpy(&va, a, 2);
                            std::memcpy(&vb, b, 2);
                            return (va < vb) ? -1 : ((va > vb) ? 1 : 0);
                        }
                        if (base_type == 0x12 && alen == 2 && blen == 2) {
                            uint16_t va = 0, vb = 0;
                            std::memcpy(&va, a, 2);
                            std::memcpy(&vb, b, 2);
                            return (va < vb) ? -1 : ((va > vb) ? 1 : 0);
                        }
                        if (base_type == 0x03 && alen == 4 && blen == 4) {
                            if (dom.key_type == 7) {
                                uint32_t va = 0, vb = 0;
                                std::memcpy(&va, a, 4);
                                std::memcpy(&vb, b, 4);
                                return (va < vb) ? -1 : ((va > vb) ? 1 : 0);
                            }
                            int32_t va = 0, vb = 0;
                            std::memcpy(&va, a, 4);
                            std::memcpy(&vb, b, 4);
                            return (va < vb) ? -1 : ((va > vb) ? 1 : 0);
                        }
                        if (base_type == 0x13 && alen == 4 && blen == 4) {
                            uint32_t va = 0, vb = 0;
                            std::memcpy(&va, a, 4);
                            std::memcpy(&vb, b, 4);
                            return (va < vb) ? -1 : ((va > vb) ? 1 : 0);
                        }
                        if (base_type == 0x04 && alen == 8 && blen == 8) {
                            if (dom.key_type == 8) {
                                uint64_t va = 0, vb = 0;
                                std::memcpy(&va, a, 8);
                                std::memcpy(&vb, b, 8);
                                return (va < vb) ? -1 : ((va > vb) ? 1 : 0);
                            }
                            int64_t va = 0, vb = 0;
                            std::memcpy(&va, a, 8);
                            std::memcpy(&vb, b, 8);
                            return (va < vb) ? -1 : ((va > vb) ? 1 : 0);
                        }
                        if (base_type == 0x14 && alen == 8 && blen == 8) {
                            uint64_t va = 0, vb = 0;
                            std::memcpy(&va, a, 8);
                            std::memcpy(&vb, b, 8);
                            return (va < vb) ? -1 : ((va > vb) ? 1 : 0);
                        }
                        size_t min_len = std::min(alen, blen);
                        int c = (min_len > 0) ? std::memcmp(a, b, min_len) : 0;
                        if (c != 0) return c;
                        return (alen < blen) ? -1 : ((alen > blen) ? 1 : 0);
                    };

                    size_t lo = 0;
                    size_t hi = static_cast<size_t>(dom.node_count);
                    while (lo < hi) {
                        size_t mid = lo + (hi - lo) / 2;
                        uint32_t cand = perm[mid];
                        const void* ck = nullptr;
                        size_t clen = 0;
                        if (!resolve_domain_key_slice_internal(snapshot, domain_id, cand, &ck, &clen)) {
                            break;
                        }
                        int c = cmp_keys(ck, clen, key_bytes, key_len);
                        if (c == 0) {
                            *out_node_id = cand;
                            return IMPULSE_OK;
                        } else if (c < 0) {
                            lo = mid + 1;
                        } else {
                            hi = mid;
                        }
                    }
                }
            }
        }
    }

    // Spec v0.10.0 §5.1 fallback: direct scan over KeyAttrIndex when no MPHF/Permutation index is present
    if (dom.key_attr_index != 0xFFFF && dom.node_count <= UINT32_MAX) {
        for (uint32_t u = 0; u < static_cast<uint32_t>(dom.node_count); ++u) {
            const void* ck = nullptr;
            size_t clen = 0;
            if (resolve_domain_key_slice_internal(snapshot, domain_id, u, &ck, &clen)) {
                if (clen == key_len && (key_len == 0 || std::memcmp(ck, key_bytes, key_len) == 0)) {
                    *out_node_id = u;
                    return IMPULSE_OK;
                }
            }
        }
    }

    return IMPULSE_ERR_INVALID_ARGUMENT;
}

impulse_status_t impulse_snapshot_resolve_dense_id(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_id,
    uint32_t node_id,
    const void** out_key_bytes,
    size_t* out_key_len
) {
    if (!snapshot || !out_key_bytes || !out_key_len) return IMPULSE_ERR_INVALID_ARGUMENT;
    if (domain_id >= snapshot->domains.size()) return IMPULSE_ERR_INVALID_ARGUMENT;
    if (node_id >= snapshot->domains[domain_id].node_count) return IMPULSE_ERR_INVALID_ARGUMENT;

    // Spec v0.10.0 §5.1: O(1) direct lookup via KeyAttrIndex
    if (resolve_domain_key_slice_internal(snapshot, domain_id, node_id, out_key_bytes, out_key_len)) {
        return IMPULSE_OK;
    }

    // Legacy v0.9.0 fallback: scan Section 2.6 MPHF string table
    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);

    for (const auto& idx : snapshot->indexes) {
        if (idx.domain_id == domain_id && idx.relation_id == 0xFFFF && idx.data_offset > 0) {
            if (idx.index_type == 4) { // IMP_INDEX_MINIMAL_PERFECT_HASH
                const void* index_data = raw + idx.data_offset;
                const uint8_t* iraw = static_cast<const uint8_t*>(index_data);
                
                if (idx.data_bytes < 32) return IMPULSE_ERR_INVALID_ARGUMENT;
                
                uint64_t key_count = 0;
                uint32_t str_bytes = 0;
                std::memcpy(&key_count, iraw, 8);
                std::memcpy(&str_bytes, iraw + 16, 4);

                size_t str_offset = 32;
                if (str_bytes == 0 || str_bytes > idx.data_bytes - str_offset) return IMPULSE_ERR_INVALID_ARGUMENT;
                size_t table_offset = str_offset + str_bytes;
                if (key_count > (idx.data_bytes - table_offset) / 8ULL) return IMPULSE_ERR_INVALID_ARGUMENT;

                const char* str_pool = reinterpret_cast<const char*>(iraw + str_offset);
                const uint8_t* table = iraw + table_offset;

                if (str_bytes == 0 || str_pool[str_bytes - 1] != '\0') return IMPULSE_ERR_INVALID_ARGUMENT;

                for (size_t i = 0; i < key_count; ++i) {
                    uint32_t k_off = 0;
                    uint32_t n_id = 0;
                    std::memcpy(&k_off, table + (i * 8), 4);
                    std::memcpy(&n_id, table + (i * 8) + 4, 4);

                    if (k_off != 0 && n_id == node_id) {
                        if (k_off >= str_bytes) return IMPULSE_ERR_INVALID_ARGUMENT;
                        const void* nul = std::memchr(str_pool + k_off, '\0', str_bytes - k_off);
                        if (!nul) return IMPULSE_ERR_INVALID_ARGUMENT;
                        *out_key_bytes = str_pool + k_off;
                        *out_key_len = static_cast<const char*>(nul) - (str_pool + k_off);
                        return IMPULSE_OK;
                    }
                }
            }
        }
    }
    return IMPULSE_ERR_INVALID_ARGUMENT;
}

} // extern "C"



