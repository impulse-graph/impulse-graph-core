/**
 * @file impulse_node.cpp
 * @brief Impulse Graph Engine Node.js / Bun Zero-Copy C-ABI Native Addon Implementation.
 */

#include <napi.h>
#include "impulse_graph.h"
#include "impulse_vm.h"
#include "vm/impulse_vm_isa.hpp"
#include "vm/impulse_impscm.hpp"
#include "impulse_cypher.hpp"

extern "C" {
struct impulse_stmt;
typedef struct impulse_stmt impulse_stmt_t;
impulse_status_t impulse_stmt_prepare(const impulse_snapshot_t* snapshot, const char* query_text, impulse_stmt_t** out_stmt);
size_t impulse_stmt_buffer_size(const impulse_stmt_t* stmt);
void impulse_stmt_finalize(impulse_stmt_t* stmt);
impulse_status_t impulse_stmt_bind_node(impulse_stmt_t* stmt, const char* param, uint64_t node_id);
impulse_status_t impulse_stmt_bind_int(impulse_stmt_t* stmt, const char* param, int64_t val);
impulse_status_t impulse_stmt_bind_float(impulse_stmt_t* stmt, const char* param, double val);
impulse_status_t impulse_stmt_bind_str(impulse_stmt_t* stmt, const char* param, const char* str);
impulse_status_t impulse_stmt_execute(impulse_stmt_t* stmt, void* buffer, size_t buffer_size);
size_t impulse_stmt_row_count(const impulse_stmt_t* stmt);
const void* impulse_stmt_column_data(const impulse_stmt_t* stmt, uint32_t col_idx);
}
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include "impulse_assert.h"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <deque>
#include <memory>
#include <stdexcept>

// ============================================================================
// Helper Functions for BigInt / Number conversions
// ============================================================================
inline bool TryGetUint64(Napi::Env env, const Napi::Value& val, uint64_t& out) {
    if (val.IsBigInt()) {
        bool lossless = false;
        out = val.As<Napi::BigInt>().Uint64Value(&lossless);
        if (!lossless) {
            Napi::RangeError::New(env, "BigInt value out of unsigned 64-bit integer range").ThrowAsJavaScriptException();
            return false;
        }
        return true;
    }
    if (val.IsNumber()) {
        double dv = val.As<Napi::Number>().DoubleValue();
        if (dv < 0.0) {
            Napi::RangeError::New(env, "Negative number cannot be converted to uint64").ThrowAsJavaScriptException();
            return false;
        }
        out = static_cast<uint64_t>(val.As<Napi::Number>().Int64Value());
        return true;
    }
    Napi::TypeError::New(env, "Expected a BigInt or Number").ThrowAsJavaScriptException();
    return false;
}

inline bool TryGetInt64(Napi::Env env, const Napi::Value& val, int64_t& out) {
    if (val.IsBigInt()) {
        bool lossless = false;
        out = val.As<Napi::BigInt>().Int64Value(&lossless);
        if (!lossless) {
            Napi::RangeError::New(env, "BigInt value out of signed 64-bit integer range").ThrowAsJavaScriptException();
            return false;
        }
        return true;
    }
    if (val.IsNumber()) {
        out = val.As<Napi::Number>().Int64Value();
        return true;
    }
    Napi::TypeError::New(env, "Expected a BigInt or Number").ThrowAsJavaScriptException();
    return false;
}

inline uint64_t GetUint64(const Napi::Value& val) {
    uint64_t out = 0;
    TryGetUint64(val.Env(), val, out);
    return out;
}

inline bool TryGetUint16Index(Napi::Env env, const Napi::Value& val, uint16_t* out, const char* arg_name) {
    if (!val.IsNumber()) {
        Napi::TypeError::New(env, std::string(arg_name) + " must be a number").ThrowAsJavaScriptException();
        return false;
    }
    double d = val.As<Napi::Number>().DoubleValue();
    if (!std::isfinite(d) || std::floor(d) != d || d < 0.0 || d > 65535.0) {
        Napi::RangeError::New(env, std::string(arg_name) + " must be an integer in [0, 65535]").ThrowAsJavaScriptException();
        return false;
    }
    *out = static_cast<uint16_t>(d);
    return true;
}

inline int64_t GetInt64(const Napi::Value& val) {
    int64_t out = 0;
    TryGetInt64(val.Env(), val, out);
    return out;
}

// ============================================================================
// NodeSnapshot Class
// ============================================================================
class NodeSnapshot : public Napi::ObjectWrap<NodeSnapshot> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "Snapshot", {
            InstanceMethod("isReachable", &NodeSnapshot::IsReachable),
            InstanceMethod("header", &NodeSnapshot::Header),
            InstanceMethod("fileSize", &NodeSnapshot::FileSize),
            InstanceMethod("sha256", &NodeSnapshot::Sha256),
            InstanceMethod("domainCount", &NodeSnapshot::DomainCount),
            InstanceMethod("getDomain", &NodeSnapshot::GetDomain),
            InstanceMethod("relationCount", &NodeSnapshot::RelationCount),
            InstanceMethod("getRelation", &NodeSnapshot::GetRelation),
            InstanceMethod("getRelationName", &NodeSnapshot::GetRelationName),
            InstanceMethod("getDomainAttributeCount", &NodeSnapshot::GetDomainAttributeCount),
            InstanceMethod("getDomainAttributeInfo", &NodeSnapshot::GetDomainAttributeInfo),
            InstanceMethod("getAttributeInfo", &NodeSnapshot::GetAttributeInfo),
            InstanceMethod("getDomainAttributeDecimal64", &NodeSnapshot::GetDomainAttributeDecimal64),
            InstanceMethod("getAttributeDecimal64", &NodeSnapshot::GetAttributeDecimal64),
            InstanceMethod("indexCount", &NodeSnapshot::IndexCount),
            InstanceMethod("getIndex", &NodeSnapshot::GetIndex),
            InstanceMethod("getMetadata", &NodeSnapshot::GetMetadata),
            InstanceMethod("getAllMetadata", &NodeSnapshot::GetAllMetadata),
            InstanceMethod("executeCypher", &NodeSnapshot::ExecuteCypher),
            InstanceMethod("close", &NodeSnapshot::Close)
        });

        Napi::FunctionReference* constructor = new Napi::FunctionReference();
        *constructor = Napi::Persistent(func);
        env.SetInstanceData(constructor);

        exports.Set("Snapshot", func);
        return exports;
    }

    NodeSnapshot(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NodeSnapshot>(info) {
        Napi::Env env = info.Env();
        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(env, "String path expected").ThrowAsJavaScriptException();
            return;
        }

        std::string path = info[0].As<Napi::String>().Utf8Value();
        impulse_status_t status = IMPULSE_OK;
        snapshot_ = impulse_snapshot_open(path.c_str(), &status);
        if (status != IMPULSE_OK || !snapshot_) {
            std::string err = impulse_get_last_error();
            Napi::Error::New(env, "Failed to open snapshot: " + err).ThrowAsJavaScriptException();
            return;
        }
        IMPULSE_ASSERT(snapshot_ != nullptr);
    }

    ~NodeSnapshot() {
        CloseInternal();
    }

    const impulse_snapshot_t* RawSnapshot() const { return snapshot_; }

private:
    void CloseInternal() {
        if (snapshot_) {
            impulse_snapshot_close(snapshot_);
            snapshot_ = nullptr;
        }
    }

    Napi::Value Close(const Napi::CallbackInfo& info) {
        CloseInternal();
        return info.Env().Undefined();
    }

    Napi::Value IsReachable(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }

        if (info.Length() < 3) {
            Napi::TypeError::New(env, "At least 3 arguments required: (relationIndex, srcId, tgtId) or (srcDomain, srcId, tgtDomain, tgtId)").ThrowAsJavaScriptException();
            return env.Null();
        }

        uint16_t relation_index = 0;
        uint64_t src_id = 0;
        uint64_t tgt_id = 0;

        if (info.Length() == 3) {
            relation_index = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
            src_id = GetUint64(info[1]);
            tgt_id = GetUint64(info[2]);
        } else {
            // 4 arguments format: srcDomain, srcId, tgtDomain, tgtId
            uint16_t src_domain = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
            src_id = GetUint64(info[1]);
            uint16_t tgt_domain = static_cast<uint16_t>(info[2].As<Napi::Number>().Uint32Value());
            tgt_id = GetUint64(info[3]);

            uint16_t rel_count = impulse_snapshot_relation_count(snapshot_);
            bool found = false;
            for (uint16_t i = 0; i < rel_count; ++i) {
                impulse_relation_directory_entry_t entry;
                if (impulse_snapshot_get_relation_entry(snapshot_, i, &entry) == IMPULSE_OK &&
                    entry.src_domain_id == src_domain &&
                    entry.tgt_domain_id == tgt_domain) {
                    relation_index = i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                return Napi::Boolean::New(env, false);
            }
        }

        IMPULSE_ASSERT(snapshot_ != nullptr);
        bool reachable = impulse_snapshot_is_reachable(snapshot_, relation_index, src_id, tgt_id);
        return Napi::Boolean::New(env, reachable);
    }

    Napi::Value Header(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        impulse_snapshot_header_info_t hdr{};
        impulse_status_t st = impulse_snapshot_get_header(snapshot_, &hdr);
        if (st != IMPULSE_OK) {
            Napi::Error::New(env, "Failed to read snapshot header").ThrowAsJavaScriptException();
            return env.Null();
        }
        Napi::Object obj = Napi::Object::New(env);
        obj.Set("magic", Napi::Number::New(env, hdr.magic));
        obj.Set("version", Napi::Number::New(env, hdr.version));
        obj.Set("dataOffset", Napi::Number::New(env, hdr.data_offset));
        obj.Set("domainCount", Napi::Number::New(env, hdr.domain_count));
        obj.Set("relationCount", Napi::Number::New(env, hdr.relation_count));
        obj.Set("indexCount", Napi::Number::New(env, hdr.index_count));
        obj.Set("timestampMs", Napi::BigInt::New(env, hdr.timestamp_ms));
        obj.Set("requiredFeatures", Napi::BigInt::New(env, hdr.required_features));
        obj.Set("footerDirectoryOffset", Napi::BigInt::New(env, hdr.footer_directory_offset));
        obj.Set("footerDirectoryBytes", Napi::BigInt::New(env, hdr.footer_directory_bytes));
        obj.Set("fileSize", Napi::BigInt::New(env, hdr.file_size));
        obj.Set("headerChecksum", Napi::Number::New(env, hdr.header_checksum));
        obj.Set("snapshotUuid", Napi::Buffer<uint8_t>::Copy(env, hdr.snapshot_uuid, 16));
        return obj;
    }

    Napi::Value FileSize(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        return Napi::BigInt::New(env, impulse_snapshot_file_size(snapshot_));
    }

    Napi::Value Sha256(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        uint8_t digest[32] = {};
        if (impulse_snapshot_compute_sha256(snapshot_, digest) != IMPULSE_OK) {
            Napi::Error::New(env, "Failed to compute SHA-256").ThrowAsJavaScriptException();
            return env.Null();
        }
        static const char hex[] = "0123456789abcdef";
        char out[65];
        for (size_t i = 0; i < 32; ++i) {
            out[i * 2] = hex[(digest[i] >> 4) & 0x0F];
            out[i * 2 + 1] = hex[digest[i] & 0x0F];
        }
        out[64] = '\0';
        return Napi::String::New(env, out, 64);
    }

    Napi::Value DomainCount(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        return Napi::Number::New(env, impulse_snapshot_domain_count(snapshot_));
    }

    Napi::Value GetDomain(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        if (info.Length() < 1) {
            Napi::TypeError::New(env, "Expected (domainIndex)").ThrowAsJavaScriptException();
            return env.Null();
        }
        uint16_t idx = 0;
        if (!TryGetUint16Index(env, info[0], &idx, "domainIndex")) return env.Null();
        impulse_domain_catalog_entry_t entry{};
        const char* name = nullptr;
        if (impulse_snapshot_get_domain_entry(snapshot_, idx, &entry, &name) != IMPULSE_OK) {
            Napi::RangeError::New(env, "Invalid domain index").ThrowAsJavaScriptException();
            return env.Null();
        }
        Napi::Object obj = Napi::Object::New(env);
        obj.Set("domainId", Napi::Number::New(env, entry.domain_id));
        obj.Set("keyType", Napi::Number::New(env, entry.key_type));
        obj.Set("denseIdWidth", Napi::Number::New(env, entry.dense_id_width));
        obj.Set("nodeCount", Napi::BigInt::New(env, entry.node_count));
        obj.Set("attrCount", Napi::Number::New(env, entry.attr_count));
        obj.Set("keyAttrIndex", Napi::Number::New(env, entry.key_attr_index));
        obj.Set("name", name ? Napi::String::New(env, name) : Napi::String::New(env, ""));
        return obj;
    }

    Napi::Value RelationCount(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        return Napi::Number::New(env, impulse_snapshot_relation_count(snapshot_));
    }

    Napi::Value GetRelation(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        if (info.Length() < 1) {
            Napi::TypeError::New(env, "Expected (relationIndex)").ThrowAsJavaScriptException();
            return env.Null();
        }
        uint16_t idx = 0;
        if (!TryGetUint16Index(env, info[0], &idx, "relationIndex")) return env.Null();
        impulse_relation_directory_entry_t entry{};
        if (impulse_snapshot_get_relation_entry(snapshot_, idx, &entry) != IMPULSE_OK) {
            Napi::RangeError::New(env, "Invalid relation index").ThrowAsJavaScriptException();
            return env.Null();
        }
        const char* rel_name = nullptr;
        impulse_snapshot_get_relation_name(snapshot_, idx, &rel_name);
        Napi::Object obj = Napi::Object::New(env);
        obj.Set("relationId", Napi::Number::New(env, entry.relation_id));
        obj.Set("name", rel_name ? Napi::String::New(env, rel_name) : Napi::String::New(env, ""));
        obj.Set("srcDomainId", Napi::Number::New(env, entry.src_domain_id));
        obj.Set("tgtDomainId", Napi::Number::New(env, entry.tgt_domain_id));
        obj.Set("encodingId", Napi::Number::New(env, entry.encoding_id));
        obj.Set("nodeIdWidth", Napi::Number::New(env, entry.node_id_width));
        obj.Set("edgeIndexWidth", Napi::Number::New(env, entry.edge_index_width));
        obj.Set("nodeCount", Napi::BigInt::New(env, entry.node_count));
        obj.Set("edgeCount", Napi::BigInt::New(env, entry.edge_count));
        obj.Set("sectionFeatures", Napi::BigInt::New(env, entry.section_features));
        obj.Set("attrCount", Napi::Number::New(env, entry.attr_count));
        obj.Set("csrRowOffOffset", Napi::BigInt::New(env, entry.csr_row_off_offset));
        obj.Set("csrRowOffBytes", Napi::BigInt::New(env, entry.csr_row_off_bytes));
        obj.Set("csrColIdxOffset", Napi::BigInt::New(env, entry.csr_col_idx_offset));
        obj.Set("csrColIdxBytes", Napi::BigInt::New(env, entry.csr_col_idx_bytes));
        obj.Set("cscRowOffOffset", Napi::BigInt::New(env, entry.csc_row_off_offset));
        obj.Set("cscRowOffBytes", Napi::BigInt::New(env, entry.csc_row_off_bytes));
        obj.Set("cscColIdxOffset", Napi::BigInt::New(env, entry.csc_col_idx_offset));
        obj.Set("cscColIdxBytes", Napi::BigInt::New(env, entry.csc_col_idx_bytes));
        obj.Set("cooSrcOffset", Napi::BigInt::New(env, entry.coo_src_offset));
        obj.Set("cooSrcBytes", Napi::BigInt::New(env, entry.coo_src_bytes));
        obj.Set("cooTgtOffset", Napi::BigInt::New(env, entry.coo_tgt_offset));
        obj.Set("cooTgtBytes", Napi::BigInt::New(env, entry.coo_tgt_bytes));
        obj.Set("cooEdgeIdOffset", Napi::BigInt::New(env, entry.coo_edge_id_offset));
        obj.Set("cooEdgeIdBytes", Napi::BigInt::New(env, entry.coo_edge_id_bytes));
        bool is_virt = (entry.section_features & IMP_REL_FEATURE_VIRTUAL) != 0;
        bool has_csr = !is_virt && (entry.csr_row_off_bytes > 0 || entry.csr_col_idx_bytes > 0);
        bool has_csc = !is_virt && (((entry.section_features & IMP_REL_FEATURE_INCLUDE_CSC) != 0) ||
                       entry.csc_row_off_bytes > 0 || entry.csc_col_idx_bytes > 0);
        bool has_coo = !is_virt && (((entry.section_features & IMPULSE_SECTION_FEAT_HAS_COO) != 0) ||
                       (entry.coo_src_bytes > 0 && entry.coo_tgt_bytes > 0));
        bool is_flat_fwd = !is_virt && entry.encoding_id != 3 &&
                           entry.csr_row_off_bytes == 0 && entry.csr_col_idx_bytes > 0;
        bool is_flat_rev = !is_virt && entry.encoding_id != 3 &&
                           entry.csc_row_off_bytes == 0 && entry.csc_col_idx_bytes > 0;
        auto norm_w = [](uint8_t w) -> uint8_t {
            if (w == 16 || w == 2) return 2;
            if (w == 64 || w == 8) return 8;
            return 4;
        };
        uint8_t fallback_w = entry.node_id_width > 0 ? norm_w(entry.node_id_width) : 4;
        uint8_t src_w = fallback_w;
        uint8_t tgt_w = fallback_w;
        uint16_t dcnt = impulse_snapshot_domain_count(snapshot_);
        for (uint16_t di = 0; di < dcnt; ++di) {
            impulse_domain_catalog_entry_t de{};
            if (impulse_snapshot_get_domain_entry(snapshot_, di, &de, nullptr) == IMPULSE_OK) {
                if (de.domain_id == entry.src_domain_id && de.dense_id_width > 0) {
                    src_w = norm_w(de.dense_id_width);
                }
                if (de.domain_id == entry.tgt_domain_id && de.dense_id_width > 0) {
                    tgt_w = norm_w(de.dense_id_width);
                }
            }
        }
        obj.Set("srcNodeIdWidth", Napi::Number::New(env, src_w));
        obj.Set("tgtNodeIdWidth", Napi::Number::New(env, tgt_w));
        obj.Set("hasCsr", Napi::Boolean::New(env, has_csr));
        obj.Set("hasCsc", Napi::Boolean::New(env, has_csc));
        obj.Set("hasCoo", Napi::Boolean::New(env, has_coo));
        obj.Set("isFlatForward", Napi::Boolean::New(env, is_flat_fwd));
        obj.Set("isFlatReverse", Napi::Boolean::New(env, is_flat_rev));
        obj.Set("cooOrder", Napi::Number::New(env, entry.coo_order));
        obj.Set("isVirtual", Napi::Boolean::New(env, is_virt));
        Napi::Array vcomps = Napi::Array::New(env);
        if (is_virt) {
            uint16_t vcnt = 0;
            if (impulse_snapshot_get_relation_virtual_components(snapshot_, idx, nullptr, 0, &vcnt) == IMPULSE_OK && vcnt > 0) {
                std::vector<uint16_t> ids(vcnt);
                if (impulse_snapshot_get_relation_virtual_components(snapshot_, idx, ids.data(), vcnt, &vcnt) == IMPULSE_OK) {
                    for (uint16_t i = 0; i < vcnt; ++i) {
                        vcomps.Set(i, Napi::Number::New(env, ids[i]));
                    }
                }
            }
        }
        obj.Set("virtualComponentIds", vcomps);
        return obj;
    }

    Napi::Value IndexCount(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        return Napi::Number::New(env, impulse_snapshot_get_index_count(snapshot_));
    }

    Napi::Value GetIndex(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        if (info.Length() < 1) {
            Napi::TypeError::New(env, "Expected (indexIdx)").ThrowAsJavaScriptException();
            return env.Null();
        }
        uint16_t idx = 0;
        if (!TryGetUint16Index(env, info[0], &idx, "indexIdx")) return env.Null();
        impulse_index_directory_entry_t entry{};
        const char* name = nullptr;
        if (impulse_snapshot_get_index_entry(snapshot_, idx, &entry, &name) != IMPULSE_OK) {
            Napi::RangeError::New(env, "Invalid secondary index").ThrowAsJavaScriptException();
            return env.Null();
        }
        Napi::Object obj = Napi::Object::New(env);
        obj.Set("indexId", Napi::Number::New(env, entry.index_id));
        obj.Set("domainId", Napi::Number::New(env, entry.domain_id));
        obj.Set("relationId", Napi::Number::New(env, entry.relation_id));
        obj.Set("attributeIndex", Napi::Number::New(env, entry.attribute_index));
        obj.Set("indexType", Napi::Number::New(env, entry.index_type));
        obj.Set("name", name ? Napi::String::New(env, name) : Napi::String::New(env, ""));
        obj.Set("dataOffset", Napi::BigInt::New(env, entry.data_offset));
        obj.Set("dataBytes", Napi::BigInt::New(env, entry.data_bytes));
        obj.Set("payloadFeatureMask", Napi::BigInt::New(env, entry.payload_feature_mask));
        return obj;
    }

    Napi::Value GetMetadata(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(env, "Expected (key: string)").ThrowAsJavaScriptException();
            return env.Null();
        }
        std::string key = info[0].As<Napi::String>().Utf8Value();
        uint32_t lo = 0;
        uint32_t hi = impulse_snapshot_get_metadata_count(snapshot_);
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            const char* k = nullptr;
            const char* v = nullptr;
            if (impulse_snapshot_get_metadata_entry(snapshot_, mid, &k, &v) != IMPULSE_OK || !k) {
                break;
            }
            int cmp = key.compare(k);
            if (cmp == 0) {
                return Napi::String::New(env, v ? v : "");
            }
            if (cmp > 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return env.Null();
    }

    Napi::Value GetAllMetadata(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        Napi::Object obj = Napi::Object::New(env);
        uint32_t cnt = impulse_snapshot_get_metadata_count(snapshot_);
        for (uint32_t i = 0; i < cnt; ++i) {
            const char* k = nullptr;
            const char* v = nullptr;
            if (impulse_snapshot_get_metadata_entry(snapshot_, i, &k, &v) == IMPULSE_OK && k && v) {
                obj.DefineProperty(Napi::PropertyDescriptor::Value(
                    k, Napi::String::New(env, v), napi_default_jsproperty));
            }
        }
        return obj;
    }

    Napi::Value GetRelationName(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        if (info.Length() < 1) {
            Napi::TypeError::New(env, "Expected (relationIndex)").ThrowAsJavaScriptException();
            return env.Null();
        }
        uint16_t idx = 0;
        if (!TryGetUint16Index(env, info[0], &idx, "relationIndex")) return env.Null();
        const char* name = nullptr;
        if (impulse_snapshot_get_relation_name(snapshot_, idx, &name) == IMPULSE_OK && name) {
            return Napi::String::New(env, name);
        }
        return env.Null();
    }

    Napi::Value GetDomainAttributeCount(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        if (info.Length() < 1) {
            Napi::TypeError::New(env, "Expected (domainIndex)").ThrowAsJavaScriptException();
            return env.Null();
        }
        uint16_t dom_idx = 0;
        if (!TryGetUint16Index(env, info[0], &dom_idx, "domainIndex")) return env.Null();
        return Napi::Number::New(env, impulse_snapshot_get_domain_attribute_count(snapshot_, dom_idx));
    }

    Napi::Value BuildAttributeInfoObject(
        Napi::Env env,
        const impulse_attribute_descriptor_t& desc,
        const char* name
    ) {
        uint8_t base_type = desc.type_code & 0x7Fu;
        Napi::Object obj = Napi::Object::New(env);
        obj.Set("name", name ? Napi::String::New(env, name) : Napi::String::New(env, ""));
        obj.Set("typeCode", Napi::Number::New(env, desc.type_code));
        obj.Set("baseTypeCode", Napi::Number::New(env, base_type));
        obj.Set("isNullable", Napi::Boolean::New(env, (desc.type_code & 0x80u) != 0));
        obj.Set("dimension", Napi::Number::New(env, desc.dimension));
        obj.Set("dataOffset", Napi::BigInt::New(env, desc.data_offset));
        obj.Set("dataBytes", Napi::BigInt::New(env, desc.data_bytes));
        obj.Set("offsetsOffset", Napi::BigInt::New(env, desc.offsets_offset));
        obj.Set("offsetsBytes", Napi::BigInt::New(env, desc.offsets_bytes));
        if (base_type == 0x15u) {
            obj.Set("precision", Napi::Number::New(env, desc.reserved2));
            obj.Set("scale", Napi::Number::New(env, desc.reserved1));
        }
        return obj;
    }

    Napi::Value GetDomainAttributeInfo(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        if (info.Length() < 2) {
            Napi::TypeError::New(env, "Expected (domainIndex, attrIndex)").ThrowAsJavaScriptException();
            return env.Null();
        }
        uint16_t dom_idx = 0;
        uint16_t attr_idx = 0;
        if (!TryGetUint16Index(env, info[0], &dom_idx, "domainIndex")) return env.Null();
        if (!TryGetUint16Index(env, info[1], &attr_idx, "attrIndex")) return env.Null();
        impulse_attribute_descriptor_t desc{};
        const char* name = nullptr;
        impulse_status_t st = impulse_snapshot_get_domain_attribute_descriptor(
            snapshot_, dom_idx, attr_idx, &desc, &name);
        if (st != IMPULSE_OK) {
            Napi::RangeError::New(env, "Failed to read domain attribute info: code=" + std::to_string(st)).ThrowAsJavaScriptException();
            return env.Null();
        }
        return BuildAttributeInfoObject(env, desc, name);
    }

    Napi::Value GetAttributeInfo(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        if (info.Length() < 2) {
            Napi::TypeError::New(env, "Expected (relationIndex, attrIndex)").ThrowAsJavaScriptException();
            return env.Null();
        }
        uint16_t rel_idx = 0;
        uint16_t attr_idx = 0;
        if (!TryGetUint16Index(env, info[0], &rel_idx, "relationIndex")) return env.Null();
        if (!TryGetUint16Index(env, info[1], &attr_idx, "attrIndex")) return env.Null();
        impulse_attribute_descriptor_t desc{};
        const char* name = nullptr;
        impulse_status_t st = impulse_snapshot_get_attribute_descriptor(
            snapshot_, rel_idx, attr_idx, &desc, &name);
        if (st != IMPULSE_OK) {
            Napi::RangeError::New(env, "Failed to read relation attribute info: code=" + std::to_string(st)).ThrowAsJavaScriptException();
            return env.Null();
        }
        return BuildAttributeInfoObject(env, desc, name);
    }

    Napi::Value DecodeDecimal64Buffer(
        Napi::Env env,
        const void* data,
        uint64_t data_bytes,
        uint8_t type_code,
        uint32_t dimension,
        uint16_t precision,
        uint8_t scale,
        uint64_t row_count
    ) {
        bool is_nullable = (type_code & 0x80u) != 0;
        const uint8_t* raw = static_cast<const uint8_t*>(data);
        Napi::Value valid_bits_val = env.Null();
        const uint8_t* payload = raw;
        uint64_t payload_bytes = data_bytes;

        if (is_nullable) {
            if (row_count == 0) {
                if (data_bytes > 0) {
                    Napi::Error::New(env, "Nullable DECIMAL64 buffer has non-zero payload with zero rowCount").ThrowAsJavaScriptException();
                    return env.Null();
                }
            } else if (raw) {
                uint64_t words = (row_count + 63ULL) / 64ULL;
                uint64_t raw_bm = words * 8ULL;
                uint64_t padded_bm = (raw_bm + 127ULL) & ~127ULL;
                if (data_bytes < padded_bm) {
                    Napi::Error::New(env, "Nullable DECIMAL64 buffer smaller than validity bitmap").ThrowAsJavaScriptException();
                    return env.Null();
                }
                Napi::BigUint64Array bm = Napi::BigUint64Array::New(env, static_cast<size_t>(words));
                std::memcpy(bm.Data(), raw, static_cast<size_t>(raw_bm));
                valid_bits_val = bm;
                payload = raw + padded_bm;
                payload_bytes = data_bytes - padded_bm;
            }
        }

        if ((payload_bytes % 8ULL) != 0ULL) {
            Napi::Error::New(env, "DECIMAL64 payload byte length is not a multiple of 8").ThrowAsJavaScriptException();
            return env.Null();
        }

        size_t elem_count = static_cast<size_t>(payload_bytes / 8ULL);
        Napi::BigInt64Array unscaled = Napi::BigInt64Array::New(env, elem_count);
        if (elem_count > 0 && payload) {
            std::memcpy(unscaled.Data(), payload, elem_count * sizeof(int64_t));
        }

        Napi::Object obj = Napi::Object::New(env);
        obj.Set("precision", Napi::Number::New(env, precision));
        obj.Set("scale", Napi::Number::New(env, scale));
        obj.Set("isNullable", Napi::Boolean::New(env, is_nullable));
        obj.Set("dimension", Napi::Number::New(env, dimension));
        obj.Set("rowCount", Napi::Number::New(env, static_cast<double>(row_count)));
        obj.Set("unscaled", unscaled);
        obj.Set("validBits", valid_bits_val);
        return obj;
    }

    Napi::Value GetDomainAttributeDecimal64(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        if (info.Length() < 2) {
            Napi::TypeError::New(env, "Expected (domainIndex, attrIndex)").ThrowAsJavaScriptException();
            return env.Null();
        }
        uint16_t dom_idx = 0;
        uint16_t attr_idx = 0;
        if (!TryGetUint16Index(env, info[0], &dom_idx, "domainIndex")) return env.Null();
        if (!TryGetUint16Index(env, info[1], &attr_idx, "attrIndex")) return env.Null();
        const void* data = nullptr;
        uint64_t data_bytes = 0;
        uint8_t type_code = 0;
        uint32_t dimension = 0;
        impulse_status_t st = impulse_snapshot_get_domain_attribute_buffers(
            snapshot_, dom_idx, attr_idx, &data, &data_bytes, nullptr, nullptr, &type_code, &dimension);
        if (st != IMPULSE_OK) {
            Napi::Error::New(env, "Failed to read domain attribute: code=" + std::to_string(st)).ThrowAsJavaScriptException();
            return env.Null();
        }
        if ((type_code & 0x7Fu) != 0x15u) {
            Napi::TypeError::New(env, "Attribute is not DECIMAL64").ThrowAsJavaScriptException();
            return env.Null();
        }
        uint16_t precision = 18;
        uint8_t scale = 0;
        st = impulse_snapshot_get_domain_attribute_decimal64_meta(snapshot_, dom_idx, attr_idx, &precision, &scale);
        if (st != IMPULSE_OK) {
            Napi::Error::New(env, "Failed to read domain DECIMAL64 metadata: code=" + std::to_string(st)).ThrowAsJavaScriptException();
            return env.Null();
        }
        impulse_domain_catalog_entry_t dom{};
        uint64_t row_count = 0;
        if (impulse_snapshot_get_domain_entry(snapshot_, dom_idx, &dom, nullptr) == IMPULSE_OK) {
            row_count = dom.node_count;
        }
        return DecodeDecimal64Buffer(env, data, data_bytes, type_code, dimension, precision, scale, row_count);
    }

    Napi::Value GetAttributeDecimal64(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        if (info.Length() < 2) {
            Napi::TypeError::New(env, "Expected (relationIndex, attrIndex)").ThrowAsJavaScriptException();
            return env.Null();
        }
        uint16_t rel_idx = 0;
        uint16_t attr_idx = 0;
        if (!TryGetUint16Index(env, info[0], &rel_idx, "relationIndex")) return env.Null();
        if (!TryGetUint16Index(env, info[1], &attr_idx, "attrIndex")) return env.Null();
        const void* data = nullptr;
        uint64_t data_bytes = 0;
        uint8_t type_code = 0;
        uint32_t dimension = 0;
        impulse_status_t st = impulse_snapshot_get_attribute_buffers(
            snapshot_, rel_idx, attr_idx, &data, &data_bytes, nullptr, nullptr, &type_code, &dimension);
        if (st != IMPULSE_OK) {
            Napi::Error::New(env, "Failed to read relation attribute: code=" + std::to_string(st)).ThrowAsJavaScriptException();
            return env.Null();
        }
        if ((type_code & 0x7Fu) != 0x15u) {
            Napi::TypeError::New(env, "Attribute is not DECIMAL64").ThrowAsJavaScriptException();
            return env.Null();
        }
        uint16_t precision = 18;
        uint8_t scale = 0;
        st = impulse_snapshot_get_attribute_decimal64_meta(snapshot_, rel_idx, attr_idx, &precision, &scale);
        if (st != IMPULSE_OK) {
            Napi::Error::New(env, "Failed to read relation DECIMAL64 metadata: code=" + std::to_string(st)).ThrowAsJavaScriptException();
            return env.Null();
        }
        impulse_relation_directory_entry_t rel{};
        uint64_t row_count = 0;
        if (impulse_snapshot_get_relation_entry(snapshot_, rel_idx, &rel) == IMPULSE_OK) {
            row_count = rel.edge_count;
        }
        return DecodeDecimal64Buffer(env, data, data_bytes, type_code, dimension, precision, scale, row_count);
    }

    Napi::Value ExecuteCypher(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) {
            Napi::Error::New(env, "Snapshot is closed").ThrowAsJavaScriptException();
            return env.Null();
        }
        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(env, "Expected (cypherString[, paramsObject])").ThrowAsJavaScriptException();
            return env.Null();
        }
        std::string query = info[0].As<Napi::String>().Utf8Value();
        impulse_stmt_t* stmt = nullptr;
        impulse_status_t st = impulse_stmt_prepare(snapshot_, query.c_str(), &stmt);
        if (st != IMPULSE_OK || !stmt) {
            Napi::Error::New(env, "impulse_stmt_prepare failed: code=" + std::to_string(st)).ThrowAsJavaScriptException();
            return env.Null();
        }
        if (info.Length() >= 2 && info[1].IsObject() && !info[1].IsNull()) {
            Napi::Object params = info[1].As<Napi::Object>();
            Napi::Array keys = params.GetPropertyNames();
            for (uint32_t i = 0; i < keys.Length(); ++i) {
                std::string k = keys.Get(i).As<Napi::String>().Utf8Value();
                Napi::Value v = params.Get(k);
                if (v.IsNumber()) {
                    double dv = v.As<Napi::Number>().DoubleValue();
                    int64_t iv = static_cast<int64_t>(dv);
                    if (static_cast<double>(iv) == dv) {
                        impulse_stmt_bind_int(stmt, k.c_str(), iv);
                        if (iv >= 0) impulse_stmt_bind_node(stmt, k.c_str(), static_cast<uint64_t>(iv));
                    } else {
                        impulse_stmt_bind_float(stmt, k.c_str(), dv);
                    }
                } else if (v.IsBigInt()) {
                    bool lossless = false;
                    uint64_t uv = v.As<Napi::BigInt>().Uint64Value(&lossless);
                    if (!lossless) {
                        impulse_stmt_finalize(stmt);
                        Napi::RangeError::New(env, "BigInt parameter '" + k + "' out of uint64 range").ThrowAsJavaScriptException();
                        return env.Null();
                    }
                    impulse_stmt_bind_node(stmt, k.c_str(), uv);
                } else if (v.IsString()) {
                    std::string sv = v.As<Napi::String>().Utf8Value();
                    impulse_stmt_bind_str(stmt, k.c_str(), sv.c_str());
                }
            }
        }
        size_t needed = impulse_stmt_buffer_size(stmt);
        if (needed < 65536) needed = 65536;
        if (stmt_buf_.size() < needed) stmt_buf_.resize(needed);
        st = impulse_stmt_execute(stmt, stmt_buf_.data(), stmt_buf_.size());
        if (st != IMPULSE_OK) {
            impulse_stmt_finalize(stmt);
            Napi::Error::New(env, "impulse_stmt_execute failed: code=" + std::to_string(st)).ThrowAsJavaScriptException();
            return env.Null();
        }
        size_t rows = impulse_stmt_row_count(stmt);
        const uint64_t* ids = static_cast<const uint64_t*>(impulse_stmt_column_data(stmt, 0));
        if (rows > 0 && ids == nullptr) {
            impulse_stmt_finalize(stmt);
            Napi::Error::New(env, "impulse_stmt_column_data returned null for non-empty result").ThrowAsJavaScriptException();
            return env.Null();
        }
        Napi::Array out = Napi::Array::New(env, rows);
        for (size_t i = 0; i < rows; ++i) {
            out.Set(static_cast<uint32_t>(i), Napi::Number::New(env, static_cast<double>(ids[i])));
        }
        impulse_stmt_finalize(stmt);
        return out;
    }

    impulse_snapshot_t* snapshot_ = nullptr;
    std::vector<uint8_t> stmt_buf_;
};

// ============================================================================
// NodeWriter Class
// ============================================================================
class NodeWriter : public Napi::ObjectWrap<NodeWriter> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "Writer", {
            InstanceMethod("addDomain", &NodeWriter::AddDomain),
            InstanceMethod("setDomainNodeCount", &NodeWriter::SetDomainNodeCount),
            InstanceMethod("addDomainAttributeDecimal64", &NodeWriter::AddDomainAttributeDecimal64),
            InstanceMethod("addRelation", &NodeWriter::AddRelation),
            InstanceMethod("addAttributeDecimal64", &NodeWriter::AddAttributeDecimal64),
            InstanceMethod("setMetadata", &NodeWriter::SetMetadata),
            InstanceMethod("finalize", &NodeWriter::DoFinalize)
        });

        exports.Set("Writer", func);
        return exports;
    }

    NodeWriter(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NodeWriter>(info) {
        Napi::Env env = info.Env();
        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(env, "String outputPath expected").ThrowAsJavaScriptException();
            return;
        }

        std::string path = info[0].As<Napi::String>().Utf8Value();
        uint64_t flags = 0;
        if (info.Length() > 1 && info[1].IsNumber()) {
            flags = info[1].As<Napi::Number>().Int64Value();
        }

        writer_ = impulse_writer_create(path.c_str(), flags);
        if (!writer_) {
            std::string err = impulse_get_last_error();
            Napi::Error::New(env, "Failed to create writer: " + err).ThrowAsJavaScriptException();
            return;
        }
        IMPULSE_ASSERT(writer_ != nullptr);
    }

    ~NodeWriter() {
        if (writer_) {
            impulse_writer_destroy(writer_);
            writer_ = nullptr;
        }
    }

private:
    Napi::Value AddDomain(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!writer_) {
            Napi::Error::New(env, "Writer is closed or uninitialized").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        if (info.Length() < 3) {
            Napi::TypeError::New(env, "3 arguments required: domainId, keyType, name").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        uint16_t domain_id = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
        uint8_t key_type = static_cast<uint8_t>(info[1].As<Napi::Number>().Uint32Value());
        std::string name = info[2].As<Napi::String>().Utf8Value();

        IMPULSE_ASSERT(writer_ != nullptr);
        impulse_status_t status = impulse_writer_add_domain(writer_, domain_id, key_type, name.c_str());
        if (status != IMPULSE_OK) {
            std::string err = impulse_get_last_error();
            Napi::Error::New(env, "Failed to add domain: " + err).ThrowAsJavaScriptException();
        }

        return env.Undefined();
    }

    Napi::Value SetDomainNodeCount(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!writer_) {
            Napi::Error::New(env, "Writer is closed or uninitialized").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        if (info.Length() < 2) {
            Napi::TypeError::New(env, "2 arguments required: domainIndex, nodeCount").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        uint16_t domain_index = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
        uint64_t node_count = GetUint64(info[1]);
        impulse_status_t status = impulse_writer_set_domain_node_count(writer_, domain_index, node_count);
        if (status != IMPULSE_OK) {
            std::string err = impulse_get_last_error();
            Napi::Error::New(env, "Failed to set domain node count: " + err).ThrowAsJavaScriptException();
        }
        return env.Undefined();
    }

    static bool ExtractBytes(const Napi::Value& val, const uint8_t*& ptr, size_t& len) {
        if (val.IsBuffer()) {
            auto buf = val.As<Napi::Buffer<uint8_t>>();
            ptr = buf.Data();
            len = buf.Length();
            return true;
        }
        if (val.IsTypedArray()) {
            auto arr = val.As<Napi::TypedArray>();
            ptr = reinterpret_cast<const uint8_t*>(arr.ArrayBuffer().Data()) + arr.ByteOffset();
            len = arr.ByteLength();
            return true;
        }
        return false;
    }

    Napi::Value AddDomainAttributeDecimal64(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!writer_) {
            Napi::Error::New(env, "Writer is closed or uninitialized").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        if (info.Length() < 5) {
            Napi::TypeError::New(env, "Expected (domainIndex, name, precision, scale, data[, dimension, typeCode])").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        uint16_t domain_index = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
        std::string name = info[1].As<Napi::String>().Utf8Value();
        uint16_t precision = static_cast<uint16_t>(info[2].As<Napi::Number>().Uint32Value());
        uint8_t scale = static_cast<uint8_t>(info[3].As<Napi::Number>().Uint32Value());
        const uint8_t* data_ptr = nullptr;
        size_t data_bytes = 0;
        if (!ExtractBytes(info[4], data_ptr, data_bytes)) {
            Napi::TypeError::New(env, "data must be a Buffer or TypedArray (e.g. BigInt64Array)").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        uint32_t dimension = (info.Length() >= 6 && info[5].IsNumber())
            ? info[5].As<Napi::Number>().Uint32Value() : 1u;
        uint8_t type_code = (info.Length() >= 7 && info[6].IsNumber())
            ? static_cast<uint8_t>(info[6].As<Napi::Number>().Uint32Value()) : 0x15u;

        impulse_status_t status = impulse_writer_add_domain_attribute_decimal64(
            writer_, domain_index, name.c_str(), type_code, dimension,
            precision, scale, data_ptr, static_cast<uint64_t>(data_bytes));
        if (status != IMPULSE_OK) {
            std::string err = impulse_get_last_error();
            Napi::Error::New(env, "Failed to add domain DECIMAL64 attribute: " + err).ThrowAsJavaScriptException();
        }
        return env.Undefined();
    }

    Napi::Value AddAttributeDecimal64(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!writer_) {
            Napi::Error::New(env, "Writer is closed or uninitialized").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        if (info.Length() < 5) {
            Napi::TypeError::New(env, "Expected (relationIndex, name, precision, scale, data[, dimension, typeCode])").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        uint16_t relation_index = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
        std::string name = info[1].As<Napi::String>().Utf8Value();
        uint16_t precision = static_cast<uint16_t>(info[2].As<Napi::Number>().Uint32Value());
        uint8_t scale = static_cast<uint8_t>(info[3].As<Napi::Number>().Uint32Value());
        const uint8_t* data_ptr = nullptr;
        size_t data_bytes = 0;
        if (!ExtractBytes(info[4], data_ptr, data_bytes)) {
            Napi::TypeError::New(env, "data must be a Buffer or TypedArray (e.g. BigInt64Array)").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        uint32_t dimension = (info.Length() >= 6 && info[5].IsNumber())
            ? info[5].As<Napi::Number>().Uint32Value() : 1u;
        uint8_t type_code = (info.Length() >= 7 && info[6].IsNumber())
            ? static_cast<uint8_t>(info[6].As<Napi::Number>().Uint32Value()) : 0x15u;

        impulse_status_t status = impulse_writer_add_attribute_decimal64(
            writer_, relation_index, name.c_str(), type_code, dimension,
            precision, scale, data_ptr, static_cast<uint64_t>(data_bytes));
        if (status != IMPULSE_OK) {
            std::string err = impulse_get_last_error();
            Napi::Error::New(env, "Failed to add relation DECIMAL64 attribute: " + err).ThrowAsJavaScriptException();
        }
        return env.Undefined();
    }

    Napi::Value AddRelation(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!writer_) {
            Napi::Error::New(env, "Writer is closed or uninitialized").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        if (info.Length() < 8) {
            Napi::TypeError::New(env, "8 arguments required: srcDomainId, tgtDomainId, encodingType, nodeCount, edgeCount, sectionFeatures, rowOffsets, colIndicesBytes").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        uint16_t src_domain = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
        uint16_t tgt_domain = static_cast<uint16_t>(info[1].As<Napi::Number>().Uint32Value());
        uint8_t encoding_type = static_cast<uint8_t>(info[2].As<Napi::Number>().Uint32Value());
        uint64_t node_count = GetUint64(info[3]);
        uint64_t edge_count = GetUint64(info[4]);
        uint64_t section_features = GetUint64(info[5]);

        // Row offsets typed array
        if (!info[6].IsTypedArray()) {
            Napi::TypeError::New(env, "rowOffsets must be a TypedArray").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        Napi::TypedArray row_array = info[6].As<Napi::TypedArray>();
        Napi::ArrayBuffer row_buf = row_array.ArrayBuffer();
        const uint8_t* row_bytes = reinterpret_cast<const uint8_t*>(row_buf.Data()) + row_array.ByteOffset();
        size_t row_byte_len = row_array.ByteLength();

        // Col indices buffer / typed array
        const uint8_t* col_bytes = nullptr;
        size_t col_byte_len = 0;
        if (info[7].IsBuffer()) {
            Napi::Buffer<uint8_t> col_buf = info[7].As<Napi::Buffer<uint8_t>>();
            col_bytes = col_buf.Data();
            col_byte_len = col_buf.Length();
        } else if (info[7].IsTypedArray()) {
            Napi::TypedArray col_array = info[7].As<Napi::TypedArray>();
            col_bytes = reinterpret_cast<const uint8_t*>(col_array.ArrayBuffer().Data()) + col_array.ByteOffset();
            col_byte_len = col_array.ByteLength();
        } else {
            Napi::TypeError::New(env, "colIndicesBytes must be Buffer or TypedArray").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        IMPULSE_ASSERT(writer_ != nullptr);
        impulse_status_t status = impulse_writer_add_relation(
            writer_, src_domain, tgt_domain, encoding_type,
            node_count, edge_count, section_features,
            row_bytes, row_byte_len,
            col_bytes, col_byte_len
        );

        if (status != IMPULSE_OK) {
            std::string err = impulse_get_last_error();
            Napi::Error::New(env, "Failed to add relation: " + err).ThrowAsJavaScriptException();
        }

        return env.Undefined();
    }

    Napi::Value SetMetadata(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!writer_) {
            Napi::Error::New(env, "Writer is closed").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        if (info.Length() < 2 || !info[0].IsString() || !info[1].IsString()) {
            Napi::TypeError::New(env, "Expected (key: string, value: string)").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        std::string key = info[0].As<Napi::String>().Utf8Value();
        std::string val = info[1].As<Napi::String>().Utf8Value();
        impulse_status_t st = impulse_writer_set_metadata(writer_, key.c_str(), val.c_str());
        if (st != IMPULSE_OK) {
            Napi::Error::New(env, "Failed to set metadata: code=" + std::to_string(st)).ThrowAsJavaScriptException();
        }
        return env.Undefined();
    }

    Napi::Value DoFinalize(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!writer_) {
            Napi::Error::New(env, "Writer is closed or uninitialized").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        IMPULSE_ASSERT(writer_ != nullptr);
        impulse_status_t status = impulse_writer_finalize(writer_);
        impulse_writer_destroy(writer_);
        writer_ = nullptr;

        if (status != IMPULSE_OK) {
            std::string err = impulse_get_last_error();
            Napi::Error::New(env, "Failed to finalize writer: " + err).ThrowAsJavaScriptException();
        }

        return env.Undefined();
    }

    impulse_writer_t* writer_ = nullptr;
};


// ============================================================================
// NodeVmWorkspace Class (v0.11 Caller-Owned 128B-Aligned Workspace)
// ============================================================================
class NodeVmWorkspace : public Napi::ObjectWrap<NodeVmWorkspace> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "VmWorkspace", {
            InstanceMethod("destroy", &NodeVmWorkspace::Destroy),
            InstanceMethod("execute", &NodeVmWorkspace::Execute),
            InstanceMethod("executeParams", &NodeVmWorkspace::ExecuteParams),
            InstanceMethod("outputCount", &NodeVmWorkspace::OutputCount),
            InstanceMethod("getOutput", &NodeVmWorkspace::GetOutput),
            InstanceMethod("computeIctxV2", &NodeVmWorkspace::ComputeIctxV2),
        });
        exports.Set("VmWorkspace", func);
        return exports;
    }

    NodeVmWorkspace(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NodeVmWorkspace>(info) {
        Napi::Env env = info.Env();
        size_t req = 4 * 1024 * 1024;
        if (info.Length() >= 1 && info[0].IsNumber()) {
            req = static_cast<size_t>(info[0].As<Napi::Number>().Int64Value());
        }
        size_t aligned = ((req + 127) / 128) * 128;
        if (aligned < 65536) aligned = 65536;
#if defined(_MSC_VER)
        buf_ = _aligned_malloc(aligned, 128);
#else
        if (posix_memalign(&buf_, 128, aligned) != 0) buf_ = nullptr;
#endif
        if (!buf_) {
            Napi::Error::New(env, "Failed to allocate 128B-aligned workspace").ThrowAsJavaScriptException();
            return;
        }
        std::memset(buf_, 0, aligned);
        cap_ = aligned;
        impulse_vm_status_t rc = impulse_workspace_init(buf_, cap_, &ws_);
        if (rc != IMPULSE_VM_OK || !ws_) {
            DestroyInternal();
            Napi::Error::New(env, "impulse_workspace_init failed: status=" + std::to_string(rc)).ThrowAsJavaScriptException();
        }
    }

    ~NodeVmWorkspace() { DestroyInternal(); }

private:
    void DestroyInternal() {
        ws_ = nullptr;
        if (buf_) {
#if defined(_MSC_VER)
            _aligned_free(buf_);
#else
            std::free(buf_);
#endif
            buf_ = nullptr;
            cap_ = 0;
        }
    }

    Napi::Value Destroy(const Napi::CallbackInfo& info) {
        DestroyInternal();
        return info.Env().Undefined();
    }

    Napi::Value Execute(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ws_) {
            Napi::Error::New(env, "VmWorkspace has been destroyed").ThrowAsJavaScriptException();
            return env.Null();
        }
        if (info.Length() < 2 || !info[0].IsBuffer()) {
            Napi::TypeError::New(env, "Expected (impbBuffer, snapshot[, seedIds, domainId])").ThrowAsJavaScriptException();
            return env.Null();
        }
        auto buf = info[0].As<Napi::Buffer<uint8_t>>();
        const impulse_snapshot_t* snap = nullptr;
        if (!info[1].IsNull() && !info[1].IsUndefined() && info[1].IsObject()) {
            NodeSnapshot* ns = Napi::ObjectWrap<NodeSnapshot>::Unwrap(info[1].As<Napi::Object>());
            if (ns) snap = ns->RawSnapshot();
        }
        std::vector<uint64_t> seeds;
        if (info.Length() >= 3 && info[2].IsArray()) {
            Napi::Array arr = info[2].As<Napi::Array>();
            seeds.reserve(arr.Length());
            for (uint32_t i = 0; i < arr.Length(); ++i) {
                seeds.push_back(GetUint64(arr.Get(i)));
            }
        }
        uint16_t domain_id = (info.Length() >= 4 && info[3].IsNumber())
            ? static_cast<uint16_t>(info[3].As<Napi::Number>().Uint32Value()) : 0;

        impulse_vm_param_binding_t binding{};
        impulse_vm_params_t params{};
        params.gas_limit = UINT64_MAX;
        if (!seeds.empty()) {
            binding.param_id = 0;
            binding.kind = IMPULSE_VM_PARAM_FRONTIER_IDS;
            binding.domain_id = domain_id;
            binding.count = static_cast<uint32_t>(seeds.size());
            binding.data_ptr = seeds.data();
            params.bindings = &binding;
            params.binding_count = 1;
        }
        size_t out_bytes = 0;
        impulse_vm_status_t rc = impulse_vm_execute(
            buf.Data(), buf.Length(), snap, &params, ws_, nullptr, 0, &out_bytes);
        return Napi::Number::New(env, rc);
    }

    Napi::Value ExecuteParams(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ws_) {
            Napi::Error::New(env, "VmWorkspace has been destroyed").ThrowAsJavaScriptException();
            return env.Null();
        }
        if (info.Length() < 1 || !info[0].IsBuffer()) {
            Napi::TypeError::New(env, "Expected (impbBuffer[, snapshot, bindings, gasLimit])").ThrowAsJavaScriptException();
            return env.Null();
        }
        auto buf = info[0].As<Napi::Buffer<uint8_t>>();
        const impulse_snapshot_t* snap = nullptr;
        if (info.Length() >= 2 && !info[1].IsNull() && !info[1].IsUndefined() && info[1].IsObject()) {
            NodeSnapshot* ns = Napi::ObjectWrap<NodeSnapshot>::Unwrap(info[1].As<Napi::Object>());
            if (ns) snap = ns->RawSnapshot();
        }
        std::vector<impulse_vm_param_binding_t> c_bindings;
        std::deque<std::vector<uint64_t>> id_storage;
        std::deque<std::string> str_storage;

        if (info.Length() >= 3 && info[2].IsArray()) {
            Napi::Array arr = info[2].As<Napi::Array>();
            c_bindings.reserve(arr.Length());
            for (uint32_t i = 0; i < arr.Length(); ++i) {
                Napi::Value elem = arr.Get(i);
                if (!elem.IsObject() || elem.IsNull()) {
                    Napi::TypeError::New(env, "Each binding entry must be an object").ThrowAsJavaScriptException();
                    return env.Null();
                }
                Napi::Object item = elem.As<Napi::Object>();
                Napi::Value pidVal = item.Get("paramId");
                Napi::Value kindVal = item.Get("kind");
                if (!pidVal.IsNumber() || !kindVal.IsNumber()) {
                    Napi::TypeError::New(env, "Binding requires numeric 'paramId' and 'kind'").ThrowAsJavaScriptException();
                    return env.Null();
                }
                impulse_vm_param_binding_t b{};
                b.param_id = pidVal.As<Napi::Number>().Uint32Value();
                b.kind = kindVal.As<Napi::Number>().Uint32Value();
                if (b.kind == IMPULSE_VM_PARAM_SCALAR_NULL) {
                    b.scalar_bits = 0;
                } else if (b.kind == IMPULSE_VM_PARAM_SCALAR_I64) {
                    int64_t v = 0;
                    if (!TryGetInt64(env, item.Get("scalarI64"), v)) return env.Null();
                    std::memcpy(&b.scalar_bits, &v, sizeof(v));
                } else if (b.kind == IMPULSE_VM_PARAM_SCALAR_U64 || b.kind == IMPULSE_VM_PARAM_SCALAR_BOOL) {
                    uint64_t uv = 0;
                    if (!TryGetUint64(env, item.Get("scalarBits"), uv)) return env.Null();
                    b.scalar_bits = uv;
                } else if (b.kind == IMPULSE_VM_PARAM_SCALAR_F64) {
                    Napi::Value fv = item.Get("scalarF64");
                    if (!fv.IsNumber()) {
                        Napi::TypeError::New(env, "SCALAR_F64 binding requires numeric 'scalarF64'").ThrowAsJavaScriptException();
                        return env.Null();
                    }
                    double dv = fv.As<Napi::Number>().DoubleValue();
                    std::memcpy(&b.scalar_bits, &dv, sizeof(dv));
                } else if (b.kind == IMPULSE_VM_PARAM_SCALAR_STR) {
                    Napi::Value sv = item.Get("strVal");
                    if (!sv.IsString()) {
                        Napi::TypeError::New(env, "SCALAR_STR binding requires string 'strVal'").ThrowAsJavaScriptException();
                        return env.Null();
                    }
                    str_storage.push_back(sv.As<Napi::String>().Utf8Value());
                    b.data_ptr = str_storage.back().data();
                    b.count = static_cast<uint32_t>(str_storage.back().size());
                } else if (b.kind == IMPULSE_VM_PARAM_FRONTIER_IDS) {
                    if (item.Has("domainId")) {
                        Napi::Value domVal = item.Get("domainId");
                        if (!domVal.IsNumber()) {
                            Napi::TypeError::New(env, "FRONTIER_IDS 'domainId' must be a number").ThrowAsJavaScriptException();
                            return env.Null();
                        }
                        b.domain_id = static_cast<uint16_t>(domVal.As<Napi::Number>().Uint32Value());
                    }
                    Napi::Value idsVal = item.Get("ids");
                    if (!idsVal.IsArray()) {
                        Napi::TypeError::New(env, "FRONTIER_IDS binding requires array 'ids'").ThrowAsJavaScriptException();
                        return env.Null();
                    }
                    Napi::Array ids = idsVal.As<Napi::Array>();
                    id_storage.emplace_back();
                    auto& vec = id_storage.back();
                    vec.reserve(ids.Length());
                    for (uint32_t j = 0; j < ids.Length(); ++j) {
                        uint64_t id_u64 = 0;
                        if (!TryGetUint64(env, ids.Get(j), id_u64)) return env.Null();
                        vec.push_back(id_u64);
                    }
                    b.data_ptr = vec.empty() ? nullptr : vec.data();
                    b.count = static_cast<uint32_t>(vec.size());
                }
                c_bindings.push_back(b);
            }
        }

        impulse_vm_params_t params{};
        if (info.Length() >= 4 && !info[3].IsNull() && !info[3].IsUndefined()) {
            uint64_t gl = 0;
            if (!TryGetUint64(env, info[3], gl)) return env.Null();
            params.gas_limit = gl;
        } else {
            params.gas_limit = UINT64_MAX;
        }
        if (!c_bindings.empty()) {
            params.bindings = c_bindings.data();
            params.binding_count = static_cast<uint32_t>(c_bindings.size());
        }
        size_t out_bytes = 0;
        impulse_vm_status_t rc = impulse_vm_execute(
            buf.Data(), buf.Length(), snap, &params, ws_, nullptr, 0, &out_bytes);
        return Napi::Number::New(env, rc);
    }

    Napi::Value OutputCount(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(), ws_ ? impulse_vm_get_output_count(ws_) : 0);
    }

    Napi::Value GetOutput(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ws_ || info.Length() < 1 || !info[0].IsNumber()) {
            return env.Null();
        }
        uint16_t slot = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
        impulse_vm_output_view_t view{};
        if (impulse_vm_get_output(ws_, slot, &view) == 0) {
            return env.Null();
        }
        auto is_valid = [&](uint32_t i) -> bool {
            if (!view.valid_bits) return true;
            return ((view.valid_bits[i >> 6] >> (i & 63u)) & 1ULL) != 0ULL;
        };
        Napi::Object obj = Napi::Object::New(env);
        obj.Set("slot", Napi::Number::New(env, view.slot));
        obj.Set("elemTag", Napi::Number::New(env, view.elem_tag));
        obj.Set("length", Napi::Number::New(env, static_cast<double>(view.length)));
        if ((view.elem_tag == 0 || view.elem_tag == 1) && view.i64_data) {
            Napi::Array arr = Napi::Array::New(env, view.length);
            Napi::Array i64_arr = Napi::Array::New(env, view.length);
            for (uint32_t i = 0; i < view.length; ++i) {
                if (!is_valid(i)) {
                    arr.Set(i, env.Null());
                    i64_arr.Set(i, env.Null());
                } else {
                    arr.Set(i, Napi::Number::New(env, static_cast<double>(view.i64_data[i])));
                    if (view.elem_tag == 1) {
                        i64_arr.Set(i, Napi::BigInt::New(env, static_cast<uint64_t>(view.i64_data[i])));
                    } else {
                        i64_arr.Set(i, Napi::BigInt::New(env, view.i64_data[i]));
                    }
                }
            }
            obj.Set("data", arr);
            obj.Set("i64Data", i64_arr);
        } else if (view.elem_tag == 4 && view.f64_data) {
            Napi::Array arr = Napi::Array::New(env, view.length);
            for (uint32_t i = 0; i < view.length; ++i) {
                arr.Set(i, is_valid(i) ? Napi::Number::New(env, view.f64_data[i]) : env.Null());
            }
            obj.Set("data", arr);
        } else if (view.elem_tag == 6 && view.bool_data_bits) {
            Napi::Array arr = Napi::Array::New(env, view.length);
            for (uint32_t i = 0; i < view.length; ++i) {
                if (!is_valid(i)) {
                    arr.Set(i, env.Null());
                } else {
                    bool bv = ((view.bool_data_bits[i >> 6] >> (i & 63u)) & 1ULL) != 0ULL;
                    arr.Set(i, Napi::Boolean::New(env, bv));
                }
            }
            obj.Set("data", arr);
        }
        return obj;
    }

    Napi::Value ComputeIctxV2(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        Napi::Object obj = Napi::Object::New(env);
        if (ws_) {
            impulse_vm_ictx_v2_t d = impulse_vm_compute_ictx_v2(ws_);
            obj.Set("logicalHash", Napi::BigInt::New(env, d.logical_hash));
            obj.Set("physicalHash", Napi::BigInt::New(env, d.physical_hash));
        }
        return obj;
    }

    void* buf_ = nullptr;
    size_t cap_ = 0;
    impulse_vm_workspace_t* ws_ = nullptr;
};

static Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
    NodeSnapshot::Init(env, exports);
    NodeWriter::Init(env, exports);
    NodeVmWorkspace::Init(env, exports);

    Napi::Object opcodes = Napi::Object::New(env);
    for (size_t i = 0; i < 256; ++i) {
        const auto& meta = impulse::vm::kOpcodeMeta[i];
        if (meta.defined && !meta.name.empty()) {
            opcodes.Set(std::string(meta.name), Napi::Number::New(env, static_cast<uint32_t>(i)));
        }
    }
    opcodes.Set("SCALAR_DECIMAL_ALU", Napi::Number::New(env, 0x15));
    opcodes.Set("VEC_DECIMAL_ALU", Napi::Number::New(env, 0x58));
    exports.Set("Opcodes", opcodes);

    Napi::Object vmStatus = Napi::Object::New(env);
    vmStatus.Set("OK", Napi::Number::New(env, IMPULSE_VM_OK));
    vmStatus.Set("ERR_INVALID_OPCODE", Napi::Number::New(env, IMPULSE_VM_ERR_INVALID_OPCODE));
    vmStatus.Set("ERR_OUT_OF_BOUNDS", Napi::Number::New(env, IMPULSE_VM_ERR_OUT_OF_BOUNDS));
    vmStatus.Set("ERR_NULL_SNAPSHOT", Napi::Number::New(env, IMPULSE_VM_ERR_NULL_SNAPSHOT));
    vmStatus.Set("ERR_DOMAIN_MISMATCH", Napi::Number::New(env, IMPULSE_VM_ERR_DOMAIN_MISMATCH));
    vmStatus.Set("ERR_BUFFER_OVERFLOW", Napi::Number::New(env, IMPULSE_VM_ERR_BUFFER_OVERFLOW));
    vmStatus.Set("ERR_GAS_EXHAUSTED", Napi::Number::New(env, IMPULSE_VM_ERR_GAS_EXHAUSTED));
    vmStatus.Set("ERR_TYPE_MISMATCH", Napi::Number::New(env, IMPULSE_VM_ERR_TYPE_MISMATCH));
    vmStatus.Set("ERR_VALIDATION_FAILED", Napi::Number::New(env, IMPULSE_VM_ERR_VALIDATION_FAILED));
    vmStatus.Set("ERR_ARITHMETIC_OVERFLOW", Napi::Number::New(env, IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW));
    vmStatus.Set("ERR_DIV_BY_ZERO", Napi::Number::New(env, IMPULSE_VM_ERR_DIV_BY_ZERO));
    vmStatus.Set("ARITHMETIC_OVERFLOW", Napi::Number::New(env, IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW));
    vmStatus.Set("DIV_BY_ZERO", Napi::Number::New(env, IMPULSE_VM_ERR_DIV_BY_ZERO));
    exports.Set("VmStatus", vmStatus);

    Napi::Object attrType = Napi::Object::New(env);
    attrType.Set("BOOL", Napi::Number::New(env, 0x0F));
    attrType.Set("DECIMAL64", Napi::Number::New(env, 0x15));
    attrType.Set("DECIMAL64_NULLABLE", Napi::Number::New(env, 0x95));
    exports.Set("AttrType", attrType);

    auto pack_compile_result = [](Napi::Env e, const impulse::compiler::ImpScmCompileResult& res) -> Napi::Object {
        Napi::Object out = Napi::Object::New(e);
        out.Set("impbBytes", Napi::Buffer<uint8_t>::Copy(e, res.impb_bytes.data(), res.impb_bytes.size()));

        Napi::Object pmap = Napi::Object::New(e);
        for (const auto& kv : res.param_name_to_id) {
            pmap.Set(kv.first, Napi::Number::New(e, kv.second));
        }
        out.Set("paramNameToId", pmap);

        Napi::Array onames = Napi::Array::New(e, res.output_names.size());
        for (size_t i = 0; i < res.output_names.size(); ++i) {
            onames.Set(static_cast<uint32_t>(i), Napi::String::New(e, res.output_names[i]));
        }
        out.Set("outputNames", onames);

        Napi::Array pschema = Napi::Array::New(e, res.param_schema.size());
        for (size_t i = 0; i < res.param_schema.size(); ++i) {
            const auto& pm = res.param_schema[i];
            Napi::Object d = Napi::Object::New(e);
            d.Set("name", Napi::String::New(e, pm.name));
            d.Set("paramId", Napi::Number::New(e, pm.param_id));
            d.Set("type", Napi::String::New(e, pm.type));
            d.Set("isDecimal", Napi::Boolean::New(e, pm.is_decimal));
            d.Set("precision", Napi::Number::New(e, pm.precision));
            d.Set("scale", Napi::Number::New(e, pm.scale));
            d.Set("domainId", Napi::Number::New(e, pm.domain_id));
            pschema.Set(static_cast<uint32_t>(i), d);
        }
        out.Set("paramSchema", pschema);

        Napi::Array oschema = Napi::Array::New(e, res.output_schema.size());
        for (size_t i = 0; i < res.output_schema.size(); ++i) {
            const auto& om = res.output_schema[i];
            Napi::Object d = Napi::Object::New(e);
            d.Set("name", Napi::String::New(e, om.name));
            d.Set("type", Napi::String::New(e, om.type));
            d.Set("isDecimal", Napi::Boolean::New(e, om.is_decimal));
            d.Set("precision", Napi::Number::New(e, om.precision));
            d.Set("scale", Napi::Number::New(e, om.scale));
            oschema.Set(static_cast<uint32_t>(i), d);
        }
        out.Set("outputSchema", oschema);
        return out;
    };

    exports.Set("compileImpScm", Napi::Function::New(env, [pack_compile_result](const Napi::CallbackInfo& info) -> Napi::Value {
        Napi::Env e = info.Env();
        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(e, "Expected (source[, snapshot, enableOptimizations])").ThrowAsJavaScriptException();
            return e.Null();
        }
        std::string source = info[0].As<Napi::String>().Utf8Value();
        const impulse_snapshot_t* snap = nullptr;
        if (info.Length() >= 2 && !info[1].IsNull() && !info[1].IsUndefined() && info[1].IsObject()) {
            NodeSnapshot* ns = Napi::ObjectWrap<NodeSnapshot>::Unwrap(info[1].As<Napi::Object>());
            if (ns) snap = ns->RawSnapshot();
        }
        bool enable_opt = (info.Length() >= 3 && info[2].IsBoolean())
            ? info[2].As<Napi::Boolean>().Value() : true;
        try {
            auto res = impulse::compiler::ImpScmCompiler::compile(source, snap, enable_opt);
            return pack_compile_result(e, res);
        } catch (const std::exception& ex) {
            Napi::Error::New(e, ex.what()).ThrowAsJavaScriptException();
            return e.Null();
        }
    }));

    exports.Set("compileCypher", Napi::Function::New(env, [pack_compile_result](const Napi::CallbackInfo& info) -> Napi::Value {
        Napi::Env e = info.Env();
        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(e, "Expected (cypher[, snapshot, paramTypes, enableOptimizations])").ThrowAsJavaScriptException();
            return e.Null();
        }
        std::string cypher = info[0].As<Napi::String>().Utf8Value();
        const impulse_snapshot_t* snap = nullptr;
        if (info.Length() >= 2 && !info[1].IsNull() && !info[1].IsUndefined() && info[1].IsObject()) {
            NodeSnapshot* ns = Napi::ObjectWrap<NodeSnapshot>::Unwrap(info[1].As<Napi::Object>());
            if (ns) snap = ns->RawSnapshot();
        }
        auto parse_strict_int = [](const std::string& raw, int& out) -> bool {
            size_t start = 0;
            while (start < raw.size() && std::isspace(static_cast<unsigned char>(raw[start]))) start++;
            size_t end = raw.size();
            while (end > start && std::isspace(static_cast<unsigned char>(raw[end - 1]))) end--;
            if (start >= end) return false;
            std::string trimmed = raw.substr(start, end - start);
            try {
                size_t pos = 0;
                int v = std::stoi(trimmed, &pos, 10);
                if (pos != trimmed.size()) return false;
                out = v;
                return true;
            } catch (...) {
                return false;
            }
        };

        std::unordered_map<std::string, impulse::compiler::CypherCompiler::ParamTypeSpec> pmap;
        if (info.Length() >= 3 && info[2].IsObject() && !info[2].IsNull()) {
            Napi::Object pt = info[2].As<Napi::Object>();
            Napi::Array keys = pt.GetPropertyNames();
            for (uint32_t i = 0; i < keys.Length(); ++i) {
                std::string key = keys.Get(i).As<Napi::String>().Utf8Value();
                Napi::Value val = pt.Get(key);
                impulse::compiler::CypherCompiler::ParamTypeSpec spec{};
                if (val.IsString()) {
                    std::string s = val.As<Napi::String>().Utf8Value();
                    std::string upper = s;
                    std::transform(upper.begin(), upper.end(), upper.begin(),
                                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
                    if (upper.rfind("DECIMAL(", 0) == 0 && !upper.empty() && upper.back() == ')') {
                        std::string inner = upper.substr(8, upper.size() - 9);
                        auto comma = inner.find(',');
                        if (comma == std::string::npos) {
                            Napi::TypeError::New(e, "Invalid DECIMAL(P, S) spec: " + s).ThrowAsJavaScriptException();
                            return e.Null();
                        }
                        int p_int = 0;
                        int s_int = 0;
                        if (!parse_strict_int(inner.substr(0, comma), p_int) ||
                            !parse_strict_int(inner.substr(comma + 1), s_int)) {
                            Napi::TypeError::New(e, "Invalid DECIMAL(P, S) integer components: " + s).ThrowAsJavaScriptException();
                            return e.Null();
                        }
                        if (p_int < 1 || p_int > 18 || s_int < 0 || s_int > p_int) {
                            Napi::RangeError::New(e, "DECIMAL(P, S) out of range (require 1 <= P <= 18 and 0 <= S <= P): " + s).ThrowAsJavaScriptException();
                            return e.Null();
                        }
                        spec.type = "DECIMAL";
                        spec.precision = static_cast<uint16_t>(p_int);
                        spec.scale = static_cast<uint8_t>(s_int);
                    } else {
                        spec.type = upper;
                    }
                } else if (val.IsObject() && !val.IsNull()) {
                    Napi::Object vd = val.As<Napi::Object>();
                    if (vd.Has("type") && vd.Get("type").IsString()) {
                        spec.type = vd.Get("type").As<Napi::String>().Utf8Value();
                    }
                    if (vd.Has("precision") && vd.Get("precision").IsNumber()) {
                        int p_int = vd.Get("precision").As<Napi::Number>().Int32Value();
                        if (p_int < 1 || p_int > 18) {
                            Napi::RangeError::New(e, "DECIMAL precision must be in [1, 18]").ThrowAsJavaScriptException();
                            return e.Null();
                        }
                        spec.precision = static_cast<uint16_t>(p_int);
                    }
                    if (vd.Has("scale") && vd.Get("scale").IsNumber()) {
                        int s_int = vd.Get("scale").As<Napi::Number>().Int32Value();
                        if (s_int < 0 || s_int > 18) {
                            Napi::RangeError::New(e, "DECIMAL scale must be in [0, 18]").ThrowAsJavaScriptException();
                            return e.Null();
                        }
                        spec.scale = static_cast<uint8_t>(s_int);
                    }
                    if (spec.type == "DECIMAL" && (spec.precision < 1 || spec.precision > 18 || spec.scale > spec.precision)) {
                        Napi::RangeError::New(e, "DECIMAL(P, S) out of range (require 1 <= P <= 18 and 0 <= S <= P)").ThrowAsJavaScriptException();
                        return e.Null();
                    }
                    if (vd.Has("domain") && vd.Get("domain").IsString()) {
                        spec.domain_name = vd.Get("domain").As<Napi::String>().Utf8Value();
                    }
                }
                pmap[key] = std::move(spec);
            }
        }
        bool enable_opt = (info.Length() >= 4 && info[3].IsBoolean())
            ? info[3].As<Napi::Boolean>().Value() : true;
        try {
            std::string impscm_src = impulse::compiler::CypherCompiler::lower_to_impscm(cypher, pmap);
            auto res = impulse::compiler::ImpScmCompiler::compile(impscm_src, snap, enable_opt);
            Napi::Object out = pack_compile_result(e, res);
            out.Set("impscmSource", Napi::String::New(e, impscm_src));
            return out;
        } catch (const std::exception& ex) {
            Napi::Error::New(e, ex.what()).ThrowAsJavaScriptException();
            return e.Null();
        }
    }));

    return exports;
}

NODE_API_MODULE(impulse_node_native, InitAll)
