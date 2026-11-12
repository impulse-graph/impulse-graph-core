/**
 * @file impulse_vm_fluent_internal.hpp
 * @brief Shared domain/relation/key catalog resolution helpers for the C++20 Fluent SDK.
 */

#ifndef IMPULSE_VM_FLUENT_INTERNAL_HPP
#define IMPULSE_VM_FLUENT_INTERNAL_HPP

#include "impulse_vm_fluent.hpp"
#include "impulse_vm_internal.hpp"
#include "impulse_graph_internal.hpp"
#include "impulse_assert.h"
#include "impulse_compiler.hpp"
#include "impulse_cypher.hpp"
#include "impulse_datalog.hpp"
#include "impulse_impk.hpp"
#include "impulse_cel.h"
#include "impulse_sexpr.hpp"

#include <stdexcept>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <cctype>

namespace impulse::vm {

inline uint16_t resolve_domain_id_by_name(const impulse_snapshot_t* snap, const std::string& name) {
    if (!snap) return 0;
    uint16_t count = impulse_snapshot_domain_count(snap);
    for (uint16_t i = 0; i < count; ++i) {
        impulse_domain_catalog_entry_t entry{};
        const char* dname = nullptr;
        if (impulse_snapshot_get_domain_entry(snap, i, &entry, &dname) == IMPULSE_OK && dname) {
            if (name == dname) return i;
        }
    }
    if (name.rfind("dom_", 0) == 0) {
        return static_cast<uint16_t>(std::stoul(name.substr(4)));
    }
    if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0]))) {
        return static_cast<uint16_t>(std::stoul(name));
    }
    return 0;
}

inline uint16_t resolve_relation_id_by_name(const impulse_snapshot_t* snap, uint16_t /*domain_id*/, const std::string& rel_name) {
    if (!snap) return 0;
    for (size_t i = 0; i < snap->relation_names.size(); ++i) {
        if (!snap->relation_names[i].empty() && snap->relation_names[i] == rel_name) {
            return static_cast<uint16_t>(i);
        }
    }
    if (rel_name.rfind("rel_", 0) == 0) {
        return static_cast<uint16_t>(std::stoul(rel_name.substr(4)));
    }
    if (!rel_name.empty() && std::isdigit(static_cast<unsigned char>(rel_name[0]))) {
        return static_cast<uint16_t>(std::stoul(rel_name));
    }
    uint16_t rel_count = impulse_snapshot_relation_count(snap);
    for (uint16_t i = 0; i < rel_count; ++i) {
        if (rel_name == ("rel_" + std::to_string(i))) return i;
    }
    return 0;
}

inline bool resolve_domain_attribute(
    const impulse_snapshot_t* snap,
    uint16_t domain_id,
    const std::string& attr_name,
    uint16_t* out_rel_id,
    uint16_t* out_attr_id,
    uint8_t* out_type_code
) {
    if (!snap || attr_name.empty()) return false;
    std::string clean_name = attr_name;
    auto dot_pos = clean_name.rfind('.');
    if (dot_pos != std::string::npos) {
        clean_name = clean_name.substr(dot_pos + 1);
    }
    if (domain_id < snap->domain_attribute_names.size()) {
        for (size_t a = 0; a < snap->domain_attribute_names[domain_id].size(); ++a) {
            if (snap->domain_attribute_names[domain_id][a] == clean_name) {
                if (out_rel_id) *out_rel_id = domain_id;
                if (out_attr_id) *out_attr_id = static_cast<uint16_t>(a);
                if (out_type_code) *out_type_code = snap->domain_attributes[domain_id][a].type_code;
                return true;
            }
        }
        if (snap->header.version >= 10 && !snap->domain_attribute_names[domain_id].empty()) {
            return false;
        }
    }
    for (size_t r = 0; r < snap->relations.size(); ++r) {
        if (snap->relations[r].src_domain_id == domain_id && r < snap->relation_attribute_names.size()) {
            for (size_t a = 0; a < snap->relation_attribute_names[r].size(); ++a) {
                if (snap->relation_attribute_names[r][a] == clean_name) {
                    if (out_rel_id) *out_rel_id = static_cast<uint16_t>(r);
                    if (out_attr_id) *out_attr_id = static_cast<uint16_t>(a);
                    if (out_type_code) *out_type_code = snap->relation_attributes[r][a].type_code;
                    return true;
                }
            }
        }
    }
    return false;
}

inline uint32_t get_domain_node_count(const impulse_snapshot_t* snap, uint16_t domain_id) {
    if (!snap) return 0;
    if (domain_id < snap->domains.size() && snap->domains[domain_id].node_count > 0) {
        return static_cast<uint32_t>(snap->domains[domain_id].node_count);
    }
    uint16_t rel_count = impulse_snapshot_relation_count(snap);
    uint64_t max_n = 0;
    for (uint16_t r = 0; r < rel_count; ++r) {
        impulse_relation_directory_entry_t entry{};
        if (impulse_snapshot_get_relation_entry(snap, r, &entry) == IMPULSE_OK) {
            if (entry.src_domain_id == domain_id && entry.node_count > max_n) {
                max_n = entry.node_count;
            }
        }
    }
    if (max_n == 0) {
        max_n = impulse_snapshot_max_node_count(snap);
    }
    return static_cast<uint32_t>(max_n);
}

inline uint32_t resolve_key_to_dense(const impulse_snapshot_t* snap, uint16_t domain_id, const std::string& key) {
    if (snap) {
        uint32_t dense_id = 0;
        if (impulse_snapshot_resolve_key(snap, domain_id, key.data(), key.size(), &dense_id) == IMPULSE_OK) {
            return dense_id;
        }
    }
    if (!key.empty() && std::isdigit(static_cast<unsigned char>(key[0]))) {
        return static_cast<uint32_t>(std::stoul(key));
    }
    return 0;
}

inline uint16_t advance_domain_id(
    const impulse_snapshot_t* snap,
    uint16_t current_dom,
    uint16_t rel_id,
    bool is_reverse
) {
    if (!snap || rel_id >= snap->relations.size()) return current_dom;
    const auto& r = snap->relations[rel_id];
    return is_reverse ? r.src_domain_id : r.tgt_domain_id;
}

inline std::string resolve_dense_to_key(const impulse_snapshot_t* snap, uint16_t domain_id, uint32_t node_id) {
    if (snap) {
        std::string formatted;
        if (format_snapshot_node_key(snap, domain_id, node_id, formatted)) {
            return formatted;
        }
    }
    return std::to_string(node_id);
}

inline uint16_t resolve_attr_or_index_id(const impulse_snapshot_t* snap, const std::string& name, uint16_t domain_id = 0xFFFF) {
    if (name.empty()) return 0;
    if (std::isdigit(static_cast<unsigned char>(name[0]))) {
        return static_cast<uint16_t>(std::stoul(name));
    }
    if (snap) {
        uint16_t dom_count = impulse_snapshot_domain_count(snap);
        if (domain_id < dom_count) {
            uint16_t dcnt = impulse_snapshot_get_domain_attribute_count(snap, domain_id);
            for (uint16_t a = 0; a < dcnt; ++a) {
                const char* aname = nullptr;
                if (impulse_snapshot_get_domain_attribute_name(snap, domain_id, a, &aname) == IMPULSE_OK &&
                    aname && name == aname) {
                    return a;
                }
            }
        }
        uint16_t idx_count = impulse_snapshot_get_index_count(snap);
        for (uint16_t k = 0; k < idx_count; ++k) {
            const char* idx_name = nullptr;
            uint16_t did = 0, rid = 0, aid = 0;
            if (impulse_snapshot_get_index(snap, k, nullptr, &did, &rid, &aid, nullptr, &idx_name, nullptr, nullptr) == IMPULSE_OK &&
                idx_name && name == idx_name) {
                return (domain_id < dom_count && did == domain_id && rid == 0xFFFF) ? aid : k;
            }
        }
        for (uint16_t d = 0; d < dom_count; ++d) {
            uint16_t dcnt = impulse_snapshot_get_domain_attribute_count(snap, d);
            for (uint16_t a = 0; a < dcnt; ++a) {
                const char* aname = nullptr;
                if (impulse_snapshot_get_domain_attribute_name(snap, d, a, &aname) == IMPULSE_OK &&
                    aname && name == aname) {
                    return a;
                }
            }
        }
        uint16_t rel_count = impulse_snapshot_relation_count(snap);
        for (uint16_t r = 0; r < rel_count; ++r) {
            impulse_relation_directory_entry_t rentry{};
            if (impulse_snapshot_get_relation_entry(snap, r, &rentry) == IMPULSE_OK) {
                for (uint16_t a = 0; a < rentry.attr_count; ++a) {
                    const char* aname = nullptr;
                    if (impulse_snapshot_get_attribute_name(snap, r, a, &aname) == IMPULSE_OK &&
                        aname && name == aname) {
                        return a;
                    }
                }
            }
        }
    }
    return 0;
}

inline bool resolve_named_attribute_buffer(
    const impulse_snapshot_t* snap,
    const std::string& name,
    const void** out_data,
    uint64_t* out_bytes,
    uint8_t* out_tc,
    uint32_t* out_dim,
    uint16_t domain_id = 0xFFFF
) {
    if (!snap) return false;
    uint16_t dom_count = impulse_snapshot_domain_count(snap);
    uint16_t rel_count = impulse_snapshot_relation_count(snap);

    if (!name.empty() && !std::isdigit(static_cast<unsigned char>(name[0]))) {
        uint16_t idx_count = impulse_snapshot_get_index_count(snap);
        for (uint16_t k = 0; k < idx_count; ++k) {
            const char* iname = nullptr;
            uint16_t did = 0, rid = 0, aid = 0;
            if (impulse_snapshot_get_index(snap, k, nullptr, &did, &rid, &aid, nullptr, &iname, nullptr, nullptr) == IMPULSE_OK &&
                iname && name == iname) {
                if (rid == 0xFFFF && did < dom_count) {
                    if (impulse_snapshot_get_domain_attribute_buffers(snap, did, aid, out_data, out_bytes, nullptr, nullptr, out_tc, out_dim) == IMPULSE_OK) {
                        return true;
                    }
                } else if (rid != 0xFFFF && rid < rel_count) {
                    if (impulse_snapshot_get_attribute_buffers(snap, rid, aid, out_data, out_bytes, nullptr, nullptr, out_tc, out_dim) == IMPULSE_OK) {
                        return true;
                    }
                }
            }
        }
        if (domain_id < dom_count) {
            uint16_t dcnt = impulse_snapshot_get_domain_attribute_count(snap, domain_id);
            for (uint16_t a = 0; a < dcnt; ++a) {
                const char* aname = nullptr;
                if (impulse_snapshot_get_domain_attribute_name(snap, domain_id, a, &aname) == IMPULSE_OK && aname && name == aname) {
                    if (impulse_snapshot_get_domain_attribute_buffers(snap, domain_id, a, out_data, out_bytes, nullptr, nullptr, out_tc, out_dim) == IMPULSE_OK) {
                        return true;
                    }
                }
            }
        }
        for (uint16_t d = 0; d < dom_count; ++d) {
            uint16_t dcnt = impulse_snapshot_get_domain_attribute_count(snap, d);
            for (uint16_t a = 0; a < dcnt; ++a) {
                const char* aname = nullptr;
                if (impulse_snapshot_get_domain_attribute_name(snap, d, a, &aname) == IMPULSE_OK && aname && name == aname) {
                    if (impulse_snapshot_get_domain_attribute_buffers(snap, d, a, out_data, out_bytes, nullptr, nullptr, out_tc, out_dim) == IMPULSE_OK) {
                        return true;
                    }
                }
            }
        }
        for (uint16_t r = 0; r < rel_count; ++r) {
            impulse_relation_directory_entry_t rentry{};
            if (impulse_snapshot_get_relation_entry(snap, r, &rentry) == IMPULSE_OK) {
                for (uint16_t a = 0; a < rentry.attr_count; ++a) {
                    const char* aname = nullptr;
                    if (impulse_snapshot_get_attribute_name(snap, r, a, &aname) == IMPULSE_OK && aname && name == aname) {
                        if (impulse_snapshot_get_attribute_buffers(snap, r, a, out_data, out_bytes, nullptr, nullptr, out_tc, out_dim) == IMPULSE_OK) {
                            return true;
                        }
                    }
                }
            }
        }
    }

    if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0]))) {
        uint16_t aid = static_cast<uint16_t>(std::stoul(name));
        uint16_t d = (domain_id < dom_count) ? domain_id : 0;
        if (d < dom_count && impulse_snapshot_get_domain_attribute_buffers(snap, d, aid, out_data, out_bytes, nullptr, nullptr, out_tc, out_dim) == IMPULSE_OK) {
            return true;
        }
        if (rel_count > 0 && impulse_snapshot_get_attribute_buffers(snap, 0, aid, out_data, out_bytes, nullptr, nullptr, out_tc, out_dim) == IMPULSE_OK) {
            return true;
        }
    }

    uint16_t d0 = (domain_id < dom_count) ? domain_id : 0;
    if (d0 < dom_count && impulse_snapshot_get_domain_attribute_count(snap, d0) > 0) {
        if (impulse_snapshot_get_domain_attribute_buffers(snap, d0, 0, out_data, out_bytes, nullptr, nullptr, out_tc, out_dim) == IMPULSE_OK) {
            return true;
        }
    }
    if (rel_count > 0) {
        if (impulse_snapshot_get_attribute_buffers(snap, 0, 0, out_data, out_bytes, nullptr, nullptr, out_tc, out_dim) == IMPULSE_OK) {
            return true;
        }
    }
    return false;
}

inline void decode_scalar_attr_for_node(
    const void* raw_data,
    uint64_t data_bytes,
    uint8_t tc,
    uint64_t row_count_hint,
    uint64_t u,
    const std::string& name_str,
    uint16_t snap_version,
    std::unordered_map<std::string, double>& float_attrs,
    std::unordered_map<std::string, int64_t>& int_attrs
) {
    if (!raw_data) return;
    uint8_t base_t = tc & 0x7F;
    bool is_nullable = (tc & 0x80) != 0;
    size_t bitmap_bytes = 0;
    if (is_nullable) {
        if (row_count_hint > 0) {
            bitmap_bytes = ((((static_cast<size_t>(row_count_hint) + 63) / 64) * 8) + 127) & ~size_t(127);
        } else {
            bitmap_bytes = 128;
        }
        if (data_bytes <= bitmap_bytes) return;
        const uint8_t* bm = static_cast<const uint8_t*>(raw_data);
        if ((u >> 3) >= bitmap_bytes || ((bm[u >> 3] >> (u & 7)) & 1U) == 0U) {
            return;
        }
    }
    const uint8_t* val_bytes = static_cast<const uint8_t*>(raw_data) + bitmap_bytes;
    uint64_t avail_bytes = (data_bytes >= bitmap_bytes) ? (data_bytes - bitmap_bytes) : 0;

    if (base_t == 0x06 && (u + 1) * sizeof(float) <= avail_bytes) {
        float val = reinterpret_cast<const float*>(val_bytes)[u];
        float_attrs[name_str] = static_cast<double>(val);
    } else if (base_t == 0x05 && snap_version == 9 && (u + 1) * sizeof(float) <= avail_bytes) {
        float val = reinterpret_cast<const float*>(val_bytes)[u];
        float_attrs[name_str] = static_cast<double>(val);
    } else if (base_t == 0x07 && (u + 1) * sizeof(double) <= avail_bytes) {
        double val = reinterpret_cast<const double*>(val_bytes)[u];
        float_attrs[name_str] = val;
    } else if (base_t == 0x01 && (u + 1) * sizeof(int8_t) <= avail_bytes) {
        int8_t val = reinterpret_cast<const int8_t*>(val_bytes)[u];
        int_attrs[name_str] = val;
        float_attrs[name_str] = static_cast<double>(val);
    } else if (base_t == 0x02 && (u + 1) * sizeof(int16_t) <= avail_bytes) {
        int16_t val = reinterpret_cast<const int16_t*>(val_bytes)[u];
        int_attrs[name_str] = val;
        float_attrs[name_str] = static_cast<double>(val);
    } else if ((base_t == 0x03 || base_t == 0x0D) && (u + 1) * sizeof(int32_t) <= avail_bytes) {
        int32_t val = reinterpret_cast<const int32_t*>(val_bytes)[u];
        int_attrs[name_str] = val;
        float_attrs[name_str] = static_cast<double>(val);
    } else if ((base_t == 0x04 || base_t == 0x08 || base_t == 0x09 || base_t == 0x0E) && (u + 1) * sizeof(int64_t) <= avail_bytes) {
        int64_t val = reinterpret_cast<const int64_t*>(val_bytes)[u];
        int_attrs[name_str] = val;
        float_attrs[name_str] = static_cast<double>(val);
    }
}

inline void populate_node_attrs(
    const impulse_snapshot_t* snap,
    uint64_t u,
    std::unordered_map<std::string, double>& float_attrs,
    std::unordered_map<std::string, int64_t>& int_attrs,
    uint16_t domain_id = 0xFFFF
) {
    if (!snap) return;
    uint16_t ver = snap->header.version;
    uint16_t dom_count = impulse_snapshot_domain_count(snap);

    auto load_dom = [&](uint16_t d) {
        impulse_domain_catalog_entry_t dentry{};
        impulse_snapshot_get_domain_entry(snap, d, &dentry, nullptr);
        uint16_t dcnt = impulse_snapshot_get_domain_attribute_count(snap, d);
        for (uint16_t a = 0; a < dcnt; ++a) {
            const char* aname = nullptr;
            impulse_snapshot_get_domain_attribute_name(snap, d, a, &aname);
            std::string name_str = (aname && aname[0] != '\0') ? aname : ("attr_" + std::to_string(a));
            const void* raw_data = nullptr;
            uint64_t data_bytes = 0;
            uint8_t tc = 0;
            uint32_t dim = 0;
            if (impulse_snapshot_get_domain_attribute_buffers(snap, d, a, &raw_data, &data_bytes, nullptr, nullptr, &tc, &dim) == IMPULSE_OK) {
                decode_scalar_attr_for_node(raw_data, data_bytes, tc, dentry.node_count, u, name_str, ver, float_attrs, int_attrs);
            }
        }
    };

    if (domain_id < dom_count) {
        load_dom(domain_id);
        if (ver >= 10 && impulse_snapshot_get_domain_attribute_count(snap, domain_id) > 0) {
            return;
        }
    } else {
        for (uint16_t d = 0; d < dom_count; ++d) load_dom(d);
    }

    uint16_t rel_count = impulse_snapshot_relation_count(snap);
    for (uint16_t r = 0; r < rel_count; ++r) {
        impulse_relation_directory_entry_t rentry{};
        if (impulse_snapshot_get_relation_entry(snap, r, &rentry) != IMPULSE_OK) continue;
        if (domain_id < dom_count && rentry.src_domain_id != domain_id) continue;
        for (uint16_t a = 0; a < rentry.attr_count; ++a) {
            const char* aname = nullptr;
            impulse_snapshot_get_attribute_name(snap, r, a, &aname);
            std::string name_str = (aname && aname[0] != '\0') ? aname : ("attr_" + std::to_string(a));
            if (float_attrs.count(name_str) > 0 || int_attrs.count(name_str) > 0) continue;

            const void* raw_data = nullptr;
            uint64_t data_bytes = 0;
            uint8_t tc = 0;
            uint32_t dim = 0;
            if (impulse_snapshot_get_attribute_buffers(snap, r, a, &raw_data, &data_bytes, nullptr, nullptr, &tc, &dim) == IMPULSE_OK && raw_data) {
                decode_scalar_attr_for_node(raw_data, data_bytes, tc, rentry.node_count, u, name_str, ver, float_attrs, int_attrs);
            }
        }
    }
}

struct CachedAttrCol {
    std::string name;
    const uint8_t* raw_data = nullptr;
    uint64_t data_bytes = 0;
    const uint8_t* val_bytes = nullptr;
    uint64_t avail_bytes = 0;
    const uint8_t* bitmap_ptr = nullptr;
    uint64_t bitmap_raw_bytes = 0;
    const uint32_t* offsets_ptr = nullptr;
    uint64_t offsets_bytes = 0;
    uint8_t base_tc = 0;
    bool is_nullable = false;
    uint32_t dimension = 1;
    uint64_t row_count = 0;
    uint16_t snap_version = 10;
    uint8_t domain_key_type = 0;
    bool is_key_attr = false;
    uint8_t interval_part = 0; // 0 = normal, 1 = start, 2 = dur

    static CachedAttrCol from_raw(
        std::string col_name,
        const void* d_ptr,
        uint64_t d_bytes,
        const void* o_ptr,
        uint64_t o_bytes,
        uint8_t tc,
        uint32_t dim,
        uint64_t rows,
        uint16_t ver,
        bool key_attr = false,
        uint8_t key_type = 0,
        uint8_t iv_part = 0
    ) {
        CachedAttrCol c;
        c.name = std::move(col_name);
        c.raw_data = static_cast<const uint8_t*>(d_ptr);
        c.data_bytes = d_bytes;
        c.offsets_ptr = static_cast<const uint32_t*>(o_ptr);
        c.offsets_bytes = o_bytes;
        c.base_tc = static_cast<uint8_t>(tc & 0x7Fu);
        c.is_nullable = (tc & 0x80u) != 0;
        c.dimension = dim;
        c.row_count = rows;
        c.snap_version = ver;
        c.is_key_attr = key_attr;
        c.domain_key_type = key_type;
        c.interval_part = iv_part;

        if (!c.raw_data) return c;
        if (c.is_nullable) {
            uint64_t eff_rows = rows;
            if (c.offsets_ptr != nullptr && c.offsets_bytes >= 8) {
                eff_rows = (c.offsets_bytes / 4ULL) - 1ULL;
            }
            c.bitmap_raw_bytes = ((eff_rows + 63ULL) / 64ULL) * 8ULL;
            uint64_t padded_bm = (eff_rows > 0) ? ((c.bitmap_raw_bytes + 127ULL) & ~127ULL) : 0ULL;
            if (eff_rows == 0 || d_bytes < padded_bm) {
                c.val_bytes = nullptr;
                c.avail_bytes = 0;
            } else {
                c.bitmap_ptr = c.raw_data;
                c.val_bytes = c.raw_data + padded_bm;
                c.avail_bytes = d_bytes - padded_bm;
            }
        } else {
            c.val_bytes = c.raw_data;
            c.avail_bytes = d_bytes;
        }
        return c;
    }

    inline impulse::cel::CelValue read(uint64_t u) const {
        using impulse::cel::CelValue;
        if (!val_bytes) return CelValue::make_null();
        if (is_nullable) {
            if (!bitmap_ptr || (u >> 3) >= bitmap_raw_bytes || ((bitmap_ptr[u >> 3] >> (u & 7u)) & 1u) == 0u) {
                return CelValue::make_null();
            }
        }

        if (offsets_ptr != nullptr && offsets_bytes >= 8) {
            if ((u + 2ULL) * 4ULL > offsets_bytes) return CelValue::make_null();
            uint32_t start = offsets_ptr[u];
            uint32_t end   = offsets_ptr[u + 1];
            if (start > end || static_cast<uint64_t>(end) > avail_bytes) return CelValue::make_null();
            if (base_tc == 0x0Cu) {
                std::string hex_out;
                hex_out.reserve(static_cast<size_t>(end - start) * 2);
                for (uint32_t i = start; i < end; ++i) {
                    char buf[4];
                    std::snprintf(buf, sizeof(buf), "%02x", val_bytes[i]);
                    hex_out.append(buf, 2);
                }
                return CelValue::make_string(std::move(hex_out));
            }
            if (end > start && val_bytes[end - 1] == '\0') {
                --end;
            }
            return CelValue::make_string_view(
                std::string_view(reinterpret_cast<const char*>(val_bytes + start), static_cast<size_t>(end - start))
            );
        }

        if (interval_part != 0) {
            if (base_tc == 0x0Du) {
                if ((u + 1ULL) * 8ULL > avail_bytes) return CelValue::make_null();
                int32_t v = 0;
                std::memcpy(&v, val_bytes + u * 8ULL + (interval_part == 2 ? 4ULL : 0ULL), 4);
                return CelValue::make_int(v);
            }
            if (base_tc == 0x0Eu) {
                if ((u + 1ULL) * 16ULL > avail_bytes) return CelValue::make_null();
                int64_t v = 0;
                std::memcpy(&v, val_bytes + u * 16ULL + (interval_part == 2 ? 8ULL : 0ULL), 8);
                return CelValue::make_int(v);
            }
        }

        switch (base_tc) {
            case 0x01: {
                if ((u + 1ULL) > avail_bytes) return CelValue::make_null();
                if (is_key_attr && domain_key_type == 5) {
                    return CelValue::make_uint(val_bytes[u]);
                }
                return CelValue::make_int(static_cast<int64_t>(reinterpret_cast<const int8_t*>(val_bytes)[u]));
            }
            case 0x11: {
                if ((u + 1ULL) > avail_bytes) return CelValue::make_null();
                return CelValue::make_uint(val_bytes[u]);
            }
            case 0x02: {
                if ((u + 1ULL) * 2ULL > avail_bytes) return CelValue::make_null();
                if (is_key_attr && domain_key_type == 6) {
                    uint16_t v = 0;
                    std::memcpy(&v, val_bytes + u * 2ULL, 2);
                    return CelValue::make_uint(v);
                }
                int16_t v = 0;
                std::memcpy(&v, val_bytes + u * 2ULL, 2);
                return CelValue::make_int(v);
            }
            case 0x12: {
                if ((u + 1ULL) * 2ULL > avail_bytes) return CelValue::make_null();
                uint16_t v = 0;
                std::memcpy(&v, val_bytes + u * 2ULL, 2);
                return CelValue::make_uint(v);
            }
            case 0x03: {
                if ((u + 1ULL) * 4ULL > avail_bytes) return CelValue::make_null();
                if (is_key_attr && domain_key_type == 7) {
                    uint32_t v = 0;
                    std::memcpy(&v, val_bytes + u * 4ULL, 4);
                    return CelValue::make_uint(v);
                }
                int32_t v = 0;
                std::memcpy(&v, val_bytes + u * 4ULL, 4);
                return CelValue::make_int(v);
            }
            case 0x13: {
                if ((u + 1ULL) * 4ULL > avail_bytes) return CelValue::make_null();
                uint32_t v = 0;
                std::memcpy(&v, val_bytes + u * 4ULL, 4);
                return CelValue::make_uint(v);
            }
            case 0x04: {
                if ((u + 1ULL) * 8ULL > avail_bytes) return CelValue::make_null();
                if (is_key_attr && domain_key_type == 8) {
                    uint64_t v = 0;
                    std::memcpy(&v, val_bytes + u * 8ULL, 8);
                    return CelValue::make_uint(v);
                }
                int64_t v = 0;
                std::memcpy(&v, val_bytes + u * 8ULL, 8);
                return CelValue::make_int(v);
            }
            case 0x14: {
                if ((u + 1ULL) * 8ULL > avail_bytes) return CelValue::make_null();
                uint64_t v = 0;
                std::memcpy(&v, val_bytes + u * 8ULL, 8);
                return CelValue::make_uint(v);
            }
            case 0x05: {
                if (snap_version >= 10) {
                    if ((u + 1ULL) * 2ULL > avail_bytes) return CelValue::make_null();
                    uint16_t h = 0;
                    std::memcpy(&h, val_bytes + u * 2ULL, 2);
                    return CelValue::make_double(static_cast<double>(impulse_fp16_to_fp32(h)));
                }
                if ((u + 1ULL) * 4ULL > avail_bytes) return CelValue::make_null();
                float f = 0.0f;
                std::memcpy(&f, val_bytes + u * 4ULL, 4);
                return CelValue::make_double(static_cast<double>(f));
            }
            case 0x06: {
                if ((u + 1ULL) * 4ULL > avail_bytes) return CelValue::make_null();
                float f = 0.0f;
                std::memcpy(&f, val_bytes + u * 4ULL, 4);
                return CelValue::make_double(static_cast<double>(f));
            }
            case 0x07: {
                if ((u + 1ULL) * 8ULL > avail_bytes) return CelValue::make_null();
                double d = 0.0;
                std::memcpy(&d, val_bytes + u * 8ULL, 8);
                return CelValue::make_double(d);
            }
            case 0x08: {
                if (row_count > 0 && avail_bytes >= row_count && avail_bytes < row_count * 8ULL) {
                    if ((u + 1ULL) > avail_bytes) return CelValue::make_null();
                    return CelValue::make_bool(val_bytes[u] != 0);
                }
                if ((u + 1ULL) * 8ULL > avail_bytes) return CelValue::make_null();
                int64_t v = 0;
                std::memcpy(&v, val_bytes + u * 8ULL, 8);
                return CelValue::make_int(v);
            }
            case 0x09:
            case 0x0C: {
                if ((u + 1ULL) * 8ULL > avail_bytes) return CelValue::make_null();
                int64_t v = 0;
                std::memcpy(&v, val_bytes + u * 8ULL, 8);
                return CelValue::make_int(v);
            }
            case 0x0A: {
                uint32_t dim = (dimension > 0) ? dimension : 16u;
                if ((u + 1ULL) * dim > avail_bytes) return CelValue::make_null();
                const uint8_t* b = val_bytes + u * dim;
                if (dim == 16) {
                    char buf[40];
                    std::snprintf(
                        buf, sizeof(buf),
                        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                        b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]
                    );
                    return CelValue::make_string(std::string(buf, 36));
                }
                std::string hex_out;
                hex_out.reserve(static_cast<size_t>(dim) * 2);
                for (uint32_t i = 0; i < dim; ++i) {
                    char hex[4];
                    std::snprintf(hex, sizeof(hex), "%02x", b[i]);
                    hex_out.append(hex, 2);
                }
                return CelValue::make_string(std::move(hex_out));
            }
            case 0x0B: {
                if ((u + 1ULL) * 4ULL > avail_bytes) return CelValue::make_null();
                int32_t v = 0;
                std::memcpy(&v, val_bytes + u * 4ULL, 4);
                return CelValue::make_int(v);
            }
            case 0x0D: {
                if (row_count > 0 && avail_bytes >= row_count * 4ULL && avail_bytes < row_count * 8ULL) {
                    if ((u + 1ULL) * 4ULL > avail_bytes) return CelValue::make_null();
                    int32_t v = 0;
                    std::memcpy(&v, val_bytes + u * 4ULL, 4);
                    return CelValue::make_int(v);
                }
                if ((u + 1ULL) * 8ULL > avail_bytes) return CelValue::make_null();
                int32_t start_sec = 0;
                std::memcpy(&start_sec, val_bytes + u * 8ULL, 4);
                return CelValue::make_int(start_sec);
            }
            case 0x0E: {
                if (row_count > 0 && avail_bytes >= row_count * 2ULL && avail_bytes < row_count * 8ULL) {
                    if ((u + 1ULL) * 2ULL > avail_bytes) return CelValue::make_null();
                    uint16_t h = 0;
                    std::memcpy(&h, val_bytes + u * 2ULL, 2);
                    return CelValue::make_double(static_cast<double>(impulse_fp16_to_fp32(h)));
                }
                if (row_count > 0 && avail_bytes >= row_count * 16ULL) {
                    if ((u + 1ULL) * 16ULL > avail_bytes) return CelValue::make_null();
                    int64_t start_ms = 0;
                    std::memcpy(&start_ms, val_bytes + u * 16ULL, 8);
                    return CelValue::make_int(start_ms);
                }
                if ((u + 1ULL) * 8ULL > avail_bytes) return CelValue::make_null();
                int64_t v = 0;
                std::memcpy(&v, val_bytes + u * 8ULL, 8);
                return CelValue::make_int(v);
            }
            default:
                return CelValue::make_null();
        }
    }
};

struct CachedAttrTable {
    std::vector<CachedAttrCol> cols;
    std::unordered_map<std::string, size_t> by_name;

    void add(CachedAttrCol col) {
        if (by_name.find(col.name) != by_name.end()) return;
        size_t idx = cols.size();
        by_name.emplace(col.name, idx);
        cols.push_back(std::move(col));
    }

    inline impulse::cel::CelValue resolve(uint64_t row_idx, const std::string& attr_name) const {
        auto it = by_name.find(attr_name);
        if (it == by_name.end()) {
            auto dot = attr_name.rfind('.');
            if (dot != std::string::npos) {
                it = by_name.find(attr_name.substr(dot + 1));
            }
        }
        if (it == by_name.end()) return impulse::cel::CelValue::make_null();
        return cols[it->second].read(row_idx);
    }
};

inline CachedAttrTable build_domain_attr_cache(const impulse_snapshot_t* snap, uint16_t domain_id) {
    CachedAttrTable tbl;
    if (!snap) return tbl;
    uint16_t ver = snap->header.version;
    uint16_t dom_count = impulse_snapshot_domain_count(snap);

    auto append_dom = [&](uint16_t d) {
        impulse_domain_catalog_entry_t dentry{};
        impulse_snapshot_get_domain_entry(snap, d, &dentry, nullptr);
        uint16_t dcnt = impulse_snapshot_get_domain_attribute_count(snap, d);
        for (uint16_t a = 0; a < dcnt; ++a) {
            const char* aname = nullptr;
            impulse_snapshot_get_domain_attribute_name(snap, d, a, &aname);
            std::string name_str = (aname && aname[0] != '\0') ? aname : ("attr_" + std::to_string(a));
            const void* d_ptr = nullptr;
            uint64_t d_bytes = 0;
            const void* o_ptr = nullptr;
            uint64_t o_bytes = 0;
            uint8_t tc = 0;
            uint32_t dim = 1;
            if (impulse_snapshot_get_domain_attribute_buffers(snap, d, a, &d_ptr, &d_bytes, &o_ptr, &o_bytes, &tc, &dim) == IMPULSE_OK) {
                bool is_key = (a == dentry.key_attr_index);
                tbl.add(CachedAttrCol::from_raw(name_str, d_ptr, d_bytes, o_ptr, o_bytes, tc, dim, dentry.node_count, ver, is_key, dentry.key_type, 0));
                tbl.add(CachedAttrCol::from_raw("attr_" + std::to_string(a), d_ptr, d_bytes, o_ptr, o_bytes, tc, dim, dentry.node_count, ver, is_key, dentry.key_type, 0));
                uint8_t btc = static_cast<uint8_t>(tc & 0x7Fu);
                if (btc == 0x0Du && name_str == "maint_window_sec") {
                    tbl.add(CachedAttrCol::from_raw("maint_window_start_sec", d_ptr, d_bytes, o_ptr, o_bytes, tc, dim, dentry.node_count, ver, false, 0, 1));
                    tbl.add(CachedAttrCol::from_raw("maint_window_dur_sec", d_ptr, d_bytes, o_ptr, o_bytes, tc, dim, dentry.node_count, ver, false, 0, 2));
                } else if (btc == 0x0Eu && name_str == "fault_window_ms") {
                    tbl.add(CachedAttrCol::from_raw("fault_window_start_ms", d_ptr, d_bytes, o_ptr, o_bytes, tc, dim, dentry.node_count, ver, false, 0, 1));
                    tbl.add(CachedAttrCol::from_raw("fault_window_dur_ms", d_ptr, d_bytes, o_ptr, o_bytes, tc, dim, dentry.node_count, ver, false, 0, 2));
                }
            }
        }
    };

    if (domain_id < dom_count) {
        append_dom(domain_id);
        if (ver >= 10 && !tbl.cols.empty()) {
            return tbl;
        }
    } else {
        for (uint16_t d = 0; d < dom_count; ++d) append_dom(d);
    }

    // Legacy v0.9 fallback where node attributes were stored on self-relations
    uint16_t rel_count = impulse_snapshot_relation_count(snap);
    for (uint16_t r = 0; r < rel_count; ++r) {
        impulse_relation_directory_entry_t rentry{};
        if (impulse_snapshot_get_relation_entry(snap, r, &rentry) != IMPULSE_OK) continue;
        if (domain_id < dom_count && rentry.src_domain_id != domain_id) continue;
        for (uint16_t a = 0; a < rentry.attr_count; ++a) {
            const char* aname = nullptr;
            impulse_snapshot_get_attribute_name(snap, r, a, &aname);
            std::string name_str = (aname && aname[0] != '\0') ? aname : ("attr_" + std::to_string(a));
            const void* d_ptr = nullptr;
            uint64_t d_bytes = 0;
            const void* o_ptr = nullptr;
            uint64_t o_bytes = 0;
            uint8_t tc = 0;
            uint32_t dim = 1;
            if (impulse_snapshot_get_attribute_buffers(snap, r, a, &d_ptr, &d_bytes, &o_ptr, &o_bytes, &tc, &dim) == IMPULSE_OK) {
                tbl.add(CachedAttrCol::from_raw(name_str, d_ptr, d_bytes, o_ptr, o_bytes, tc, dim, rentry.node_count, ver));
                tbl.add(CachedAttrCol::from_raw("attr_" + std::to_string(a), d_ptr, d_bytes, o_ptr, o_bytes, tc, dim, rentry.node_count, ver));
            }
        }
    }
    return tbl;
}

inline CachedAttrTable build_relation_attr_cache(const impulse_snapshot_t* snap, uint16_t rel_id) {
    CachedAttrTable tbl;
    if (!snap) return tbl;
    uint16_t ver = snap->header.version;
    impulse_relation_directory_entry_t rentry{};
    if (impulse_snapshot_get_relation_entry(snap, rel_id, &rentry) != IMPULSE_OK) return tbl;
    for (uint16_t a = 0; a < rentry.attr_count; ++a) {
        const char* aname = nullptr;
        impulse_snapshot_get_attribute_name(snap, rel_id, a, &aname);
        std::string name_str = (aname && aname[0] != '\0') ? aname : ("attr_" + std::to_string(a));
        const void* d_ptr = nullptr;
        uint64_t d_bytes = 0;
        const void* o_ptr = nullptr;
        uint64_t o_bytes = 0;
        uint8_t tc = 0;
        uint32_t dim = 1;
        if (impulse_snapshot_get_attribute_buffers(snap, rel_id, a, &d_ptr, &d_bytes, &o_ptr, &o_bytes, &tc, &dim) == IMPULSE_OK) {
            tbl.add(CachedAttrCol::from_raw(name_str, d_ptr, d_bytes, o_ptr, o_bytes, tc, dim, rentry.edge_count, ver));
            tbl.add(CachedAttrCol::from_raw("attr_" + std::to_string(a), d_ptr, d_bytes, o_ptr, o_bytes, tc, dim, rentry.edge_count, ver));
            // Common synonym aliases if needed
            if (name_str == "x_pu") {
                tbl.add(CachedAttrCol::from_raw("reactance_x_pu", d_ptr, d_bytes, o_ptr, o_bytes, tc, dim, rentry.edge_count, ver));
            } else if (name_str == "r_pu") {
                tbl.add(CachedAttrCol::from_raw("resistance_r_pu", d_ptr, d_bytes, o_ptr, o_bytes, tc, dim, rentry.edge_count, ver));
            }
        }
    }
    return tbl;
}

} // namespace impulse::vm

#endif // IMPULSE_VM_FLUENT_INTERNAL_HPP
