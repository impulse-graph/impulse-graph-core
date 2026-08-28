/**
 * @file impulse_node.cpp
 * @brief Impulse Graph Engine Node.js / Bun Zero-Copy C-ABI Native Addon Implementation.
 */

#include <napi.h>
#include "impulse_graph.h"
#include "impulse_vm.h"
#include "impulse_vm_fluent.hpp"

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
            src_id = GetUint64(info[1]);
            tgt_id = GetUint64(info[3]);
        }

        bool reachable = impulse_snapshot_is_reachable(snapshot_, relation_index, src_id, tgt_id);
        return Napi::Boolean::New(env, reachable);
    }

    impulse_snapshot_t* snapshot_ = nullptr;
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
        }
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

        impulse_status_t status = impulse_writer_finalize(writer_);
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
// NodeVmContext Class
// ============================================================================
class NodeVmContext : public Napi::ObjectWrap<NodeVmContext> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "VmContext", {
            InstanceMethod("destroy", &NodeVmContext::Destroy),
            InstanceMethod("vectorSize", &NodeVmContext::VectorSize),
            InstanceMethod("acquireBitset", &NodeVmContext::AcquireBitset),
            InstanceMethod("releaseBitset", &NodeVmContext::ReleaseBitset),
            InstanceMethod("bitsetAdd", &NodeVmContext::BitsetAdd),
            InstanceMethod("bitsetTest", &NodeVmContext::BitsetTest),
            InstanceMethod("bitsetFill", &NodeVmContext::BitsetFill),
            InstanceMethod("bitsetGetWord", &NodeVmContext::BitsetGetWord),
            InstanceMethod("acquireFloatVector", &NodeVmContext::AcquireFloatVector),
            InstanceMethod("releaseFloatVector", &NodeVmContext::ReleaseFloatVector),
            InstanceMethod("floatVectorSet", &NodeVmContext::FloatVectorSet),
            InstanceMethod("getFloatVector", &NodeVmContext::GetFloatVector),
            InstanceMethod("acquireDoubleVector", &NodeVmContext::AcquireDoubleVector),
            InstanceMethod("releaseDoubleVector", &NodeVmContext::ReleaseDoubleVector),
            InstanceMethod("doubleVectorSet", &NodeVmContext::DoubleVectorSet),
            InstanceMethod("getDoubleVector", &NodeVmContext::GetDoubleVector),
            InstanceMethod("acquireNodeVector", &NodeVmContext::AcquireNodeVector),
            InstanceMethod("releaseNodeVector", &NodeVmContext::ReleaseNodeVector),
            InstanceMethod("getNodeVector", &NodeVmContext::GetNodeVector),
            InstanceMethod("acquireStringVector", &NodeVmContext::AcquireStringVector),
            InstanceMethod("releaseStringVector", &NodeVmContext::ReleaseStringVector),
            InstanceMethod("stringVectorAdd", &NodeVmContext::StringVectorAdd),
            InstanceMethod("stringVectorSize", &NodeVmContext::StringVectorSize),
            InstanceMethod("stringVectorGet", &NodeVmContext::StringVectorGet),
            InstanceMethod("acquireValueMap", &NodeVmContext::AcquireValueMap),
            InstanceMethod("releaseValueMap", &NodeVmContext::ReleaseValueMap),
            InstanceMethod("valueMapSize", &NodeVmContext::ValueMapSize),
            InstanceMethod("valueMapGetKey", &NodeVmContext::ValueMapGetKey),
            InstanceMethod("valueMapGetValue", &NodeVmContext::ValueMapGetValue)
        });

        exports.Set("VmContext", func);
        return exports;
    }

    NodeVmContext(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NodeVmContext>(info) {
        Napi::Env env = info.Env();
        const impulse_snapshot_t* snap_ptr = nullptr;

        if (info.Length() > 0 && info[0].IsObject()) {
            Napi::Object obj = info[0].As<Napi::Object>();
            if (obj.InstanceOf(env.GetInstanceData<Napi::FunctionReference>()->Value())) {
                NodeSnapshot* snap_obj = Napi::ObjectWrap<NodeSnapshot>::Unwrap(obj);
                if (snap_obj) snap_ptr = snap_obj->RawSnapshot();
            }
        }

        ctx_ = impulse_vm_context_create(snap_ptr);
        if (!ctx_) {
            Napi::Error::New(env, "Failed to create impulse_vm_context").ThrowAsJavaScriptException();
        }
    }

    ~NodeVmContext() {
        DestroyInternal();
    }

    impulse_vm_context_t* RawContext() const { return ctx_; }

private:
    void DestroyInternal() {
        if (ctx_) {
            impulse_vm_context_destroy(ctx_);
            ctx_ = nullptr;
        }
    }

    Napi::Value Destroy(const Napi::CallbackInfo& info) {
        DestroyInternal();
        return info.Env().Undefined();
    }

    Napi::Value VectorSize(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_) return Napi::Number::New(env, 0);
        return Napi::Number::New(env, impulse_vm_context_get_vector_size(ctx_));
    }

    Napi::Value AcquireBitset(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_) { Napi::Error::New(env, "Context destroyed").ThrowAsJavaScriptException(); return env.Null(); }
        int handle = impulse_vm_context_acquire_bitset(ctx_);
        return Napi::Number::New(env, handle);
    }

    Napi::Value ReleaseBitset(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (ctx_ && info.Length() > 0) {
            size_t handle = info[0].As<Napi::Number>().Uint32Value();
            impulse_vm_context_release_bitset(ctx_, handle);
        }
        return env.Undefined();
    }

    Napi::Value BitsetAdd(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_) { Napi::Error::New(env, "Context destroyed").ThrowAsJavaScriptException(); return env.Undefined(); }
        if (info.Length() >= 2) {
            size_t handle = info[0].As<Napi::Number>().Uint32Value();
            uint64_t node_id = GetUint64(info[1]);
            impulse_vm_context_bitset_add(ctx_, handle, node_id);
        }
        return env.Undefined();
    }

    Napi::Value BitsetTest(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_) return Napi::Boolean::New(env, false);
        if (info.Length() >= 2) {
            size_t handle = info[0].As<Napi::Number>().Uint32Value();
            uint64_t node_id = GetUint64(info[1]);
            return Napi::Boolean::New(env, impulse_vm_context_bitset_test(ctx_, handle, node_id));
        }
        return Napi::Boolean::New(env, false);
    }

    Napi::Value BitsetFill(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (ctx_ && info.Length() >= 2) {
            size_t handle = info[0].As<Napi::Number>().Uint32Value();
            uint64_t count = GetUint64(info[1]);
            impulse_vm_context_bitset_fill(ctx_, handle, count);
        }
        return env.Undefined();
    }

    Napi::Value BitsetGetWord(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_ || info.Length() < 2) return Napi::BigInt::New(env, (uint64_t)0);
        size_t handle = info[0].As<Napi::Number>().Uint32Value();
        size_t word_idx = info[1].As<Napi::Number>().Uint32Value();
        uint64_t w = impulse_vm_context_bitset_get_word(ctx_, handle, word_idx);
        return Napi::BigInt::New(env, w);
    }

    Napi::Value AcquireFloatVector(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_) return Napi::Number::New(env, -1);
        return Napi::Number::New(env, impulse_vm_context_acquire_float_vector(ctx_));
    }

    Napi::Value ReleaseFloatVector(const Napi::CallbackInfo& info) {
        if (ctx_ && info.Length() > 0) {
            impulse_vm_context_release_float_vector(ctx_, info[0].As<Napi::Number>().Uint32Value());
        }
        return info.Env().Undefined();
    }

    Napi::Value FloatVectorSet(const Napi::CallbackInfo& info) {
        if (ctx_ && info.Length() >= 3) {
            size_t handle = info[0].As<Napi::Number>().Uint32Value();
            size_t idx = info[1].As<Napi::Number>().Uint32Value();
            float val = info[2].As<Napi::Number>().FloatValue();
            impulse_vm_context_float_vector_set(ctx_, handle, idx, val);
        }
        return info.Env().Undefined();
    }

    Napi::Value GetFloatVector(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_ || info.Length() < 1) return Napi::TypedArrayOf<float>::New(env, 0);
        size_t handle = info[0].As<Napi::Number>().Uint32Value();
        const float* ptr = impulse_vm_context_get_float_vector(ctx_, handle);
        size_t sz = impulse_vm_context_get_vector_size(ctx_);
        if (!ptr || sz == 0) return Napi::TypedArrayOf<float>::New(env, 0);
        Napi::Float32Array arr = Napi::Float32Array::New(env, sz);
        std::memcpy(arr.Data(), ptr, sz * sizeof(float));
        return arr;
    }

    Napi::Value AcquireDoubleVector(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_) return Napi::Number::New(env, -1);
        return Napi::Number::New(env, impulse_vm_context_acquire_double_vector(ctx_));
    }

    Napi::Value ReleaseDoubleVector(const Napi::CallbackInfo& info) {
        if (ctx_ && info.Length() > 0) {
            impulse_vm_context_release_double_vector(ctx_, info[0].As<Napi::Number>().Uint32Value());
        }
        return info.Env().Undefined();
    }

    Napi::Value DoubleVectorSet(const Napi::CallbackInfo& info) {
        if (ctx_ && info.Length() >= 3) {
            size_t handle = info[0].As<Napi::Number>().Uint32Value();
            size_t idx = info[1].As<Napi::Number>().Uint32Value();
            double val = info[2].As<Napi::Number>().DoubleValue();
            impulse_vm_context_double_vector_set(ctx_, handle, idx, val);
        }
        return info.Env().Undefined();
    }

    Napi::Value GetDoubleVector(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_ || info.Length() < 1) return Napi::TypedArrayOf<double>::New(env, 0);
        size_t handle = info[0].As<Napi::Number>().Uint32Value();
        const double* ptr = impulse_vm_context_get_double_vector(ctx_, handle);
        size_t sz = impulse_vm_context_get_vector_size(ctx_);
        if (!ptr || sz == 0) return Napi::TypedArrayOf<double>::New(env, 0);
        Napi::Float64Array arr = Napi::Float64Array::New(env, sz);
        std::memcpy(arr.Data(), ptr, sz * sizeof(double));
        return arr;
    }

    Napi::Value AcquireNodeVector(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_) return Napi::Number::New(env, -1);
        return Napi::Number::New(env, impulse_vm_context_acquire_node_vector(ctx_));
    }

    Napi::Value ReleaseNodeVector(const Napi::CallbackInfo& info) {
        if (ctx_ && info.Length() > 0) {
            impulse_vm_context_release_node_vector(ctx_, info[0].As<Napi::Number>().Uint32Value());
        }
        return info.Env().Undefined();
    }

    Napi::Value GetNodeVector(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_ || info.Length() < 1) return Napi::BigUint64Array::New(env, 0);
        size_t handle = info[0].As<Napi::Number>().Uint32Value();
        const uint64_t* ptr = impulse_vm_context_get_node_vector(ctx_, handle);
        size_t sz = impulse_vm_context_get_vector_size(ctx_);
        if (!ptr || sz == 0) return Napi::BigUint64Array::New(env, 0);
        Napi::BigUint64Array arr = Napi::BigUint64Array::New(env, sz);
        std::memcpy(arr.Data(), ptr, sz * sizeof(uint64_t));
        return arr;
    }

    Napi::Value AcquireStringVector(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_) return Napi::Number::New(env, -1);
        return Napi::Number::New(env, impulse_vm_context_acquire_string_vector(ctx_));
    }

    Napi::Value ReleaseStringVector(const Napi::CallbackInfo& info) {
        if (ctx_ && info.Length() > 0) {
            impulse_vm_context_release_string_vector(ctx_, info[0].As<Napi::Number>().Uint32Value());
        }
        return info.Env().Undefined();
    }

    Napi::Value StringVectorAdd(const Napi::CallbackInfo& info) {
        if (ctx_ && info.Length() >= 2) {
            size_t handle = info[0].As<Napi::Number>().Uint32Value();
            std::string str = info[1].As<Napi::String>().Utf8Value();
            owned_strings_.push_back(std::move(str));
            impulse_vm_context_string_vector_add(ctx_, handle, owned_strings_.back().c_str());
        }
        return info.Env().Undefined();
    }

    Napi::Value StringVectorSize(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_ || info.Length() < 1) return Napi::Number::New(env, 0);
        size_t handle = info[0].As<Napi::Number>().Uint32Value();
        return Napi::Number::New(env, impulse_vm_context_string_vector_size(ctx_, handle));
    }

    Napi::Value StringVectorGet(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_ || info.Length() < 2) return env.Null();
        size_t handle = info[0].As<Napi::Number>().Uint32Value();
        size_t index = info[1].As<Napi::Number>().Uint32Value();
        const char* str = impulse_vm_context_string_vector_get(ctx_, handle, index);
        if (!str) return env.Null();
        return Napi::String::New(env, str);
    }

    Napi::Value AcquireValueMap(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_) return Napi::Number::New(env, -1);
        return Napi::Number::New(env, impulse_vm_context_acquire_value_map(ctx_));
    }

    Napi::Value ReleaseValueMap(const Napi::CallbackInfo& info) {
        if (ctx_ && info.Length() > 0) {
            impulse_vm_context_release_value_map(ctx_, info[0].As<Napi::Number>().Uint32Value());
        }
        return info.Env().Undefined();
    }

    Napi::Value ValueMapSize(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_ || info.Length() < 1) return Napi::Number::New(env, 0);
        size_t handle = info[0].As<Napi::Number>().Uint32Value();
        return Napi::Number::New(env, impulse_vm_context_value_map_size(ctx_, handle));
    }

    Napi::Value ValueMapGetKey(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_ || info.Length() < 2) return env.Null();
        size_t handle = info[0].As<Napi::Number>().Uint32Value();
        size_t index = info[1].As<Napi::Number>().Uint32Value();
        const char* key = impulse_vm_context_value_map_get_key(ctx_, handle, index);
        if (!key) return env.Null();
        return Napi::String::New(env, key);
    }

    Napi::Value ValueMapGetValue(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!ctx_ || info.Length() < 2) return Napi::Number::New(env, 0.0);
        size_t handle = info[0].As<Napi::Number>().Uint32Value();
        size_t index = info[1].As<Napi::Number>().Uint32Value();
        return Napi::Number::New(env, impulse_vm_context_value_map_get_value(ctx_, handle, index));
    }

    impulse_vm_context_t* ctx_ = nullptr;
    std::deque<std::string> owned_strings_;
};

// ============================================================================
// NodeVmState Class
// ============================================================================
class NodeVmState : public Napi::ObjectWrap<NodeVmState> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "VmState", {
            InstanceMethod("getRegister", &NodeVmState::GetRegister),
            InstanceMethod("setRegister", &NodeVmState::SetRegister),
            InstanceMethod("getRegisterType", &NodeVmState::GetRegisterType),
            InstanceMethod("getPc", &NodeVmState::GetPc),
            InstanceMethod("getFlags", &NodeVmState::GetFlags)
        });

        exports.Set("VmState", func);
        return exports;
    }

    NodeVmState(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NodeVmState>(info) {
        std::memset(&state_, 0, sizeof(impulse_vm_state_t));
    }

    impulse_vm_state_t* RawState() { return &state_; }

private:
    Napi::Value GetRegister(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (info.Length() < 1) return Napi::BigInt::New(env, (uint64_t)0);
        uint16_t reg = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
        if (reg >= 64) return Napi::BigInt::New(env, (uint64_t)0);
        return Napi::BigInt::New(env, state_.registers[reg]);
    }

    Napi::Value SetRegister(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (info.Length() >= 2) {
            uint16_t reg = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
            if (reg < 64) {
                uint64_t val = GetUint64(info[1]);
                state_.registers[reg] = val;
            }
        }
        return env.Undefined();
    }

    Napi::Value GetRegisterType(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (info.Length() < 1) return Napi::Number::New(env, 0);
        uint16_t reg = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
        if (reg >= 64) return Napi::Number::New(env, 0);
        return Napi::Number::New(env, state_.register_types[reg]);
    }

    Napi::Value GetPc(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(), state_.pc);
    }

    Napi::Value GetFlags(const Napi::CallbackInfo& info) {
        return Napi::BigInt::New(info.Env(), state_.flags);
    }

    alignas(64) impulse_vm_state_t state_;
};

// ============================================================================
// NodeQueryResult Class
// ============================================================================
class NodeQueryResult : public Napi::ObjectWrap<NodeQueryResult> {
public:
    static Napi::FunctionReference constructor;

    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "QueryResult", {
            InstanceMethod("isOk", &NodeQueryResult::IsOk),
            InstanceMethod("asInt", &NodeQueryResult::AsInt),
            InstanceMethod("asFloat", &NodeQueryResult::AsFloat),
            InstanceMethod("asDouble", &NodeQueryResult::AsDouble),
            InstanceMethod("testBitset", &NodeQueryResult::TestBitset),
            InstanceAccessor("status", &NodeQueryResult::GetStatus, nullptr),
            InstanceAccessor("resultRegister", &NodeQueryResult::GetResultRegister, nullptr),
            InstanceAccessor("resultType", &NodeQueryResult::GetResultType, nullptr),
            InstanceAccessor("rawValue", &NodeQueryResult::GetRawValue, nullptr)
        });

        constructor = Napi::Persistent(func);
        constructor.SuppressDestruct();

        exports.Set("QueryResult", func);
        return exports;
    }

    static Napi::Object CreateInstance(Napi::Env env, const impulse::vm::QueryResult& res) {
        Napi::Object obj = constructor.New({});
        NodeQueryResult* ptr = Napi::ObjectWrap<NodeQueryResult>::Unwrap(obj);
        if (ptr) ptr->res_ = res;
        return obj;
    }

    NodeQueryResult(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NodeQueryResult>(info) {}

private:
    Napi::Value IsOk(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), res_.isOk()); }
    Napi::Value AsInt(const Napi::CallbackInfo& info) { return Napi::BigInt::New(info.Env(), res_.asInt()); }
    Napi::Value AsFloat(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), res_.asFloat()); }
    Napi::Value AsDouble(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), res_.asDouble()); }

    Napi::Value TestBitset(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (info.Length() < 2) return Napi::Boolean::New(env, false);

        Napi::Object ctx_obj = info[0].As<Napi::Object>();
        NodeVmContext* ctx = Napi::ObjectWrap<NodeVmContext>::Unwrap(ctx_obj);
        if (!ctx || !ctx->RawContext()) return Napi::Boolean::New(env, false);

        uint64_t node_id = GetUint64(info[1]);
        return Napi::Boolean::New(env, res_.testBitset(ctx->RawContext(), node_id));
    }

    Napi::Value GetStatus(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), res_.status); }
    Napi::Value GetResultRegister(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), res_.result_register); }
    Napi::Value GetResultType(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), res_.result_type); }
    Napi::Value GetRawValue(const Napi::CallbackInfo& info) { return Napi::BigInt::New(info.Env(), res_.raw_value); }

    impulse::vm::QueryResult res_;
};

Napi::FunctionReference NodeQueryResult::constructor;

// ============================================================================
// NodeCompiledQuery Class
// ============================================================================


class NodeCompiledQuery : public Napi::ObjectWrap<NodeCompiledQuery> {
public:

    class VmExecuteContextWorker : public Napi::AsyncWorker {
    public:
        VmExecuteContextWorker(Napi::Env& env, NodeCompiledQuery* query, NodeVmContext* ctx, NodeVmState* st, uint64_t seed)
            : Napi::AsyncWorker(env), query(query), ctx(ctx), st(st), seed(seed), deferred(Napi::Promise::Deferred::New(env)) {}

        Napi::Promise GetPromise() { return deferred.Promise(); }

    protected:
        void Execute() override {
            st->RawState()->query_context = ctx->RawContext();
            status = impulse_vm_execute(
                query->compiled_->bytecode().data(),
                query->compiled_->instructionCount(),
                st->RawState(),
                seed
            );

            res.status = status;
            if (query->compiled_->instructionCount() > 0) {
                res.result_register = query->compiled_->bytecode().data()[query->compiled_->instructionCount() - 1].dst_reg;
            }
            if (res.result_register < 64) {
                res.result_type = static_cast<impulse_register_type_t>(st->RawState()->register_types[res.result_register]);
                res.raw_value = st->RawState()->registers[res.result_register];
            }
        }

        void OnOK() override {
            Napi::Env env = Env();
            Napi::Value result = NodeQueryResult::CreateInstance(env, res);
            deferred.Resolve(result);
        }

        void OnError(const Napi::Error& e) override {
            deferred.Reject(e.Value());
        }

    private:
        NodeCompiledQuery* query;
        friend class VmExecuteContextWorker;
        NodeVmContext* ctx;
        NodeVmState* st;
        uint64_t seed;
        impulse_vm_status_t status = IMPULSE_VM_OK;
        impulse::vm::QueryResult res;
        Napi::Promise::Deferred deferred;
    };

    static Napi::FunctionReference constructor;

    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "CompiledQuery", {
            InstanceMethod("execute", &NodeCompiledQuery::Execute),
            InstanceMethod("executeWithContext", &NodeCompiledQuery::ExecuteWithContext),
            InstanceMethod("executeWithContextAsync", &NodeCompiledQuery::ExecuteWithContextAsync),
            InstanceMethod("instructionCount", &NodeCompiledQuery::InstructionCount),
            InstanceMethod("resultRegister", &NodeCompiledQuery::ResultRegister),
            InstanceMethod("bytecode", &NodeCompiledQuery::Bytecode)
        });

        constructor = Napi::Persistent(func);
        constructor.SuppressDestruct();

        exports.Set("CompiledQuery", func);
        return exports;
    }

    static Napi::Object CreateInstance(Napi::Env env, impulse::vm::CompiledQuery compiled) {
        Napi::Object obj = constructor.New({});
        NodeCompiledQuery* ptr = Napi::ObjectWrap<NodeCompiledQuery>::Unwrap(obj);
        if (ptr) ptr->compiled_ = std::make_unique<impulse::vm::CompiledQuery>(std::move(compiled));
        return obj;
    }

    NodeCompiledQuery(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NodeCompiledQuery>(info) {}

private:
    Napi::Value Execute(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!compiled_) { Napi::Error::New(env, "Uninitialized query").ThrowAsJavaScriptException(); return env.Null(); }
        if (info.Length() < 1 || !info[0].IsObject()) {
            Napi::TypeError::New(env, "Snapshot argument required").ThrowAsJavaScriptException();
            return env.Null();
        }

        NodeSnapshot* snap_obj = Napi::ObjectWrap<NodeSnapshot>::Unwrap(info[0].As<Napi::Object>());
        if (!snap_obj || !snap_obj->RawSnapshot()) {
            Napi::Error::New(env, "Invalid or closed Snapshot").ThrowAsJavaScriptException();
            return env.Null();
        }

        uint64_t input_param = 0;
        if (info.Length() > 1) {
            input_param = GetUint64(info[1]);
        }

        impulse::vm::QueryResult res = compiled_->execute(snap_obj->RawSnapshot(), input_param);
        return NodeQueryResult::CreateInstance(env, res);
    }

    
    Napi::Value ExecuteWithContextAsync(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!compiled_) { Napi::Error::New(env, "Uninitialized query").ThrowAsJavaScriptException(); return env.Null(); }
        if (info.Length() < 2) {
            Napi::TypeError::New(env, "Context and State arguments required").ThrowAsJavaScriptException();
            return env.Null();
        }

        NodeVmContext* ctx = Napi::ObjectWrap<NodeVmContext>::Unwrap(info[0].As<Napi::Object>());
        NodeVmState* st = Napi::ObjectWrap<NodeVmState>::Unwrap(info[1].As<Napi::Object>());

        uint64_t seed = 0;
        if (info.Length() > 2) seed = GetUint64(info[2]);

        VmExecuteContextWorker* worker = new VmExecuteContextWorker(env, this, ctx, st, seed);
        worker->Queue();
        return worker->GetPromise();
    }

    Napi::Value ExecuteWithContext(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!compiled_) { Napi::Error::New(env, "Uninitialized query").ThrowAsJavaScriptException(); return env.Null(); }
        if (info.Length() < 2) {
            Napi::TypeError::New(env, "Context and State arguments required").ThrowAsJavaScriptException();
            return env.Null();
        }

        NodeVmContext* ctx = Napi::ObjectWrap<NodeVmContext>::Unwrap(info[0].As<Napi::Object>());
        NodeVmState* st = Napi::ObjectWrap<NodeVmState>::Unwrap(info[1].As<Napi::Object>());

        if (!ctx || !ctx->RawContext() || !st) {
            Napi::Error::New(env, "Invalid Context or State").ThrowAsJavaScriptException();
            return env.Null();
        }

        uint64_t input_param = 0;
        if (info.Length() > 2) {
            input_param = GetUint64(info[2]);
        }

        impulse::vm::QueryResult res = compiled_->executeWithContext(ctx->RawContext(), st->RawState(), input_param);
        return NodeQueryResult::CreateInstance(env, res);
    }

    Napi::Value InstructionCount(const Napi::CallbackInfo& info) {
        if (!compiled_) return Napi::Number::New(info.Env(), 0);
        return Napi::Number::New(info.Env(), compiled_->instructionCount());
    }

    Napi::Value ResultRegister(const Napi::CallbackInfo& info) {
        if (!compiled_) return Napi::Number::New(info.Env(), 0);
        return Napi::Number::New(info.Env(), compiled_->resultRegister());
    }

    Napi::Value Bytecode(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!compiled_) return Napi::Buffer<uint8_t>::New(env, 0);
        const auto& bc = compiled_->bytecode();
        size_t byte_len = bc.size() * sizeof(impulse_instruction_t);
        Napi::Buffer<uint8_t> buf = Napi::Buffer<uint8_t>::New(env, byte_len);
        std::memcpy(buf.Data(), bc.data(), byte_len);
        return buf;
    }

    std::unique_ptr<impulse::vm::CompiledQuery> compiled_;
};

Napi::FunctionReference NodeCompiledQuery::constructor;

// ============================================================================
// NodeQueryBuilder Class
// ============================================================================
class NodeQueryBuilder : public Napi::ObjectWrap<NodeQueryBuilder> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "QueryBuilder", {
            InstanceMethod("inputNode", &NodeQueryBuilder::InputNode),
            InstanceMethod("inputSet", &NodeQueryBuilder::InputSet),
            InstanceMethod("loadConstInt", &NodeQueryBuilder::LoadConstInt),
            InstanceMethod("loadConstFloat", &NodeQueryBuilder::LoadConstFloat),
            InstanceMethod("loadConstStrPrefix", &NodeQueryBuilder::LoadConstStrPrefix),
            InstanceMethod("loadKeys", &NodeQueryBuilder::LoadKeys),
            InstanceMethod("walkEdge", &NodeQueryBuilder::WalkEdge),
            InstanceMethod("walkEdgeFiltered", &NodeQueryBuilder::WalkEdgeFiltered),
            InstanceMethod("walkEdgePredicate", &NodeQueryBuilder::WalkEdgePredicate),
            InstanceMethod("walkDegree", &NodeQueryBuilder::WalkDegree),
            InstanceMethod("walkReduceSum", &NodeQueryBuilder::WalkReduceSum),
            InstanceMethod("walkCsc", &NodeQueryBuilder::WalkCsc),
            InstanceMethod("filterNode", &NodeQueryBuilder::FilterNode),
            InstanceMethod("filterNodeStrPrefix", &NodeQueryBuilder::FilterNodeStrPrefix),
            InstanceMethod("unionWith", &NodeQueryBuilder::UnionWith),
            InstanceMethod("intersectWith", &NodeQueryBuilder::IntersectWith),
            InstanceMethod("differenceWith", &NodeQueryBuilder::DifferenceWith),
            InstanceMethod("cardinality", &NodeQueryBuilder::Cardinality),
            InstanceMethod("vectorMulAttr", &NodeQueryBuilder::VectorMulAttr),
            InstanceMethod("vectorReduceSum", &NodeQueryBuilder::VectorReduceSum),
            InstanceMethod("vectorDiv", &NodeQueryBuilder::VectorDiv),
            InstanceMethod("l1NormDiff", &NodeQueryBuilder::L1NormDiff),
            InstanceMethod("matrixVectorMul", &NodeQueryBuilder::MatrixVectorMul),
            InstanceMethod("vectorMatrixMul", &NodeQueryBuilder::VectorMatrixMul),
            InstanceMethod("ewiseAdd", &NodeQueryBuilder::EwiseAdd),
            InstanceMethod("ewiseMult", &NodeQueryBuilder::EwiseMult),
            InstanceMethod("reduce", &NodeQueryBuilder::Reduce),
            InstanceMethod("afforest", &NodeQueryBuilder::Afforest),
            InstanceMethod("tcSweepBatch", &NodeQueryBuilder::TcSweepBatch),
            InstanceMethod("brandesForward", &NodeQueryBuilder::BrandesForward),
            InstanceMethod("brandesBackward", &NodeQueryBuilder::BrandesBackward),
            InstanceMethod("deltaStepRelax", &NodeQueryBuilder::DeltaStepRelax),
            InstanceMethod("sampleNeighbors", &NodeQueryBuilder::SampleNeighbors),
            InstanceMethod("randomWalk", &NodeQueryBuilder::RandomWalk),
            InstanceMethod("scatterGather", &NodeQueryBuilder::ScatterGather),
            InstanceMethod("rebacCheck", &NodeQueryBuilder::RebacCheck),
            InstanceMethod("roaringBitmapAnd", &NodeQueryBuilder::RoaringBitmapAnd),
            InstanceMethod("islandDetect", &NodeQueryBuilder::IslandDetect),
            InstanceMethod("sparseMatVec", &NodeQueryBuilder::SparseMatVec),
            InstanceMethod("louvainModularity", &NodeQueryBuilder::LouvainModularity),
            InstanceMethod("kcoreDecomposition", &NodeQueryBuilder::KcoreDecomposition),
            InstanceMethod("motifMatch3", &NodeQueryBuilder::MotifMatch3),
            InstanceMethod("graphIsomorphism", &NodeQueryBuilder::GraphIsomorphism),
            InstanceMethod("mov", &NodeQueryBuilder::Mov),
            InstanceMethod("clearReg", &NodeQueryBuilder::ClearReg),
            InstanceMethod("nop", &NodeQueryBuilder::Nop),
            InstanceMethod("jmp", &NodeQueryBuilder::Jmp),
            InstanceMethod("jz", &NodeQueryBuilder::Jz),
            InstanceMethod("jnz", &NodeQueryBuilder::Jnz),
            InstanceMethod("repeat", &NodeQueryBuilder::Repeat),
            InstanceMethod("repeatUntilStable", &NodeQueryBuilder::RepeatUntilStable),
            InstanceMethod("collectBitset", &NodeQueryBuilder::CollectBitset),
            InstanceMethod("collectArray", &NodeQueryBuilder::CollectArray),
            InstanceMethod("mapDenseToKeys", &NodeQueryBuilder::MapDenseToKeys),
            InstanceMethod("collectValueMap", &NodeQueryBuilder::CollectValueMap),
            InstanceMethod("compile", &NodeQueryBuilder::Compile)
        });

        exports.Set("QueryBuilder", func);
        return exports;
    }

    NodeQueryBuilder(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NodeQueryBuilder>(info) {
        if (info.Length() > 0 && info[0].IsNumber()) {
            uint16_t reg = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
            builder_ = impulse::vm::QueryBuilder(reg);
        }
    }

private:
    Napi::Value InputNode(const Napi::CallbackInfo& info) {
        uint16_t reg = info.Length() > 0 ? static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value()) : 0;
        builder_.inputNode(reg);
        return info.This();
    }

    Napi::Value InputSet(const Napi::CallbackInfo& info) {
        uint16_t reg = info.Length() > 0 ? static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value()) : 0;
        builder_.inputSet(reg);
        return info.This();
    }

    Napi::Value LoadConstInt(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) {
            int64_t val = GetInt64(info[0]);
            uint16_t reg = info.Length() > 1 ? static_cast<uint16_t>(info[1].As<Napi::Number>().Uint32Value()) : 0;
            builder_.loadConstInt(val, reg);
        }
        return info.This();
    }

    Napi::Value LoadConstFloat(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) {
            float val = info[0].As<Napi::Number>().FloatValue();
            uint16_t reg = info.Length() > 1 ? static_cast<uint16_t>(info[1].As<Napi::Number>().Uint32Value()) : 0;
            builder_.loadConstFloat(val, reg);
        }
        return info.This();
    }

    Napi::Value LoadConstStrPrefix(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1 && info[0].IsString()) {
            std::string str = info[0].As<Napi::String>().Utf8Value();
            uint16_t reg = info.Length() > 1 ? static_cast<uint16_t>(info[1].As<Napi::Number>().Uint32Value()) : 0;
            builder_.loadConstStrPrefix(str.c_str(), reg);
        }
        return info.This();
    }

    Napi::Value LoadKeys(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1 && info[0].IsArray()) {
            Napi::Array arr = info[0].As<Napi::Array>();
            std::vector<std::string> str_storage;
            std::vector<const char*> keys;
            str_storage.reserve(arr.Length());
            keys.reserve(arr.Length());
            for (uint32_t i = 0; i < arr.Length(); ++i) {
                Napi::Value v = arr[i];
                str_storage.push_back(v.As<Napi::String>().Utf8Value());
                keys.push_back(str_storage.back().c_str());
            }
            uint16_t reg = info.Length() > 1 ? static_cast<uint16_t>(info[1].As<Napi::Number>().Uint32Value()) : 0;
            builder_.loadKeys(keys.data(), keys.size(), reg);
        }
        return info.This();
    }

    Napi::Value WalkEdge(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) {
            uint16_t rel = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
            uint8_t flags = info.Length() > 1 ? static_cast<uint8_t>(info[1].As<Napi::Number>().Uint32Value()) : 0;
            builder_.walkEdge(rel, flags);
        }
        return info.This();
    }

    Napi::Value WalkEdgeFiltered(const Napi::CallbackInfo& info) {
        if (info.Length() >= 2) {
            uint16_t rel = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
            uint32_t filter = info[1].As<Napi::Number>().Uint32Value();
            builder_.walkEdgeFiltered(rel, filter);
        }
        return info.This();
    }

    Napi::Value WalkEdgePredicate(const Napi::CallbackInfo& info) {
        if (info.Length() >= 2) {
            uint16_t rel = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
            uint32_t filter = info[1].As<Napi::Number>().Uint32Value();
            builder_.walkEdgePredicate(rel, filter);
        }
        return info.This();
    }

    Napi::Value WalkDegree(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) {
            builder_.walkDegree(static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value()));
        }
        return info.This();
    }

    Napi::Value WalkReduceSum(const Napi::CallbackInfo& info) {
        if (info.Length() >= 2) {
            uint16_t rel = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
            uint16_t val = static_cast<uint16_t>(info[1].As<Napi::Number>().Uint32Value());
            builder_.walkReduceSum(rel, val);
        }
        return info.This();
    }

    Napi::Value WalkCsc(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) {
            builder_.walkCsc(static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value()));
        }
        return info.This();
    }

    Napi::Value FilterNode(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) {
            builder_.filterNode(info[0].As<Napi::Number>().Uint32Value());
        }
        return info.This();
    }

    Napi::Value FilterNodeStrPrefix(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1 && info[0].IsString()) {
            std::string prefix = info[0].As<Napi::String>().Utf8Value();
            builder_.filterNodeStrPrefix(prefix.c_str());
        }
        return info.This();
    }

    Napi::Value UnionWith(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) {
            builder_.unionWith(static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value()));
        }
        return info.This();
    }

    Napi::Value IntersectWith(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) {
            builder_.intersectWith(static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value()));
        }
        return info.This();
    }

    Napi::Value DifferenceWith(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) {
            builder_.differenceWith(static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value()));
        }
        return info.This();
    }

    Napi::Value Cardinality(const Napi::CallbackInfo& info) { builder_.cardinality(); return info.This(); }
    Napi::Value VectorMulAttr(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) builder_.vectorMulAttr(static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value()));
        return info.This();
    }
    Napi::Value VectorReduceSum(const Napi::CallbackInfo& info) { builder_.vectorReduceSum(); return info.This(); }
    Napi::Value VectorDiv(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) builder_.vectorDiv(static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value()));
        return info.This();
    }
    Napi::Value L1NormDiff(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) builder_.l1NormDiff(static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value()));
        return info.This();
    }
    Napi::Value MatrixVectorMul(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) {
            uint16_t matrix_reg = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
            uint8_t semiring = info.Length() > 1 ? static_cast<uint8_t>(info[1].As<Napi::Number>().Uint32Value()) : SEMIRING_PLUS_TIMES;
            builder_.matrixVectorMul(matrix_reg, semiring);
        }
        return info.This();
    }
    Napi::Value VectorMatrixMul(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) {
            uint16_t matrix_reg = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
            uint8_t semiring = info.Length() > 1 ? static_cast<uint8_t>(info[1].As<Napi::Number>().Uint32Value()) : SEMIRING_PLUS_TIMES;
            builder_.vectorMatrixMul(matrix_reg, semiring);
        }
        return info.This();
    }
    Napi::Value EwiseAdd(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) {
            uint16_t other = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
            uint8_t op = info.Length() > 1 ? static_cast<uint8_t>(info[1].As<Napi::Number>().Uint32Value()) : BINARY_OP_ADD;
            builder_.ewiseAdd(other, op);
        }
        return info.This();
    }
    Napi::Value EwiseMult(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) {
            uint16_t other = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
            uint8_t op = info.Length() > 1 ? static_cast<uint8_t>(info[1].As<Napi::Number>().Uint32Value()) : BINARY_OP_MUL;
            builder_.ewiseMult(other, op);
        }
        return info.This();
    }
    Napi::Value Reduce(const Napi::CallbackInfo& info) {
        uint8_t op = info.Length() > 0 ? static_cast<uint8_t>(info[0].As<Napi::Number>().Uint32Value()) : BINARY_OP_ADD;
        builder_.reduce(op);
        return info.This();
    }
    Napi::Value Afforest(const Napi::CallbackInfo& info) { builder_.afforest(); return info.This(); }
    Napi::Value TcSweepBatch(const Napi::CallbackInfo& info) { builder_.tcSweepBatch(); return info.This(); }
    Napi::Value BrandesForward(const Napi::CallbackInfo& info) { builder_.brandesForward(); return info.This(); }
    Napi::Value BrandesBackward(const Napi::CallbackInfo& info) { builder_.brandesBackward(); return info.This(); }
    Napi::Value DeltaStepRelax(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) builder_.deltaStepRelax(static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value()));
        return info.This();
    }
    Napi::Value SampleNeighbors(const Napi::CallbackInfo& info) {
        if (info.Length() >= 2) {
            uint16_t rel = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
            int32_t k = info[1].As<Napi::Number>().Int32Value();
            uint32_t seed = info.Length() > 2 ? info[2].As<Napi::Number>().Uint32Value() : 0;
            builder_.sampleNeighbors(rel, k, seed);
        }
        return info.This();
    }
    Napi::Value RandomWalk(const Napi::CallbackInfo& info) {
        if (info.Length() >= 2) {
            uint16_t rel = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
            int32_t steps = info[1].As<Napi::Number>().Int32Value();
            uint32_t seed = info.Length() > 2 ? info[2].As<Napi::Number>().Uint32Value() : 0;
            builder_.randomWalk(rel, steps, seed);
        }
        return info.This();
    }
    Napi::Value ScatterGather(const Napi::CallbackInfo& info) { builder_.scatterGather(); return info.This(); }
    Napi::Value RebacCheck(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) builder_.rebacCheck(info[0].As<Napi::Number>().Uint32Value());
        return info.This();
    }
    Napi::Value RoaringBitmapAnd(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) builder_.roaringBitmapAnd(static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value()));
        return info.This();
    }
    Napi::Value IslandDetect(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) builder_.islandDetect(static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value()));
        return info.This();
    }
    Napi::Value SparseMatVec(const Napi::CallbackInfo& info) { builder_.sparseMatVec(); return info.This(); }
    Napi::Value LouvainModularity(const Napi::CallbackInfo& info) { builder_.louvainModularity(); return info.This(); }
    Napi::Value KcoreDecomposition(const Napi::CallbackInfo& info) { builder_.kcoreDecomposition(); return info.This(); }
    Napi::Value MotifMatch3(const Napi::CallbackInfo& info) { builder_.motifMatch3(); return info.This(); }
    Napi::Value GraphIsomorphism(const Napi::CallbackInfo& info) { builder_.graphIsomorphism(); return info.This(); }

    Napi::Value Mov(const Napi::CallbackInfo& info) {
        if (info.Length() >= 2) {
            uint16_t dst = static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value());
            uint16_t src = static_cast<uint16_t>(info[1].As<Napi::Number>().Uint32Value());
            builder_.mov(dst, src);
        }
        return info.This();
    }
    Napi::Value ClearReg(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) builder_.clearReg(static_cast<uint16_t>(info[0].As<Napi::Number>().Uint32Value()));
        return info.This();
    }
    Napi::Value Nop(const Napi::CallbackInfo& info) { builder_.nop(); return info.This(); }
    Napi::Value Jmp(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) builder_.jmp(info[0].As<Napi::Number>().Int32Value());
        return info.This();
    }
    Napi::Value Jz(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) builder_.jz(info[0].As<Napi::Number>().Int32Value());
        return info.This();
    }
    Napi::Value Jnz(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1) builder_.jnz(info[0].As<Napi::Number>().Int32Value());
        return info.This();
    }

    Napi::Value Repeat(const Napi::CallbackInfo& info) {
        if (info.Length() >= 2 && info[1].IsFunction()) {
            int count = info[0].As<Napi::Number>().Int32Value();
            Napi::Function cb = info[1].As<Napi::Function>();
            builder_.repeat(count, [&](impulse::vm::QueryBuilder& sub) {
                cb.Call(info.This(), { info.This() });
            });
        }
        return info.This();
    }

    Napi::Value RepeatUntilStable(const Napi::CallbackInfo& info) {
        if (info.Length() >= 1 && info[0].IsFunction()) {
            Napi::Function cb = info[0].As<Napi::Function>();
            builder_.repeatUntilStable([&](impulse::vm::QueryBuilder& sub) {
                cb.Call(info.This(), { info.This() });
            });
        }
        return info.This();
    }

    Napi::Value CollectBitset(const Napi::CallbackInfo& info) { builder_.collectBitset(); return info.This(); }
    Napi::Value CollectArray(const Napi::CallbackInfo& info) { builder_.collectArray(); return info.This(); }
    Napi::Value MapDenseToKeys(const Napi::CallbackInfo& info) { builder_.mapDenseToKeys(); return info.This(); }
    Napi::Value CollectValueMap(const Napi::CallbackInfo& info) { builder_.collectValueMap(); return info.This(); }

    Napi::Value Compile(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        impulse::vm::CompiledQuery compiled = builder_.compile();
        return NodeCompiledQuery::CreateInstance(env, std::move(compiled));
    }

    impulse::vm::QueryBuilder builder_;
};

// ============================================================================
// Low-Level Direct Bytecode Execution Function
// ============================================================================
Napi::Value ExecuteBytecode(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2) {
        Napi::TypeError::New(env, "Snapshot and Bytecode Buffer arguments required").ThrowAsJavaScriptException();
        return env.Null();
    }

    NodeSnapshot* snap_obj = Napi::ObjectWrap<NodeSnapshot>::Unwrap(info[0].As<Napi::Object>());
    if (!snap_obj || !snap_obj->RawSnapshot()) {
        Napi::Error::New(env, "Invalid Snapshot").ThrowAsJavaScriptException();
        return env.Null();
    }

    const uint8_t* bc_bytes = nullptr;
    size_t byte_len = 0;

    if (info[1].IsBuffer()) {
        Napi::Buffer<uint8_t> buf = info[1].As<Napi::Buffer<uint8_t>>();
        bc_bytes = buf.Data();
        byte_len = buf.Length();
    } else if (info[1].IsTypedArray()) {
        Napi::TypedArray arr = info[1].As<Napi::TypedArray>();
        bc_bytes = reinterpret_cast<const uint8_t*>(arr.ArrayBuffer().Data()) + arr.ByteOffset();
        byte_len = arr.ByteLength();
    } else {
        Napi::TypeError::New(env, "Bytecode must be a Buffer or TypedArray").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (byte_len % sizeof(impulse_instruction_t) != 0) {
        Napi::Error::New(env, "Bytecode buffer size must be a multiple of 8 bytes").ThrowAsJavaScriptException();
        return env.Null();
    }

    uint64_t input_param = 0;
    if (info.Length() > 2) {
        input_param = GetUint64(info[2]);
    }

    size_t inst_count = byte_len / sizeof(impulse_instruction_t);
    const impulse_instruction_t* instructions = reinterpret_cast<const impulse_instruction_t*>(bc_bytes);

    impulse_vm_context_t* ctx = impulse_vm_context_create(snap_obj->RawSnapshot());
    if (!ctx) {
        Napi::Error::New(env, "Failed to create VM context").ThrowAsJavaScriptException();
        return env.Null();
    }

    alignas(64) impulse_vm_state_t state;
    std::memset(&state, 0, sizeof(impulse_vm_state_t));
    state.query_context = ctx;

    impulse_vm_status_t status = impulse_vm_execute(instructions, inst_count, &state, input_param);

    impulse::vm::QueryResult res;
    res.status = status;
    if (inst_count > 0) {
        res.result_register = instructions[inst_count - 1].dst_reg;
    }
    if (res.result_register < 64) {
        res.result_type = static_cast<impulse_register_type_t>(state.register_types[res.result_register]);
        res.raw_value = state.registers[res.result_register];
    }

    impulse_vm_context_destroy(ctx);
    return NodeQueryResult::CreateInstance(env, res);
}

// ============================================================================
// Addon Initialization & Export Definitions
// ============================================================================

class VmExecuteWorker : public Napi::AsyncWorker {
public:
    VmExecuteWorker(Napi::Env& env, NodeSnapshot* snap_obj, const uint8_t* bc_bytes, size_t byte_len, uint64_t input_param)
        : Napi::AsyncWorker(env),
          snap_obj(snap_obj),
          bc_bytes(bc_bytes),
          inst_count(byte_len / sizeof(impulse_instruction_t)),
          input_param(input_param),
          deferred(Napi::Promise::Deferred::New(env)) {}

    Napi::Promise GetPromise() { return deferred.Promise(); }

protected:
    void Execute() override {
        const impulse_instruction_t* instructions = reinterpret_cast<const impulse_instruction_t*>(bc_bytes);
        ctx = impulse_vm_context_create(snap_obj->RawSnapshot());
        if (!ctx) {
            SetError("Failed to create VM context");
            return;
        }

        std::memset(&state, 0, sizeof(impulse_vm_state_t));
        state.query_context = ctx;

        status = impulse_vm_execute(instructions, inst_count, &state, input_param);

        res.status = status;
        if (inst_count > 0) {
            res.result_register = instructions[inst_count - 1].dst_reg;
        }
        if (res.result_register < 64) {
            res.result_type = static_cast<impulse_register_type_t>(state.register_types[res.result_register]);
            res.raw_value = state.registers[res.result_register];
        }

        impulse_vm_context_destroy(ctx);
    }

    void OnOK() override {
        Napi::Env env = Env();
        Napi::Value result = NodeQueryResult::CreateInstance(env, res);
        deferred.Resolve(result);
    }

    void OnError(const Napi::Error& e) override {
        deferred.Reject(e.Value());
    }

private:
    NodeSnapshot* snap_obj;
    const uint8_t* bc_bytes;
    size_t inst_count;
    uint64_t input_param;

    impulse_vm_context_t* ctx = nullptr;
    alignas(64) impulse_vm_state_t state;
    impulse_vm_status_t status = IMPULSE_VM_OK;
    impulse::vm::QueryResult res;

    Napi::Promise::Deferred deferred;
};

Napi::Value ExecuteBytecodeAsync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2) {
        Napi::TypeError::New(env, "Snapshot and Bytecode Buffer arguments required").ThrowAsJavaScriptException();
        return env.Null();
    }

    NodeSnapshot* snap_obj = Napi::ObjectWrap<NodeSnapshot>::Unwrap(info[0].As<Napi::Object>());
    if (!snap_obj || !snap_obj->RawSnapshot()) {
        Napi::Error::New(env, "Invalid Snapshot").ThrowAsJavaScriptException();
        return env.Null();
    }

    const uint8_t* bc_bytes = nullptr;
    size_t byte_len = 0;

    if (info[1].IsBuffer()) {
        Napi::Buffer<uint8_t> buf = info[1].As<Napi::Buffer<uint8_t>>();
        bc_bytes = buf.Data();
        byte_len = buf.Length();
    } else if (info[1].IsTypedArray()) {
        Napi::TypedArray arr = info[1].As<Napi::TypedArray>();
        bc_bytes = reinterpret_cast<const uint8_t*>(arr.ArrayBuffer().Data()) + arr.ByteOffset();
        byte_len = arr.ByteLength();
    } else {
        Napi::TypeError::New(env, "Bytecode must be a Buffer or TypedArray").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (byte_len % sizeof(impulse_instruction_t) != 0) {
        Napi::Error::New(env, "Bytecode buffer size must be a multiple of 8 bytes").ThrowAsJavaScriptException();
        return env.Null();
    }

    uint64_t input_param = 0;
    if (info.Length() > 2) {
        input_param = GetUint64(info[2]);
    }

    VmExecuteWorker* worker = new VmExecuteWorker(env, snap_obj, bc_bytes, byte_len, input_param);
    worker->Queue();
    return worker->GetPromise();
}

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
    NodeSnapshot::Init(env, exports);
    NodeWriter::Init(env, exports);
    NodeVmContext::Init(env, exports);
    NodeVmState::Init(env, exports);
    NodeQueryResult::Init(env, exports);
    NodeCompiledQuery::Init(env, exports);
    NodeQueryBuilder::Init(env, exports);

    exports.Set("executeBytecode", Napi::Function::New(env, ExecuteBytecode));
    exports.Set("executeBytecodeAsync", Napi::Function::New(env, ExecuteBytecodeAsync));

    // Export Opcodes
    Napi::Object opcodes = Napi::Object::New(env);
    opcodes.Set("OP_NOP", Napi::Number::New(env, OP_NOP));
    opcodes.Set("OP_INIT_INPUT_NODE", Napi::Number::New(env, OP_INIT_INPUT_NODE));
    opcodes.Set("OP_INIT_INPUT_SET", Napi::Number::New(env, OP_INIT_INPUT_SET));
    opcodes.Set("OP_LOAD_CONST_INT", Napi::Number::New(env, OP_LOAD_CONST_INT));
    opcodes.Set("OP_LOAD_CONST_FLOAT", Napi::Number::New(env, OP_LOAD_CONST_FLOAT));
    opcodes.Set("OP_CSR_WALK", Napi::Number::New(env, OP_CSR_WALK));
    opcodes.Set("OP_CSR_WALK_FILTERED", Napi::Number::New(env, OP_CSR_WALK_FILTERED));
    opcodes.Set("OP_CSR_DEGREE", Napi::Number::New(env, OP_CSR_DEGREE));
    opcodes.Set("OP_SET_UNION", Napi::Number::New(env, OP_SET_UNION));
    opcodes.Set("OP_SET_INTERSECT", Napi::Number::New(env, OP_SET_INTERSECT));
    opcodes.Set("OP_SET_DIFFERENCE", Napi::Number::New(env, OP_SET_DIFFERENCE));
    opcodes.Set("OP_CC_AFFOREST", Napi::Number::New(env, OP_CC_AFFOREST));
    opcodes.Set("OP_MXV", Napi::Number::New(env, OP_MXV));
    opcodes.Set("OP_SAMPLE_NEIGHBORS", Napi::Number::New(env, OP_SAMPLE_NEIGHBORS));
    opcodes.Set("OP_REBAC_CHECK", Napi::Number::New(env, OP_REBAC_CHECK));
    opcodes.Set("OP_COLLECT_BITSET", Napi::Number::New(env, OP_COLLECT_BITSET));
    opcodes.Set("OP_COLLECT_ARRAY", Napi::Number::New(env, OP_COLLECT_ARRAY));
    opcodes.Set("OP_HALT", Napi::Number::New(env, OP_HALT));
    exports.Set("Opcodes", opcodes);

    // Export Register Types
    Napi::Object regTypes = Napi::Object::New(env);
    regTypes.Set("TYPE_NULL", Napi::Number::New(env, TYPE_NULL));
    regTypes.Set("TYPE_INT64", Napi::Number::New(env, TYPE_INT64));
    regTypes.Set("TYPE_NODE_ID", Napi::Number::New(env, TYPE_NODE_ID));
    regTypes.Set("TYPE_RELATION_ID", Napi::Number::New(env, TYPE_RELATION_ID));
    regTypes.Set("TYPE_BITSET_HANDLE", Napi::Number::New(env, TYPE_BITSET_HANDLE));
    regTypes.Set("TYPE_NODE_VECTOR", Napi::Number::New(env, TYPE_NODE_VECTOR));
    regTypes.Set("TYPE_FLOAT_VECTOR", Napi::Number::New(env, TYPE_FLOAT_VECTOR));
    regTypes.Set("TYPE_DOUBLE_VECTOR", Napi::Number::New(env, TYPE_DOUBLE_VECTOR));
    regTypes.Set("TYPE_VALUE_MAP", Napi::Number::New(env, TYPE_VALUE_MAP));
    regTypes.Set("TYPE_STRING_VECTOR", Napi::Number::New(env, TYPE_STRING_VECTOR));
    exports.Set("RegisterType", regTypes);

    // Export VmStatus
    Napi::Object vmStatus = Napi::Object::New(env);
    vmStatus.Set("IMPULSE_VM_OK", Napi::Number::New(env, IMPULSE_VM_OK));
    vmStatus.Set("IMPULSE_VM_ERR_INVALID_OPCODE", Napi::Number::New(env, IMPULSE_VM_ERR_INVALID_OPCODE));
    vmStatus.Set("IMPULSE_VM_ERR_OUT_OF_BOUNDS", Napi::Number::New(env, IMPULSE_VM_ERR_OUT_OF_BOUNDS));
    vmStatus.Set("IMPULSE_VM_ERR_NULL_SNAPSHOT", Napi::Number::New(env, IMPULSE_VM_ERR_NULL_SNAPSHOT));
    exports.Set("VmStatus", vmStatus);

    return exports;
}

NODE_API_MODULE(impulse_node_native, InitAll)
