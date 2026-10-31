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
    if (::fstat(fd, &st) < 0) {
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

        IMPULSE_TESTCASE(string_table_bytes == 1);
        IMPULSE_WELL_FORMED_ASSERT(string_table_bytes >= 1 && string_pool[0] == '\0');
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

        rem = cur % 128;
        if (rem != 0) cur += 128 - rem;

        uint16_t idx_count = snap->header.index_count;
        snap->indexes.reserve(idx_count);
        snap->index_names.resize(idx_count);

        for (uint16_t k = 0; k < idx_count; ++k) {
            if (cur + sizeof(impulse_index_directory_entry_v0_9_t) > file_size) {
                g_last_error = "Buffer overflow parsing index directory";
                if (out_status) *out_status = IMPULSE_ERR_BUFFER_OVERFLOW;
                return nullptr;
            }
            impulse_index_directory_entry_v0_9_t idx_entry;
            std::memcpy(&idx_entry, raw + cur, sizeof(idx_entry));
            cur += sizeof(idx_entry);

            std::string iname;
            impulse_status_t str_status = validate_and_get_string(idx_entry.name_offset, iname);
            if (str_status == IMPULSE_OK) {
                snap->index_names[k] = iname;
            }

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

            if (aux_offset > 0 && aux_bytes >= 24 && aux_offset + aux_bytes <= file_size) {
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
    IMPULSE_ASSERT(snap != nullptr);
    IMPULSE_AUDIT_ASSERT(snap->mmap_ptr != nullptr);
    IMPULSE_AUDIT_ASSERT(snap->file_size >= 64);
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
    if (rel.csr_row_off_offset + rel.csr_row_off_bytes > snapshot->file_size) return false;
    if (rel.csr_col_idx_offset + rel.csr_col_idx_bytes > snapshot->file_size) return false;

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
    if (index_data) *index_data = reinterpret_cast<const uint8_t*>(snapshot->mmap_ptr) + entry.data_offset;
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
    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);

    const uint32_t* row_offsets = reinterpret_cast<const uint32_t*>(raw + rel.csr_row_off_offset);
    const uint32_t* col_indices = reinterpret_cast<const uint32_t*>(raw + rel.csr_col_idx_offset);

    size_t total_written = 0;
    pcg32_fast rng(seed, 54u);

    // Dry-run mode: count total samples needed
    if (!out_src || !out_tgt) {
        for (size_t i = 0; i < num_nodes; ++i) {
            uint64_t u = src_nodes[i];
            if (u >= rel.node_count) continue;

            uint32_t start_idx = row_offsets[u];
            uint32_t end_idx = row_offsets[u + 1];
            uint32_t deg = end_idx - start_idx;
            if (deg == 0) continue;

            int num_to_sample = (k_samples < 0) ? static_cast<int>(deg) : (std::min)(static_cast<int>(deg), k_samples);
            total_written += num_to_sample;
        }
        *out_count = total_written;
        return IMPULSE_OK;
    }

    for (size_t i = 0; i < num_nodes; ++i) {
        uint64_t u = src_nodes[i];
        if (u >= rel.node_count) continue;

        uint32_t start_idx = row_offsets[u];
        uint32_t end_idx = row_offsets[u + 1];
        uint32_t deg = end_idx - start_idx;

        if (deg == 0) continue;

        int num_to_sample = (k_samples < 0) ? static_cast<int>(deg) : (std::min)(static_cast<int>(deg), k_samples);

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
    if (out_data) *out_data = raw + attr.data_offset;
    if (out_data_bytes) *out_data_bytes = attr.data_bytes;
    if (out_offsets) *out_offsets = (attr.offsets_offset > 0) ? (raw + attr.offsets_offset) : nullptr;
    if (out_offsets_bytes) *out_offsets_bytes = attr.offsets_bytes;
    if (out_type_code) *out_type_code = attr.type_code;
    if (out_dimension) *out_dimension = attr.dimension;
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
    if (out_csc_row_count) *out_csc_row_count = rel.node_count;
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
    if (out_csc_row_count) *out_csc_row_count = rel.node_count;
    if (out_csc_edge_count) *out_csc_edge_count = rel.edge_count;
    if (out_node_id_width) *out_node_id_width = rel.node_id_width ? rel.node_id_width : 4;
    if (out_edge_index_width) *out_edge_index_width = rel.edge_index_width ? rel.edge_index_width : 4;
    return IMPULSE_OK;
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

    for (const auto& idx : snapshot->indexes) {
        if (idx.domain_id == domain_id && idx.relation_id == 0xFFFF) {
            const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);
            const void* index_data = raw + idx.data_offset;
            
            if (idx.index_type == 4) { // IMP_INDEX_MINIMAL_PERFECT_HASH
                std::string null_term_key(static_cast<const char*>(key_bytes), key_len);
                return impulse_index_minimal_perfect_hash_lookup(index_data, idx.data_bytes, null_term_key.c_str(), out_node_id);
            }
            // Add other index types here (e.g. ZoneMap, Permutation for integers) if needed
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

    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);

    for (const auto& idx : snapshot->indexes) {
        if (idx.domain_id == domain_id && idx.relation_id == 0xFFFF) {
            if (idx.index_type == 4) { // IMP_INDEX_MINIMAL_PERFECT_HASH
                const void* index_data = raw + idx.data_offset;
                const uint8_t* iraw = static_cast<const uint8_t*>(index_data);
                
                // MphfHeader is 32 bytes.
                // struct MphfHeader { uint64_t key_count, seed; uint32_t string_table_bytes; uint8_t reserved[12]; }
                if (idx.data_bytes < 32) return IMPULSE_ERR_INVALID_ARGUMENT;
                
                uint64_t key_count = 0;
                uint32_t str_bytes = 0;
                std::memcpy(&key_count, iraw, 8);
                std::memcpy(&str_bytes, iraw + 16, 4);

                size_t str_offset = 32;
                size_t table_offset = str_offset + str_bytes;
                if (idx.data_bytes < table_offset + key_count * 8) return IMPULSE_ERR_INVALID_ARGUMENT;

                const char* str_pool = reinterpret_cast<const char*>(iraw + str_offset);
                const uint8_t* table = iraw + table_offset;

                for (size_t i = 0; i < key_count; ++i) {
                    uint32_t k_off = 0;
                    uint32_t n_id = 0;
                    std::memcpy(&k_off, table + (i * 8), 4);
                    std::memcpy(&n_id, table + (i * 8) + 4, 4);

                    if (k_off != 0 && n_id == node_id) {
                        *out_key_bytes = str_pool + k_off;
                        *out_key_len = std::strlen(str_pool + k_off);
                        return IMPULSE_OK;
                    }
                }
            }
        }
    }
    return IMPULSE_ERR_INVALID_ARGUMENT;
}

} // extern "C"



