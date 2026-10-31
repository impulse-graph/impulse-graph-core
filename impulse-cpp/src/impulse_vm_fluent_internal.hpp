/**
 * @file impulse_vm_fluent_internal.hpp
 * @brief Shared domain/relation/key catalog resolution helpers for the C++20 Fluent SDK.
 */

#ifndef IMPULSE_VM_FLUENT_INTERNAL_HPP
#define IMPULSE_VM_FLUENT_INTERNAL_HPP

#include "impulse_vm_fluent.hpp"
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

inline uint32_t get_domain_node_count(const impulse_snapshot_t* snap, uint16_t domain_id) {
    if (!snap) return 0;
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

inline uint16_t resolve_attr_or_index_id(const impulse_snapshot_t* snap, const std::string& name) {
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
    }
    return 0;
}

} // namespace impulse::vm

#endif // IMPULSE_VM_FLUENT_INTERNAL_HPP
