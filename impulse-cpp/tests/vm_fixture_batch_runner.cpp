/**
 * @file vm_fixture_batch_runner.cpp
 * @brief Persistent stdin/stdout JSONL Fixture Batch Runner for ImpulseVM v0.11.0 (C++20).
 *
 * Implements the exact line protocol of `impulse-ocaml --v11-fixture-batch` and
 * `V11FixtureBatchMain.java`:
 *   Input:  `<profile>\t<max_steps|->\t<fixture_json>\n`
 *   Output: `<result_json>\n`
 */
#include "impulse_graph.h"
#include "vm/vm_internal.hpp"
#include "hwy/targets.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace impulse::vm;

static std::atomic<size_t> g_exec_allocs{0};
static std::atomic<bool>   g_track_exec_allocs{false};

#if defined(__SANITIZE_THREAD__)
#define IMPULSE_RUNNER_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define IMPULSE_RUNNER_TSAN 1
#endif
#endif

#if !defined(IMPULSE_RUNNER_TSAN)
void* operator new(size_t sz) {
    if (g_track_exec_allocs.load(std::memory_order_relaxed)) {
        g_exec_allocs.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(sz == 0 ? 1 : sz);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
#endif

namespace {

// --- Minimal zero-dependency JSON value & parser ---
struct JsonVal {
    enum class Kind { Null, Bool, Int, Double, String, Array, Object };
    Kind kind = Kind::Null;
    bool bval = false;
    int64_t ival = 0;
    double dval = 0.0;
    std::string sval;
    std::vector<JsonVal> arr;
    std::map<std::string, JsonVal> obj;

    const JsonVal* get(const std::string& k) const {
        if (kind != Kind::Object) return nullptr;
        auto it = obj.find(k);
        return (it != obj.end()) ? &it->second : nullptr;
    }
};

class MiniJsonParser {
public:
    explicit MiniJsonParser(const std::string& s) : s_(s), pos_(0) {}

    JsonVal parse() {
        skip_ws();
        if (pos_ >= s_.size()) return {};
        char c = s_[pos_];
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') {
            JsonVal v;
            v.kind = JsonVal::Kind::String;
            v.sval = parse_string();
            return v;
        }
        if (c == 't') { pos_ += 4; JsonVal v; v.kind = JsonVal::Kind::Bool; v.bval = true; return v; }
        if (c == 'f') { pos_ += 5; JsonVal v; v.kind = JsonVal::Kind::Bool; v.bval = false; return v; }
        if (c == 'n') { pos_ += 4; return {}; }
        return parse_number();
    }

private:
    const std::string& s_;
    size_t pos_;

    void skip_ws() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\r' || s_[pos_] == '\n')) {
            ++pos_;
        }
    }

    std::string parse_string() {
        ++pos_; // skip '"'
        std::string out;
        while (pos_ < s_.size() && s_[pos_] != '"') {
            char c = s_[pos_++];
            if (c == '\\' && pos_ < s_.size()) {
                char esc = s_[pos_++];
                switch (esc) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 <= s_.size()) {
                            unsigned cp = static_cast<unsigned>(std::stoul(s_.substr(pos_, 4), nullptr, 16));
                            pos_ += 4;
                            if (cp < 0x80) {
                                out.push_back(static_cast<char>(cp));
                            } else if (cp < 0x800) {
                                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                            } else {
                                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                            }
                        }
                        break;
                    }
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        if (pos_ < s_.size() && s_[pos_] == '"') ++pos_;
        return out;
    }

    JsonVal parse_number() {
        size_t start = pos_;
        bool is_float = false;
        if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c >= '0' && c <= '9') {
                ++pos_;
            } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                is_float = true;
                ++pos_;
            } else {
                break;
            }
        }
        std::string tok = s_.substr(start, pos_ - start);
        JsonVal v;
        if (is_float) {
            v.kind = JsonVal::Kind::Double;
            v.dval = std::strtod(tok.c_str(), nullptr);
        } else {
            v.kind = JsonVal::Kind::Int;
            if (!tok.empty() && tok[0] == '-') {
                v.ival = std::strtoll(tok.c_str(), nullptr, 10);
            } else {
                uint64_t u = std::strtoull(tok.c_str(), nullptr, 10);
                v.ival = static_cast<int64_t>(u);
            }
        }
        return v;
    }

    JsonVal parse_array() {
        ++pos_; // skip '['
        JsonVal v;
        v.kind = JsonVal::Kind::Array;
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return v; }
        while (pos_ < s_.size()) {
            v.arr.push_back(parse());
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; break; }
            break;
        }
        return v;
    }

    JsonVal parse_object() {
        ++pos_; // skip '{'
        JsonVal v;
        v.kind = JsonVal::Kind::Object;
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return v; }
        while (pos_ < s_.size()) {
            skip_ws();
            std::string key = parse_string();
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == ':') ++pos_;
            v.obj[std::move(key)] = parse();
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; break; }
            break;
        }
        return v;
    }
};

uint64_t parse_hex_or_dec_u64(const std::string& raw) {
    size_t i = 0;
    while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
    std::string s = raw.substr(i);
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        return std::strtoull(s.c_str() + 2, nullptr, 16);
    }
    if (!s.empty() && s[0] == '-') {
        return static_cast<uint64_t>(std::strtoll(s.c_str(), nullptr, 10));
    }
    return std::strtoull(s.c_str(), nullptr, 10);
}

int64_t as_i64(const JsonVal* v, int64_t def = 0) {
    if (!v) return def;
    if (v->kind == JsonVal::Kind::Int) return v->ival;
    if (v->kind == JsonVal::Kind::Double) return static_cast<int64_t>(v->dval);
    if (v->kind == JsonVal::Kind::String) return static_cast<int64_t>(parse_hex_or_dec_u64(v->sval));
    if (v->kind == JsonVal::Kind::Bool) return v->bval ? 1 : 0;
    return def;
}

double as_f64(const JsonVal* v, double def = 0.0) {
    if (!v) return def;
    if (v->kind == JsonVal::Kind::Double) return v->dval;
    if (v->kind == JsonVal::Kind::Int) return static_cast<double>(v->ival);
    if (v->kind == JsonVal::Kind::String) {
        if (v->sval == "inf" || v->sval == "+inf" || v->sval == "Inf") return std::numeric_limits<double>::infinity();
        if (v->sval == "-inf" || v->sval == "-Inf") return -std::numeric_limits<double>::infinity();
        if (v->sval == "nan" || v->sval == "NaN") return std::numeric_limits<double>::quiet_NaN();
        return std::strtod(v->sval.c_str(), nullptr);
    }
    return def;
}

std::string as_str(const JsonVal* v) {
    if (!v || v->kind != JsonVal::Kind::String) return "";
    return v->sval;
}

V11Vector* parse_vec_into_arena(V11Arena& arena, const std::string& ty, const JsonVal* vals_v) {
    auto* vec = static_cast<V11Vector*>(arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
    if (!vec) return nullptr;
    new (vec) V11Vector();

    size_t n = (vals_v && vals_v->kind == JsonVal::Kind::Array) ? vals_v->arr.size() : 0;
    vec->length = static_cast<uint32_t>(n);
    size_t words = std::max<size_t>(1, (n + 63) / 64);

    if (ty == "i64" || ty == "i64_vec" || ty == "i32" || ty == "i16" || ty == "i8" ||
        ty == "u64" || ty == "u64_vec" || ty == "u32" || ty == "u16" || ty == "u8") {
        vec->elem_tag = (ty[0] == 'u') ? kVecTagU64 : kVecTagI64;
        vec->i64_data = (n > 0) ? arena.alloc_array<int64_t>(n, 8) : nullptr;
        vec->valid_bits = arena.alloc_array<uint64_t>(words, 8);
        std::memset(vec->valid_bits, 0, words * sizeof(uint64_t));
        for (size_t i = 0; i < n; ++i) {
            const JsonVal& elem = vals_v->arr[i];
            if (elem.kind != JsonVal::Kind::Null) {
                vec->i64_data[i] = as_i64(&elem);
                vec->valid_bits[i >> 6] |= (1ULL << (i & 63));
            }
        }
    } else if (ty == "str" || ty == "str_vec") {
        vec->elem_tag = kVecTagStr;
        vec->str_data = (n > 0) ? arena.alloc_array<V11StrSlice>(n, 8) : nullptr;
        for (size_t i = 0; i < n; ++i) {
            const JsonVal& elem = vals_v->arr[i];
            if (elem.kind == JsonVal::Kind::Null) {
                vec->str_data[i] = {nullptr, 0u, 0u};
            } else {
                std::string s = as_str(&elem);
                char* copy = nullptr;
                if (!s.empty()) {
                    copy = static_cast<char*>(arena.alloc(s.size(), 1));
                    std::memcpy(copy, s.data(), s.size());
                }
                vec->str_data[i] = {copy, static_cast<uint32_t>(s.size()), 1u};
            }
        }
    } else if (ty == "bool" || ty == "bool_3vl" || ty == "bool_3vl_vec") {
        vec->elem_tag = kVecTagBool3vl;
        vec->valid_bits = arena.alloc_array<uint64_t>(words, 8);
        vec->bool_data_bits = arena.alloc_array<uint64_t>(words, 8);
        std::memset(vec->valid_bits, 0, words * sizeof(uint64_t));
        std::memset(vec->bool_data_bits, 0, words * sizeof(uint64_t));
        for (size_t i = 0; i < n; ++i) {
            const JsonVal& elem = vals_v->arr[i];
            if (elem.kind != JsonVal::Kind::Null) {
                uint64_t bit = 1ULL << (i & 63);
                vec->valid_bits[i >> 6] |= bit;
                bool is_true = (elem.kind == JsonVal::Kind::Bool) ? elem.bval : (as_i64(&elem) != 0);
                if (is_true) vec->bool_data_bits[i >> 6] |= bit;
            }
        }
    } else if (ty == "f32_emb" || ty == "emb") {
        vec->elem_tag = kVecTagEmb;
        vec->emb_rows = (n > 0) ? arena.alloc_array<double*>(n, 8) : nullptr;
        uint32_t dim = 0;
        for (size_t i = 0; i < n; ++i) {
            const JsonVal& elem = vals_v->arr[i];
            if (elem.kind == JsonVal::Kind::Array) {
                size_t rl = elem.arr.size();
                double* row = (rl > 0) ? arena.alloc_array<double>(rl, 8) : nullptr;
                for (size_t j = 0; j < rl; ++j) row[j] = as_f64(&elem.arr[j]);
                vec->emb_rows[i] = row;
                if (rl > dim) dim = static_cast<uint32_t>(rl);
            } else {
                vec->emb_rows[i] = nullptr;
            }
        }
        vec->emb_dim = dim;
    } else {
        vec->elem_tag = kVecTagF64;
        vec->f64_data = (n > 0) ? arena.alloc_array<double>(n, 8) : nullptr;
        vec->valid_bits = arena.alloc_array<uint64_t>(words, 8);
        std::memset(vec->valid_bits, 0, words * sizeof(uint64_t));
        for (size_t i = 0; i < n; ++i) {
            const JsonVal& elem = vals_v->arr[i];
            if (elem.kind != JsonVal::Kind::Null) {
                double f = as_f64(&elem);
                if (!std::isnan(f)) {
                    vec->f64_data[i] = f;
                    vec->valid_bits[i >> 6] |= (1ULL << (i & 63));
                }
            }
        }
    }
    return vec;
}

const char* status_name(impulse_vm_status_v11_t st) {
    switch (st) {
        case IMPULSE_VM_V11_OK:                    return "IMPULSE_VM_OK";
        case IMPULSE_VM_V11_ERR_INVALID_OPCODE:    return "IMPULSE_VM_ERR_INVALID_OPCODE";
        case IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS:     return "IMPULSE_VM_ERR_OUT_OF_BOUNDS";
        case IMPULSE_VM_V11_ERR_NULL_SNAPSHOT:     return "IMPULSE_VM_ERR_NULL_SNAPSHOT";
        case IMPULSE_VM_V11_ERR_STACK_OVERFLOW:    return "IMPULSE_VM_ERR_STACK_OVERFLOW";
        case IMPULSE_VM_V11_ERR_STACK_UNDERFLOW:   return "IMPULSE_VM_ERR_STACK_UNDERFLOW";
        case IMPULSE_VM_V11_ERR_INVALID_OPERAND:   return "IMPULSE_VM_ERR_INVALID_OPERAND";
        case IMPULSE_VM_V11_ERR_USER_THROW:        return "IMPULSE_VM_ERR_USER_THROW";
        case IMPULSE_VM_V11_ERR_ASSERTION_FAILED:  return "IMPULSE_VM_ERR_ASSERTION_FAILED";
        case IMPULSE_VM_V11_ERR_TRAP:              return "IMPULSE_VM_ERR_TRAP";
        case IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH:   return "IMPULSE_VM_ERR_DOMAIN_MISMATCH";
        case IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW:   return "IMPULSE_VM_ERR_BUFFER_OVERFLOW";
        case IMPULSE_VM_V11_ERR_FLOATING_POINT:    return "IMPULSE_VM_ERR_FLOATING_POINT";
        case IMPULSE_VM_V11_ERR_GAS_EXHAUSTED:     return "IMPULSE_VM_ERR_GAS_EXHAUSTED";
        case IMPULSE_VM_V11_ERR_TYPE_MISMATCH:     return "IMPULSE_VM_ERR_TYPE_MISMATCH";
        case IMPULSE_VM_V11_ERR_VALIDATION_FAILED: return "IMPULSE_VM_ERR_VALIDATION_FAILED";
        default:                                   return "IMPULSE_VM_ERR_UNKNOWN";
    }
}

void append_json_string(std::ostringstream& oss, const char* ptr, uint32_t len) {
    oss << '"';
    for (uint32_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(ptr[i]);
        switch (c) {
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    oss << buf;
                } else {
                    oss << static_cast<char>(c);
                }
                break;
        }
    }
    oss << '"';
}

std::string hex64(uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(v));
    return std::string(buf);
}

impulse_vm_geometry_v11_t geometry_of_profile(const std::string& prof) {
    if (prof == "P1") return kGeomP1;
    if (prof == "P2") return kGeomP2;
    if (prof == "P3") return kGeomP3;
    if (prof == "P4") return kGeomP4;
    return kGeomP0;
}

std::string run_fixture_json(const std::string& profile,
                             bool has_max_steps_override,
                             uint64_t max_steps_override,
                             const std::string& raw_json,
                             void* scratch_buf,
                             size_t scratch_bytes,
                             const impulse_snapshot_t* preloaded_snap = nullptr) {
    JsonVal root = MiniJsonParser(raw_json).parse();

    std::vector<uint64_t> words;
    if (const JsonVal* wh = root.get("words_hex"); wh && wh->kind == JsonVal::Kind::Array) {
        words.reserve(wh->arr.size());
        for (const auto& item : wh->arr) {
            words.push_back(parse_hex_or_dec_u64(as_str(&item)));
        }
    }

    uint64_t max_steps = has_max_steps_override
        ? max_steps_override
        : (root.get("max_steps") ? static_cast<uint64_t>(as_i64(root.get("max_steps"), 100000)) : 100000ULL);

    size_t eff_scratch = scratch_bytes;
    if (const JsonVal* sb = root.get("scratch_bytes"); sb != nullptr) {
        int64_t req = as_i64(sb, static_cast<int64_t>(scratch_bytes));
        if (req >= 0 && static_cast<size_t>(req) < scratch_bytes) {
            eff_scratch = static_cast<size_t>(req);
        }
    }
    int repeat_runs = root.get("repeat_runs") ? static_cast<int>(as_i64(root.get("repeat_runs"), 1)) : 1;

    uint64_t planned_scratch = 0ULL;
    if (preloaded_snap != nullptr && !words.empty()) {
        std::vector<uint8_t> impb(32u + words.size() * 8u, 0u);
        impulse_impb_header_v11_t hdr{};
        hdr.magic = 0x494D5042u; // 'IMPB'
        hdr.isa_version = 11u;
        hdr.max_call_depth = 16u;
        hdr.code_word_count = static_cast<uint32_t>(words.size());
        hdr.program_xxh3_64 = 0ULL;
        std::memcpy(impb.data(), &hdr, sizeof(hdr));
        std::memcpy(impb.data() + 32u, words.data(), words.size() * 8u);
        impulse_vm_memory_plan_t plan{};
        if (impulse_vm_plan_memory(impb.data(), impb.size(), preloaded_snap, 1u, &plan) == IMPULSE_VM_V11_OK) {
            planned_scratch = plan.guaranteed_scratch_bytes;
        }
    }

    impulse_vm_workspace_v11_t* ws = nullptr;
    impulse_vm_status_v11_t init_st = impulse_workspace_init(scratch_buf, eff_scratch, &ws);
    if (init_st != IMPULSE_VM_V11_OK || !ws) {
        std::ostringstream oss;
        oss << "{\"status\":\"" << status_name(init_st) << "\",\"status_code\":" << init_st
            << ",\"planned_scratch_bytes\":" << planned_scratch
            << ",\"arena_peak_bytes\":0,\"exec_allocs\":0,\"outputs\":{}}";
        return oss.str();
    }
    ws->geometry = geometry_of_profile(profile);

    std::map<uint16_t, uint64_t> dom_tbl;
    std::map<uint16_t, std::pair<uint16_t, uint16_t>> rel_meta;
    std::map<uint16_t, std::pair<std::vector<uint32_t>, std::vector<uint64_t>>> rel_csr;
    std::map<uint16_t, std::pair<std::vector<uint32_t>, std::vector<uint32_t>>> rel_csc;
    std::map<uint16_t, std::array<bool, 4>> rel_flags;
    struct CooFixtureData {
        std::vector<uint64_t> srcs;
        std::vector<uint64_t> tgts;
        std::vector<uint64_t> edge_ids;
        uint8_t order = 0;
    };
    std::map<uint16_t, CooFixtureData> rel_coo;
    std::map<uint16_t, std::vector<std::vector<int>>> rel_dense;
    std::vector<V11DomAttrEntry> node_attrs;
    std::map<uint16_t, std::vector<std::pair<uint32_t, V11Vector*>>> edge_attrs;
    std::vector<V11KeyCatalog> key_catalogs;
    std::vector<V11IndexEntry> indexes;

    if (const JsonVal* ddirs = root.get("data_directives"); ddirs && ddirs->kind == JsonVal::Kind::Array) {
        for (const auto& d : ddirs->arr) {
            std::string kind = as_str(d.get("directive"));
            const JsonVal* args = d.get("args");
            const JsonVal* kv = d.get("kv");
            if (kind == ".domain") {
                uint16_t id = (args && !args->arr.empty()) ? static_cast<uint16_t>(as_i64(&args->arr[0])) : 0u;
                uint64_t cnt = kv ? static_cast<uint64_t>(as_i64(kv->get("count"), 0)) : 0ULL;
                dom_tbl[id] = cnt;
            } else if (kind == ".relation") {
                uint16_t id = (args && !args->arr.empty()) ? static_cast<uint16_t>(as_i64(&args->arr[0])) : 0u;
                uint16_t sd = kv ? static_cast<uint16_t>(as_i64(kv->get("src_dom"), 0)) : 0u;
                uint16_t td = kv ? static_cast<uint16_t>(as_i64(kv->get("tgt_dom"), 0)) : 0u;
                rel_meta[id] = {sd, td};
            } else if (kind == ".csr_inline" && kv) {
                uint16_t rid = static_cast<uint16_t>(as_i64(kv->get("rel"), 0));
                std::vector<uint32_t> offs;
                std::vector<uint64_t> tgts;
                if (const JsonVal* o = kv->get("offsets"); o && o->kind == JsonVal::Kind::Array) {
                    for (const auto& x : o->arr) offs.push_back(static_cast<uint32_t>(as_i64(&x)));
                }
                if (const JsonVal* t = kv->get("targets"); t && t->kind == JsonVal::Kind::Array) {
                    for (const auto& x : t->arr) tgts.push_back(static_cast<uint64_t>(as_i64(&x)));
                }
                rel_csr[rid] = {std::move(offs), std::move(tgts)};
                rel_flags[rid][0] = true;
            } else if (kind == ".csc_inline" && kv) {
                uint16_t rid = static_cast<uint16_t>(as_i64(kv->get("rel"), 0));
                std::vector<uint32_t> offs;
                std::vector<uint32_t> srcs;
                if (const JsonVal* o = kv->get("offsets"); o && o->kind == JsonVal::Kind::Array) {
                    for (const auto& x : o->arr) offs.push_back(static_cast<uint32_t>(as_i64(&x)));
                }
                const JsonVal* s = kv->get("sources") ? kv->get("sources") : kv->get("srcs");
                if (s && s->kind == JsonVal::Kind::Array) {
                    for (const auto& x : s->arr) srcs.push_back(static_cast<uint32_t>(as_i64(&x)));
                }
                if (!offs.empty()) {
                    rel_csc[rid] = {std::move(offs), std::move(srcs)};
                }
                rel_flags[rid][1] = true;
            } else if (kind == ".coo_inline" && kv) {
                uint16_t rid = static_cast<uint16_t>(as_i64(kv->get("rel"), 0));
                CooFixtureData cd{};
                const JsonVal* s = kv->get("sources") ? kv->get("sources") : kv->get("srcs");
                const JsonVal* t = kv->get("targets") ? kv->get("targets") : kv->get("tgts");
                if (s && s->kind == JsonVal::Kind::Array) {
                    for (const auto& x : s->arr) cd.srcs.push_back(static_cast<uint64_t>(as_i64(&x)));
                }
                if (t && t->kind == JsonVal::Kind::Array) {
                    for (const auto& x : t->arr) cd.tgts.push_back(static_cast<uint64_t>(as_i64(&x)));
                }
                if (const JsonVal* e = kv->get("edge_ids"); e && e->kind == JsonVal::Kind::Array) {
                    for (const auto& x : e->arr) cd.edge_ids.push_back(static_cast<uint64_t>(as_i64(&x)));
                }
                if (const JsonVal* ord = kv->get("order"); ord && ord->kind == JsonVal::Kind::String) {
                    if (ord->sval == "src") cd.order = 1;
                    else if (ord->sval == "src_tgt") cd.order = 2;
                    else if (ord->sval == "tgt") cd.order = 3;
                    else if (ord->sval == "tgt_src") cd.order = 4;
                }
                rel_coo[rid] = std::move(cd);
                rel_flags[rid][2] = true;
            } else if (kind == ".dense_inline" && kv) {
                uint16_t rid = static_cast<uint16_t>(as_i64(kv->get("rel"), 0));
                std::vector<std::vector<int>> rows;
                if (const JsonVal* rl = kv->get("rows"); rl && rl->kind == JsonVal::Kind::Array) {
                    for (const auto& r : rl->arr) {
                        std::vector<int> row;
                        if (r.kind == JsonVal::Kind::Array) {
                            for (const auto& x : r.arr) row.push_back(static_cast<int>(as_i64(&x)));
                        }
                        rows.push_back(std::move(row));
                    }
                }
                rel_dense[rid] = std::move(rows);
                rel_flags[rid][3] = true;
            } else if (kind == ".key_catalog" && kv) {
                uint16_t dom = static_cast<uint16_t>(as_i64(kv->get("dom"), 0));
                V11KeyCatalog kc{};
                kc.dom_id = dom;
                if (const JsonVal* kl = kv->get("keys"); kl && kl->kind == JsonVal::Kind::Array) {
                    kc.count = static_cast<uint32_t>(kl->arr.size());
                    if (kc.count > 0u) {
                        auto* arr = ws->arena.alloc_array<V11StrSlice>(kc.count, 8);
                        for (uint32_t i = 0; i < kc.count; ++i) {
                            std::string s = as_str(&kl->arr[i]);
                            char* copy = nullptr;
                            if (!s.empty()) {
                                copy = static_cast<char*>(ws->arena.alloc(s.size(), 1));
                                std::memcpy(copy, s.data(), s.size());
                            }
                            arr[i] = {copy, static_cast<uint32_t>(s.size()), 1u};
                        }
                        kc.keys = arr;
                    }
                }
                key_catalogs.push_back(kc);
            } else if (kind == ".index" && kv) {
                uint16_t dom = static_cast<uint16_t>(as_i64(kv->get("dom"), 0));
                uint16_t attr = static_cast<uint16_t>(as_i64(kv->get("attr"), 0));
                std::string ik = kv->get("kind") ? as_str(kv->get("kind")) : "sorted";
                char* copy = nullptr;
                if (!ik.empty()) {
                    copy = static_cast<char*>(ws->arena.alloc(ik.size(), 1));
                    std::memcpy(copy, ik.data(), ik.size());
                }
                indexes.push_back({dom, attr, {copy, static_cast<uint32_t>(ik.size()), 1u}});
            } else if (kind == ".node_attr" && kv) {
                uint16_t dom = static_cast<uint16_t>(as_i64(kv->get("dom"), 0));
                uint16_t aid = static_cast<uint16_t>(as_i64(kv->get("attr"), 0));
                std::string ty = kv->get("type") ? as_str(kv->get("type")) : "f64";
                V11Vector* v = parse_vec_into_arena(ws->arena, ty, kv->get("values"));
                node_attrs.push_back({dom, aid, v});
            } else if (kind == ".edge_attr" && kv) {
                uint16_t rid = static_cast<uint16_t>(as_i64(kv->get("rel"), 0));
                uint32_t aid = static_cast<uint32_t>(as_i64(kv->get("attr"), 0));
                std::string ty = kv->get("type") ? as_str(kv->get("type")) : "f64";
                V11Vector* v = parse_vec_into_arena(ws->arena, ty, kv->get("values"));
                edge_attrs[rid].push_back({aid, v});
            } else if (kind == ".tier_thresholds" && kv) {
                if (kv->get("tier0_max"))  ws->geometry.tier0_max  = static_cast<uint32_t>(as_i64(kv->get("tier0_max")));
                if (kv->get("page_shift")) ws->geometry.page_shift = static_cast<uint32_t>(as_i64(kv->get("page_shift")));
                if (kv->get("morsel_max")) ws->geometry.morsel_max = static_cast<uint32_t>(as_i64(kv->get("morsel_max")));
            } else if (kind == ".param" && kv && ws->param_count < kMaxParams) {
                uint32_t pid = (args && !args->arr.empty()) ? static_cast<uint32_t>(as_i64(&args->arr[0])) : 0u;
                std::string ty = kv->get("type") ? as_str(kv->get("type")) : "i64";
                V11ParamEntry pe{};
                pe.id = pid;
                if (ty == "frontier") {
                    pe.kind = ParamKind::Frontier;
                    pe.dom_id = static_cast<uint16_t>(as_i64(kv->get("dom"), 0));
                    if (const JsonVal* vl = kv->get("values"); vl && vl->kind == JsonVal::Kind::Array) {
                        pe.f_len = static_cast<uint32_t>(vl->arr.size());
                        if (pe.f_len > 0) {
                            pe.f_ids = ws->arena.alloc_array<uint64_t>(pe.f_len, 8);
                            for (uint32_t i = 0; i < pe.f_len; ++i) pe.f_ids[i] = static_cast<uint64_t>(as_i64(&vl->arr[i]));
                        }
                    }
                } else if (ty == "multilane") {
                    pe.kind = ParamKind::MultiLane;
                    pe.dom_id = static_cast<uint16_t>(as_i64(kv->get("dom"), 0));
                    pe.ml_width = static_cast<uint8_t>(as_i64(kv->get("width"), 64));
                    pe.ml_active = static_cast<uint8_t>(as_i64(kv->get("active_lanes"), 0));
                    if (const JsonVal* el = kv->get("entries"); el && el->kind == JsonVal::Kind::Array) {
                        std::vector<uint64_t> ns, ms;
                        for (const auto& item : el->arr) {
                            std::string s = as_str(&item);
                            auto colon = s.find(':');
                            if (colon != std::string::npos && colon > 0) {
                                ns.push_back(parse_hex_or_dec_u64(s.substr(0, colon)));
                                ms.push_back(parse_hex_or_dec_u64(s.substr(colon + 1)));
                            }
                        }
                        pe.ml_len = static_cast<uint32_t>(ns.size());
                        if (pe.ml_len > 0) {
                            pe.ml_nodes = ws->arena.alloc_array<uint64_t>(pe.ml_len, 8);
                            pe.ml_masks = ws->arena.alloc_array<uint64_t>(pe.ml_len, 8);
                            std::memcpy(pe.ml_nodes, ns.data(), pe.ml_len * sizeof(uint64_t));
                            std::memcpy(pe.ml_masks, ms.data(), pe.ml_len * sizeof(uint64_t));
                        }
                    }
                } else if (ty == "str" || ty == "str_handle") {
                    pe.kind = ParamKind::Str;
                    const JsonVal* v = kv->get("value");
                    if (!v || v->kind == JsonVal::Kind::Null) {
                        pe.str = {nullptr, 0u, 0u};
                    } else {
                        std::string s = as_str(v);
                        char* copy = nullptr;
                        if (!s.empty()) {
                            copy = static_cast<char*>(ws->arena.alloc(s.size(), 1));
                            std::memcpy(copy, s.data(), s.size());
                        }
                        pe.str = {copy, static_cast<uint32_t>(s.size()), 1u};
                    }
                } else if (ty == "i64" || ty == "u64") {
                    pe.kind = ParamKind::Scalar;
                    pe.scalar_type = (ty == "u64") ? static_cast<uint8_t>(ScalarType::SCALAR_U64)
                                                   : static_cast<uint8_t>(ScalarType::SCALAR_I64);
                    const JsonVal* v = kv->get("value");
                    if (!v || v->kind == JsonVal::Kind::Null) {
                        pe.scalar_valid = false;
                        pe.scalar_bits = 0ULL;
                    } else {
                        pe.scalar_valid = true;
                        pe.scalar_bits = static_cast<uint64_t>(as_i64(v));
                    }
                } else if (ty == "f64") {
                    pe.kind = ParamKind::Scalar;
                    pe.scalar_type = static_cast<uint8_t>(ScalarType::SCALAR_F64);
                    const JsonVal* v = kv->get("value");
                    if (!v || v->kind == JsonVal::Kind::Null) {
                        pe.scalar_valid = false;
                        pe.scalar_bits = 0ULL;
                    } else {
                        double f = as_f64(v);
                        if (std::isnan(f)) {
                            pe.scalar_valid = false;
                            pe.scalar_bits = 0ULL;
                        } else {
                            pe.scalar_valid = true;
                            std::memcpy(&pe.scalar_bits, &f, 8);
                        }
                    }
                } else if (ty == "bool" || ty == "bool_3vl") {
                    pe.kind = ParamKind::Scalar;
                    pe.scalar_type = static_cast<uint8_t>(ScalarType::SCALAR_BOOL_3VL);
                    const JsonVal* v = kv->get("value");
                    if (!v || v->kind == JsonVal::Kind::Null) {
                        pe.scalar_valid = false;
                        pe.scalar_bits = 0ULL;
                    } else {
                        pe.scalar_valid = true;
                        bool b = (v->kind == JsonVal::Kind::Bool) ? v->bval : (as_i64(v) != 0);
                        pe.scalar_bits = b ? 1ULL : 0ULL;
                    }
                } else if (ty == "null") {
                    pe.kind = ParamKind::Scalar;
                    pe.scalar_type = static_cast<uint8_t>(ScalarType::SCALAR_NULL_UNSET);
                    pe.scalar_valid = false;
                    pe.scalar_bits = 0ULL;
                } else if (ty == "i64_vec" || ty == "u64_vec" || ty == "f64_vec" ||
                           ty == "f32" || ty == "f32_vec" || ty == "str_vec" || ty == "bool_3vl_vec") {
                    pe.kind = ParamKind::Vec;
                    pe.vec = parse_vec_into_arena(ws->arena, ty, kv->get("values"));
                }
                if (pe.kind != ParamKind::Empty) {
                    // Replace existing param with same id if present
                    bool replaced = false;
                    for (uint32_t i = 0; i < ws->param_count; ++i) {
                        if (ws->params[i].id == pid) { ws->params[i] = pe; replaced = true; break; }
                    }
                    if (!replaced) ws->params[ws->param_count++] = pe;
                }
            }
        }
    }

    if (const JsonVal* cpool = root.get("const_pool"); cpool && cpool->kind == JsonVal::Kind::Array) {
        for (const auto& p : cpool->arr) {
            if (ws->const_pool_count >= kMaxConstPool) break;
            const JsonVal* args = p.get("args");
            const JsonVal* kv = p.get("kv");
            uint32_t pid = (args && !args->arr.empty()) ? static_cast<uint32_t>(as_i64(&args->arr[0])) : 0u;
            std::string ty = (kv && kv->get("type")) ? as_str(kv->get("type")) : "i64_vec";
            V11ConstPoolEntry ce{};
            ce.id = pid;
            if (ty == "str") {
                ce.kind = ConstPoolKind::Str;
                std::string s = kv ? as_str(kv->get("value")) : "";
                char* copy = nullptr;
                if (!s.empty()) {
                    copy = static_cast<char*>(ws->arena.alloc(s.size(), 1));
                    std::memcpy(copy, s.data(), s.size());
                }
                ce.str = {copy, static_cast<uint32_t>(s.size()), 1u};
            } else if (ty == "frontier") {
                ce.kind = ConstPoolKind::Frontier;
                if (kv) {
                    if (const JsonVal* vl = kv->get("values"); vl && vl->kind == JsonVal::Kind::Array) {
                        ce.f_len = static_cast<uint32_t>(vl->arr.size());
                        if (ce.f_len > 0) {
                            ce.f_ids = ws->arena.alloc_array<uint64_t>(ce.f_len, 8);
                            for (uint32_t i = 0; i < ce.f_len; ++i) {
                                ce.f_ids[i] = static_cast<uint64_t>(as_i64(&vl->arr[i]));
                            }
                        }
                    }
                }
            } else {
                ce.kind = ConstPoolKind::Vec;
                ce.vec = parse_vec_into_arena(ws->arena, ty, kv ? kv->get("values") : nullptr);
            }
            bool replaced = false;
            for (uint32_t i = 0; i < ws->const_pool_count; ++i) {
                if (ws->const_pool[i].id == pid) { ws->const_pool[i] = ce; replaced = true; break; }
            }
            if (!replaced) ws->const_pool[ws->const_pool_count++] = ce;
        }
    }

    // Build V11SnapshotView if any domains/relations were declared
    V11SnapshotView snap_view{};
    const V11SnapshotView* snap_ptr = nullptr;
    std::vector<uint64_t> dom_cards;
    std::vector<V11RelView> rel_views;
    std::vector<std::vector<uint32_t>> owned_row_offs;
    std::vector<std::vector<uint64_t>> owned_col_tgts;
    std::vector<std::vector<uint32_t>> owned_csc_offs;
    std::vector<std::vector<uint32_t>> owned_csc_srcs;
    std::vector<std::vector<uint32_t>> owned_ea_ids;
    std::vector<std::vector<V11Vector*>> owned_ea_vecs;
    if (preloaded_snap != nullptr) {
        snap_ptr = v11_get_cached_snapshot_view(preloaded_snap);
    } else if (!dom_tbl.empty() || !rel_meta.empty()) {
        int max_dom = -1;
        for (const auto& kv : dom_tbl) if (kv.first > max_dom) max_dom = kv.first;
        dom_cards.assign(static_cast<size_t>(max_dom + 1), 0ULL);
        for (const auto& kv : dom_tbl) dom_cards[kv.first] = kv.second;

        int max_rel = -1;
        for (const auto& kv : rel_meta) if (kv.first > max_rel) max_rel = kv.first;
        size_t n_rel = (max_rel >= 0) ? static_cast<size_t>(max_rel + 1) : 0u;
        rel_views.resize(n_rel);
        owned_row_offs.resize(n_rel);
        owned_col_tgts.resize(n_rel);
        owned_csc_offs.resize(n_rel);
        owned_csc_srcs.resize(n_rel);
        owned_ea_ids.resize(n_rel);
        owned_ea_vecs.resize(n_rel);

        for (size_t rid = 0; rid < n_rel; ++rid) {
            uint16_t r16 = static_cast<uint16_t>(rid);
            auto meta_it = rel_meta.find(r16);
            uint16_t sd = (meta_it != rel_meta.end()) ? meta_it->second.first : 0u;
            uint16_t td = (meta_it != rel_meta.end()) ? meta_it->second.second : 0u;
            uint32_t n_src = (sd < dom_cards.size()) ? static_cast<uint32_t>(dom_cards[sd]) : 0u;
            auto fl_it = rel_flags.find(r16);
            std::array<bool, 4> fl = (fl_it != rel_flags.end()) ? fl_it->second : std::array<bool, 4>{false, false, false, false};

            if (auto it = rel_csr.find(r16); it != rel_csr.end()) {
                owned_row_offs[rid] = it->second.first;
                owned_col_tgts[rid] = it->second.second;
            } else if (auto it = rel_coo.find(r16); it != rel_coo.end()) {
                const auto& srcs = it->second.srcs;
                const auto& tgts = it->second.tgts;
                std::vector<std::vector<uint64_t>> buckets(n_src);
                size_t lim = std::min(srcs.size(), tgts.size());
                for (size_t i = 0; i < lim; ++i) {
                    if (srcs[i] < n_src) buckets[static_cast<size_t>(srcs[i])].push_back(tgts[i]);
                }
                owned_row_offs[rid].assign(n_src + 1u, 0u);
                for (uint32_t u = 0; u < n_src; ++u) {
                    owned_row_offs[rid][u + 1u] = owned_row_offs[rid][u] + static_cast<uint32_t>(buckets[u].size());
                    owned_col_tgts[rid].insert(owned_col_tgts[rid].end(), buckets[u].begin(), buckets[u].end());
                }
            } else if (auto it = rel_dense.find(r16); it != rel_dense.end()) {
                const auto& rows = it->second;
                owned_row_offs[rid].assign(n_src + 1u, 0u);
                for (uint32_t u = 0; u < n_src; ++u) {
                    uint32_t cnt = 0u;
                    if (u < rows.size()) {
                        for (size_t v = 0; v < rows[u].size(); ++v) {
                            if (rows[u][v] != 0) {
                                owned_col_tgts[rid].push_back(static_cast<uint64_t>(v));
                                ++cnt;
                            }
                        }
                    }
                    owned_row_offs[rid][u + 1u] = owned_row_offs[rid][u] + cnt;
                }
            } else {
                owned_row_offs[rid].assign(n_src + 1u, 0u);
            }

            if (auto ea_it = edge_attrs.find(r16); ea_it != edge_attrs.end()) {
                for (const auto& pair : ea_it->second) {
                    owned_ea_ids[rid].push_back(pair.first);
                    owned_ea_vecs[rid].push_back(pair.second);
                }
            }

            V11RelView rv{};
            rv.rel_id = r16;
            rv.src_dom = sd;
            rv.tgt_dom = td;
            rv.row_offsets = owned_row_offs[rid].data();
            rv.row_off_len = static_cast<uint32_t>(owned_row_offs[rid].size());
            rv.col_targets = owned_col_tgts[rid].data();
            rv.col_tgt_len = static_cast<uint32_t>(owned_col_tgts[rid].size());
            if (auto cit = rel_coo.find(r16); cit != rel_coo.end()) {
                rv.coo_srcs = cit->second.srcs.data();
                rv.coo_tgts = cit->second.tgts.data();
                rv.coo_edge_ids = cit->second.edge_ids.empty() ? nullptr : cit->second.edge_ids.data();
                rv.coo_edge_count = static_cast<uint32_t>(std::min(cit->second.srcs.size(), cit->second.tgts.size()));
                rv.coo_order = cit->second.order;
            }
            if (fl[1]) {
                uint32_t n_tgt = (td < dom_cards.size()) ? static_cast<uint32_t>(dom_cards[td]) : 0u;
                if (auto cit = rel_csc.find(r16); cit != rel_csc.end()) {
                    owned_csc_offs[rid] = cit->second.first;
                    owned_csc_srcs[rid] = cit->second.second;
                } else {
                    owned_csc_offs[rid].assign(static_cast<size_t>(n_tgt) + 1u, 0u);
                    uint32_t csr_rows = owned_row_offs[rid].empty() ? 0u : static_cast<uint32_t>(owned_row_offs[rid].size() - 1u);
                    uint32_t total = 0u;
                    for (uint32_t u = 0; u < csr_rows; ++u) {
                        for (uint32_t e = owned_row_offs[rid][u]; e < owned_row_offs[rid][u + 1u]; ++e) {
                            uint64_t v = owned_col_tgts[rid][e];
                            if (v < n_tgt) {
                                owned_csc_offs[rid][static_cast<size_t>(v) + 1u]++;
                                ++total;
                            }
                        }
                    }
                    for (uint32_t v = 0; v < n_tgt; ++v) owned_csc_offs[rid][v + 1u] += owned_csc_offs[rid][v];
                    owned_csc_srcs[rid].assign(total, 0u);
                    std::vector<uint32_t> cur(owned_csc_offs[rid].begin(), owned_csc_offs[rid].begin() + n_tgt);
                    for (uint32_t u = 0; u < csr_rows; ++u) {
                        for (uint32_t e = owned_row_offs[rid][u]; e < owned_row_offs[rid][u + 1u]; ++e) {
                            uint64_t v = owned_col_tgts[rid][e];
                            if (v < n_tgt) {
                                owned_csc_srcs[rid][cur[static_cast<size_t>(v)]++] = u;
                            }
                        }
                    }
                }
                rv.csc_row_off_raw = owned_csc_offs[rid].empty() ? nullptr : reinterpret_cast<const uint8_t*>(owned_csc_offs[rid].data());
                rv.csc_col_idx_raw = owned_csc_srcs[rid].empty() ? nullptr : reinterpret_cast<const uint8_t*>(owned_csc_srcs[rid].data());
                rv.csc_row_off_len = static_cast<uint32_t>(owned_csc_offs[rid].size());
            }
            rv.has_csr = fl[0];
            rv.has_csc = fl[1];
            rv.has_coo = fl[2];
            rv.has_dense = fl[3];
            rv.edge_attr_ids = owned_ea_ids[rid].data();
            rv.edge_attrs = owned_ea_vecs[rid].data();
            rv.edge_attr_count = static_cast<uint32_t>(owned_ea_ids[rid].size());
            rel_views[rid] = rv;
        }

        snap_view.domain_cards = dom_cards.data();
        snap_view.domain_count = static_cast<uint32_t>(dom_cards.size());
        snap_view.relations = rel_views.data();
        snap_view.relation_count = static_cast<uint32_t>(rel_views.size());
        snap_view.node_attrs = node_attrs.data();
        snap_view.node_attr_count = static_cast<uint32_t>(node_attrs.size());
        snap_view.key_catalogs = key_catalogs.data();
        snap_view.key_catalog_count = static_cast<uint32_t>(key_catalogs.size());
        snap_view.indexes = indexes.data();
        snap_view.index_count = static_cast<uint32_t>(indexes.size());
        snap_ptr = &snap_view;
    }

    // Set arena watermark after fixture bindings so execution preserves bindings
    ws->arena.mark = ws->arena.pos;

    g_exec_allocs.store(0, std::memory_order_relaxed);
    g_track_exec_allocs.store(true, std::memory_order_relaxed);
    execute_words_internal(words.data(), words.size(), snap_ptr, *ws, max_steps);
    g_track_exec_allocs.store(false, std::memory_order_relaxed);
    size_t exec1_allocs = g_exec_allocs.load(std::memory_order_relaxed);
    size_t arena_peak = ws->arena.pos;
    impulse_vm_ictx_v2_t digest = impulse_vm_v11_compute_ictx_v2(ws);

    bool repeat_hash_match = true;
    size_t exec2_allocs = 0;
    if (repeat_runs >= 2) {
        ws->arena.reset_to_mark();
        ws->reset_execution_state();
        g_exec_allocs.store(0, std::memory_order_relaxed);
        g_track_exec_allocs.store(true, std::memory_order_relaxed);
        execute_words_internal(words.data(), words.size(), snap_ptr, *ws, max_steps);
        g_track_exec_allocs.store(false, std::memory_order_relaxed);
        exec2_allocs = g_exec_allocs.load(std::memory_order_relaxed);
        impulse_vm_ictx_v2_t digest2 = impulse_vm_v11_compute_ictx_v2(ws);
        repeat_hash_match = (digest.logical_hash == digest2.logical_hash &&
                             digest.physical_hash == digest2.physical_hash);
    }

    // Format JSON response matching V11FixtureBatchMain.java / impulse-ocaml
    std::ostringstream oss;
    oss << '{';
    oss << "\"status\":\"" << status_name(ws->status) << "\",";
    oss << "\"status_code\":" << ws->status << ',';
    oss << "\"planned_scratch_bytes\":" << planned_scratch << ',';
    oss << "\"arena_peak_bytes\":" << arena_peak << ',';
    oss << "\"exec_allocs\":" << exec1_allocs << ',';
    if (repeat_runs >= 2) {
        oss << "\"repeat_hash_match\":" << (repeat_hash_match ? "true" : "false") << ',';
        oss << "\"exec2_allocs\":" << exec2_allocs << ',';
    }
    oss << "\"user_throw_code\":" << static_cast<uint64_t>(ws->user_throw_code) << ',';
    oss << "\"pc\":" << ws->state.pc << ',';
    oss << "\"max_dop\":" << ws->state.max_dop << ',';
    oss << "\"flags\":{\"zf\":" << (ws->flag_zf() ? "true" : "false")
        << ",\"st\":" << (ws->flag_st() ? "true" : "false")
        << ",\"nf\":" << (ws->flag_nf() ? "true" : "false")
        << ",\"raw\":" << (ws->state.vm_flags & 7u) << "},";
    oss << "\"logical_hash\":\"" << hex64(digest.logical_hash) << "\",";
    oss << "\"physical_hash\":\"" << hex64(digest.physical_hash) << "\",";
    oss << "\"r_valid_mask\":" << static_cast<unsigned>(ws->state.r_valid_mask) << ',';
    oss << "\"r_type_word\":\"" << hex64(ws->canonical_r_type_word()) << "\",";

    oss << "\"r_regs\":{";
    for (uint8_t i = 0; i < 16; ++i) {
        if (i > 0) oss << ',';
        oss << "\"R" << static_cast<unsigned>(i) << "\":";
        if (!ws->is_r_valid(i)) {
            oss << "null";
        } else if (ws->read_r_type(i) == static_cast<uint8_t>(ScalarType::SCALAR_STR_HANDLE)) {
            V11StrSlice s = ws->resolve_scalar_str(ws->read_r_raw(i));
            append_json_string(oss, s.valid ? s.ptr : "", s.valid ? s.len : 0u);
        } else {
            oss << static_cast<int64_t>(ws->read_r_raw(i));
        }
    }
    oss << "},\"r_types\":{";
    for (uint8_t i = 0; i < 16; ++i) {
        if (i > 0) oss << ',';
        oss << "\"R" << static_cast<unsigned>(i) << "\":"
            << static_cast<unsigned>(ws->is_r_valid(i) ? ws->read_r_type(i) : 0u);
    }
    oss << "},\"f_regs\":{";
    for (uint8_t i = 0; i < 16; ++i) {
        if (i > 0) oss << ',';
        oss << "\"F" << static_cast<unsigned>(i) << "\":";
        if (!ws->is_f_init(i)) {
            oss << "null";
        } else {
            const V11Frontier& f = ws->f_regs[i];
            oss << "{\"domain_id\":" << f.domain_id
                << ",\"tier\":" << static_cast<unsigned>(f.tier)
                << ",\"nodes\":[";
            if (!f.multi_lane) {
                if (f.tier == kTierAllSpan) {
                    for (uint64_t u = 0; u < f.domain_card; ++u) {
                        if (u > 0) oss << ',';
                        oss << u;
                    }
                } else {
                    for (uint32_t k = 0; k < f.node_count; ++k) {
                        if (k > 0) oss << ',';
                        oss << static_cast<uint64_t>(f.nodes[k]);
                    }
                }
            } else {
                for (uint32_t k = 0; k < f.ml_count; ++k) {
                    if (k > 0) oss << ',';
                    char buf[48];
                    std::snprintf(buf, sizeof(buf), "\"%llu:0x%llx\"",
                                  static_cast<unsigned long long>(f.ml_nodes[k]),
                                  static_cast<unsigned long long>(f.ml_masks[k]));
                    oss << buf;
                }
            }
            oss << "]}";
        }
    }
    oss << "},\"outputs\":{";
    std::vector<uint32_t> out_order(ws->output_count);
    for (uint32_t i = 0; i < ws->output_count; ++i) out_order[i] = i;
    std::sort(out_order.begin(), out_order.end(), [&](uint32_t a, uint32_t b) {
        return ws->outputs[a].slot < ws->outputs[b].slot;
    });
    for (size_t idx = 0; idx < out_order.size(); ++idx) {
        if (idx > 0) oss << ',';
        const V11OutputSlot& os = ws->outputs[out_order[idx]];
        oss << '"' << os.slot << "\":[";
        if (os.vec) {
            const V11Vector& v = *os.vec;
            for (uint32_t i = 0; i < v.length; ++i) {
                if (i > 0) oss << ',';
                switch (v.elem_tag) {
                    case kVecTagI64:
                    case kVecTagU64:
                        if (!v.is_valid_at(i)) oss << "null";
                        else oss << v.i64_data[i];
                        break;
                    case kVecTagF64:
                        if (!v.is_valid_at(i)) oss << "null";
                        else {
                            double f = v.f64_data[i];
                            if (std::isnan(f) || std::isinf(f)) oss << "null";
                            else {
                                char fbuf[64];
                                std::snprintf(fbuf, sizeof(fbuf), "%.17g", f);
                                oss << fbuf;
                            }
                        }
                        break;
                    case kVecTagStr:
                        if (!v.is_valid_at(i)) oss << "null";
                        else append_json_string(oss, v.str_data[i].ptr ? v.str_data[i].ptr : "", v.str_data[i].len);
                        break;
                    case kVecTagBool3vl:
                        if (!v.is_valid_at(i)) oss << "null";
                        else oss << (v.bool_data_at(i) ? "true" : "false");
                        break;
                    default:
                        oss << "null";
                        break;
                }
            }
        }
        oss << ']';
    }
    oss << "}}";
    return oss.str();
}

} // namespace

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::string snapshot_path;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--snapshot" && i + 1 < argc) {
            snapshot_path = argv[++i];
        } else if (arg.rfind("--snapshot=", 0) == 0) {
            snapshot_path = arg.substr(11);
        } else if (arg == "--scalar-simd") {
            hwy::DisableTargets(~int64_t(HWY_SCALAR));
        }
    }

    impulse_snapshot_t* opened_snap = nullptr;
    if (!snapshot_path.empty()) {
        impulse_status_t st = IMPULSE_OK;
        opened_snap = impulse_snapshot_open(snapshot_path.c_str(), &st);
        if (!opened_snap || st != IMPULSE_OK) {
            std::cerr << "Failed to open snapshot: " << snapshot_path << " (status=" << st << ")\n";
            return 2;
        }
        (void)impulse_vm_v11_prepare_snapshot(opened_snap);
    }

    constexpr size_t kScratchSize = 128u * 1024u * 1024u; // 128 MB 128B-aligned caller buffer
    void* scratch_buf = nullptr;
#if defined(_WIN32)
    scratch_buf = _aligned_malloc(kScratchSize, 128);
#else
    if (posix_memalign(&scratch_buf, 128, kScratchSize) != 0) {
        if (opened_snap) impulse_snapshot_close(opened_snap);
        return 1;
    }
#endif

    std::string line;
    while (std::getline(std::cin, line)) {
        bool blank = true;
        for (char c : line) {
            if (!std::isspace(static_cast<unsigned char>(c))) { blank = false; break; }
        }
        if (blank) continue;

        size_t t1 = line.find('\t');
        size_t t2 = (t1 != std::string::npos) ? line.find('\t', t1 + 1) : std::string::npos;
        std::string profile = (t1 != std::string::npos) ? line.substr(0, t1) : "P0";
        std::string ms_str = (t1 != std::string::npos && t2 != std::string::npos)
            ? line.substr(t1 + 1, t2 - t1 - 1) : "-";
        std::string raw_json = (t2 != std::string::npos) ? line.substr(t2 + 1) : line;

        bool has_ms = (!ms_str.empty() && ms_str != "-");
        uint64_t ms_val = has_ms ? std::strtoull(ms_str.c_str(), nullptr, 10) : 0ULL;

        std::string resp = run_fixture_json(profile, has_ms, ms_val, raw_json, scratch_buf, kScratchSize, opened_snap);
        std::cout << resp << '\n' << std::flush;
    }

    if (opened_snap) {
        impulse_snapshot_close(opened_snap);
    }
#if defined(_WIN32)
    _aligned_free(scratch_buf);
#else
    std::free(scratch_buf);
#endif
    return 0;
}
