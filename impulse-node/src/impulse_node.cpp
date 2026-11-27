/**
 * @file impulse_node.cpp
 * @brief Impulse Graph Engine Node.js / Bun Zero-Copy C-ABI Native Addon Implementation.
 */

#include <napi.h>
#include "impulse_graph.h"
#include "impulse_vm_v11.h"
#include "vm/impulse_vm_isa.hpp"

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
#include <cstdlib>
#include "impulse_assert.h"

#include <cstring>
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <stdexcept>

// ============================================================================
// Helper Functions for BigInt / Number conversions
// ============================================================================
inline uint64_t GetUint64(const Napi::Value& val) {
    if (val.IsBigInt()) {
        bool lossless = false;
        return val.As<Napi::BigInt>().Uint64Value(&lossless);
    }
    return static_cast<uint64_t>(val.As<Napi::Number>().Int64Value());
}

inline int64_t GetInt64(const Napi::Value& val) {
    if (val.IsBigInt()) {
        bool lossless = false;
        return val.As<Napi::BigInt>().Int64Value(&lossless);
    }
    return val.As<Napi::Number>().Int64Value();
}

// ============================================================================
// NodeSnapshot Class
// ============================================================================
class NodeSnapshot : public Napi::ObjectWrap<NodeSnapshot> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "Snapshot", {
            InstanceMethod("isReachable", &NodeSnapshot::IsReachable),
            InstanceMethod("domainCount", &NodeSnapshot::DomainCount),
            InstanceMethod("relationCount", &NodeSnapshot::RelationCount),
            InstanceMethod("getRelationName", &NodeSnapshot::GetRelationName),
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

    Napi::Value DomainCount(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) return Napi::Number::New(env, 0);
        return Napi::Number::New(env, impulse_snapshot_domain_count(snapshot_));
    }

    Napi::Value RelationCount(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_) return Napi::Number::New(env, 0);
        return Napi::Number::New(env, impulse_snapshot_relation_count(snapshot_));
    }

    Napi::Value GetRelationName(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!snapshot_ || info.Length() < 1 || !info[0].IsNumber()) {
            return env.Null();
        }
        uint16_t idx = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
        const char* name = nullptr;
        if (impulse_snapshot_get_relation_name(snapshot_, idx, &name) == IMPULSE_OK && name) {
            return Napi::String::New(env, name);
        }
        return env.Null();
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
            InstanceMethod("addRelation", &NodeWriter::AddRelation),
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
        impulse_vm_status_v11_t rc = impulse_workspace_init(buf_, cap_, &ws_);
        if (rc != IMPULSE_VM_V11_OK || !ws_) {
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

        impulse_vm_param_binding_v11_t binding{};
        impulse_vm_params_v11_t params{};
        params.gas_limit = UINT64_MAX;
        if (!seeds.empty()) {
            binding.param_id = 0;
            binding.kind = IMPULSE_VM_V11_PARAM_FRONTIER_IDS;
            binding.domain_id = domain_id;
            binding.count = static_cast<uint32_t>(seeds.size());
            binding.data_ptr = seeds.data();
            params.bindings = &binding;
            params.binding_count = 1;
        }
        size_t out_bytes = 0;
        impulse_vm_status_v11_t rc = impulse_vm_execute_v11(
            buf.Data(), buf.Length(), snap, &params, ws_, nullptr, 0, &out_bytes);
        return Napi::Number::New(env, rc);
    }

    Napi::Value OutputCount(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(), ws_ ? impulse_vm_v11_get_output_count(ws_) : 0);
    }

    Napi::Value GetOutput(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ws_ || info.Length() < 1 || !info[0].IsNumber()) {
            return env.Null();
        }
        uint16_t slot = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
        impulse_vm_output_view_v11_t view{};
        if (impulse_vm_v11_get_output(ws_, slot, &view) == 0) {
            return env.Null();
        }
        Napi::Object obj = Napi::Object::New(env);
        obj.Set("slot", Napi::Number::New(env, view.slot));
        obj.Set("elemTag", Napi::Number::New(env, view.elem_tag));
        obj.Set("length", Napi::Number::New(env, static_cast<double>(view.length)));
        if ((view.elem_tag == 0 || view.elem_tag == 1) && view.i64_data) {
            Napi::Array arr = Napi::Array::New(env, view.length);
            for (uint64_t i = 0; i < view.length; ++i) {
                arr.Set(static_cast<uint32_t>(i), Napi::Number::New(env, static_cast<double>(view.i64_data[i])));
            }
            obj.Set("data", arr);
        } else if (view.elem_tag == 4 && view.f64_data) {
            Napi::Array arr = Napi::Array::New(env, view.length);
            for (uint64_t i = 0; i < view.length; ++i) {
                arr.Set(static_cast<uint32_t>(i), Napi::Number::New(env, view.f64_data[i]));
            }
            obj.Set("data", arr);
        }
        return obj;
    }

    Napi::Value ComputeIctxV2(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        Napi::Object obj = Napi::Object::New(env);
        if (ws_) {
            impulse_vm_ictx_v2_t d = impulse_vm_v11_compute_ictx_v2(ws_);
            obj.Set("logicalHash", Napi::BigInt::New(env, d.logical_hash));
            obj.Set("physicalHash", Napi::BigInt::New(env, d.physical_hash));
        }
        return obj;
    }

    void* buf_ = nullptr;
    size_t cap_ = 0;
    impulse_vm_workspace_v11_t* ws_ = nullptr;
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
    vmStatus.Set("OK", Napi::Number::New(env, IMPULSE_VM_V11_OK));
    vmStatus.Set("ERR_INVALID_OPCODE", Napi::Number::New(env, IMPULSE_VM_V11_ERR_INVALID_OPCODE));
    vmStatus.Set("ERR_OUT_OF_BOUNDS", Napi::Number::New(env, IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS));
    vmStatus.Set("ERR_NULL_SNAPSHOT", Napi::Number::New(env, IMPULSE_VM_V11_ERR_NULL_SNAPSHOT));
    vmStatus.Set("ERR_DOMAIN_MISMATCH", Napi::Number::New(env, IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH));
    vmStatus.Set("ERR_BUFFER_OVERFLOW", Napi::Number::New(env, IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW));
    vmStatus.Set("ERR_GAS_EXHAUSTED", Napi::Number::New(env, IMPULSE_VM_V11_ERR_GAS_EXHAUSTED));
    vmStatus.Set("ERR_TYPE_MISMATCH", Napi::Number::New(env, IMPULSE_VM_V11_ERR_TYPE_MISMATCH));
    vmStatus.Set("ERR_VALIDATION_FAILED", Napi::Number::New(env, IMPULSE_VM_V11_ERR_VALIDATION_FAILED));
    vmStatus.Set("ERR_ARITHMETIC_OVERFLOW", Napi::Number::New(env, IMPULSE_VM_V11_ERR_ARITHMETIC_OVERFLOW));
    vmStatus.Set("ERR_DIV_BY_ZERO", Napi::Number::New(env, IMPULSE_VM_V11_ERR_DIV_BY_ZERO));
    vmStatus.Set("ARITHMETIC_OVERFLOW", Napi::Number::New(env, IMPULSE_VM_V11_ERR_ARITHMETIC_OVERFLOW));
    vmStatus.Set("DIV_BY_ZERO", Napi::Number::New(env, IMPULSE_VM_V11_ERR_DIV_BY_ZERO));
    exports.Set("VmStatus", vmStatus);

    Napi::Object attrType = Napi::Object::New(env);
    attrType.Set("BOOL", Napi::Number::New(env, 0x0F));
    attrType.Set("DECIMAL64", Napi::Number::New(env, 0x15));
    exports.Set("AttrType", attrType);

    return exports;
}

NODE_API_MODULE(impulse_node_native, InitAll)
