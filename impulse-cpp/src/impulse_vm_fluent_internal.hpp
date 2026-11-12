/**
 * @file impulse_vm_fluent_internal.hpp
 * @brief Shared domain/relation/key catalog resolution helpers for the C++20 Fluent SDK.
 */

#ifndef IMPULSE_VM_FLUENT_INTERNAL_HPP
#define IMPULSE_VM_FLUENT_INTERNAL_HPP

#include "impulse_vm_fluent.hpp"
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

inline std::string resolve_dense_to_key(const impulse_snapshot_t* snap, uint16_t domain_id, uint32_t node_id) {
    if (snap) {
        const void* key_bytes = nullptr;
        size_t key_len = 0;
        if (impulse_snapshot_resolve_dense_id(snap, domain_id, node_id, &key_bytes, &key_len) == IMPULSE_OK &&
            key_bytes != nullptr && key_len > 0) {
            return std::string(static_cast<const char*>(key_bytes), key_len);
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
        uint16_t idx_count = impulse_snapshot_get_index_count(snap);
        for (uint16_t k = 0; k < idx_count; ++k) {
            const char* idx_name = nullptr;
            if (impulse_snapshot_get_index(snap, k, nullptr, nullptr, nullptr, nullptr, nullptr, &idx_name, nullptr, nullptr) == IMPULSE_OK &&
                idx_name && name == idx_name) {
                return k;
            }
        }
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

} // namespace impulse::vm

#endif // IMPULSE_VM_FLUENT_INTERNAL_HPP
