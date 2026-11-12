/**
 * @file kleisli_json_runner.cpp
 * @brief Phase B Canonical Batch JSONL Query Runner for Impulse C++ Spec v0.10.0 Engine.
 *
 * Usage:
 *   kleisli_json_runner <snapshot.imps> <queries.jsonl>
 *
 * Opens <snapshot.imps> once via zero-copy mmap, reads <queries.jsonl> line-by-line,
 * executes each Kleisli pipeline strictly through native C++ SnapshotView / DomainView /
 * Traversal / MultiLaneTraversal APIs, and writes one single-line JSON object per query
 * to stdout.
 */

#include "impulse_graph.h"
#include "impulse_vm_fluent.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Minimal RFC 8259 Zero-Dependency JSON Value & Parser
// ---------------------------------------------------------------------------

struct JsonVal {
    enum class Kind { Null, Bool, Int, Uint, Float, String, Array, Object };

    Kind kind = Kind::Null;
    bool b_val = false;
    int64_t i_val = 0;
    uint64_t u_val = 0;
    double f_val = 0.0;
    std::string s_val;
    std::vector<JsonVal> arr_val;
    std::map<std::string, JsonVal> obj_val;

    bool is_null() const { return kind == Kind::Null; }
    bool is_object() const { return kind == Kind::Object; }
    bool is_array() const { return kind == Kind::Array; }

    bool has(const std::string& key) const {
        return kind == Kind::Object && obj_val.find(key) != obj_val.end();
    }

    const JsonVal& at_or_null(const std::string& key) const {
        static const JsonVal kNull{};
        if (kind != Kind::Object) return kNull;
        auto it = obj_val.find(key);
        return (it != obj_val.end()) ? it->second : kNull;
    }

    std::string get_str(const std::string& key, const std::string& def = "") const {
        const auto& v = at_or_null(key);
        return (v.kind == Kind::String) ? v.s_val : def;
    }

    int64_t as_i64(int64_t def = 0) const {
        if (kind == Kind::Int) return i_val;
        if (kind == Kind::Uint) return static_cast<int64_t>(u_val);
        if (kind == Kind::Float) return static_cast<int64_t>(f_val);
        if (kind == Kind::String && !s_val.empty()) {
            return std::strtoll(s_val.c_str(), nullptr, 10);
        }
        return def;
    }

    uint64_t as_u64(uint64_t def = 0) const {
        if (kind == Kind::Uint) return u_val;
        if (kind == Kind::Int) return static_cast<uint64_t>(i_val);
        if (kind == Kind::Float) return static_cast<uint64_t>(f_val);
        if (kind == Kind::String && !s_val.empty()) {
            return std::strtoull(s_val.c_str(), nullptr, 10);
        }
        return def;
    }

    double as_f64(double def = 0.0) const {
        if (kind == Kind::Float) return f_val;
        if (kind == Kind::Int) return static_cast<double>(i_val);
        if (kind == Kind::Uint) return static_cast<double>(u_val);
        return def;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view src) : src_(src), pos_(0) {}

    JsonVal parse() {
        skip_ws();
        JsonVal v = parse_value(0);
        skip_ws();
        if (pos_ != src_.size()) {
            throw std::runtime_error("Trailing characters in JSON line");
        }
        return v;
    }

private:
    std::string_view src_;
    size_t pos_;

    void skip_ws() {
        while (pos_ < src_.size()) {
            unsigned char c = static_cast<unsigned char>(src_[pos_]);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    char peek() const {
        return (pos_ < src_.size()) ? src_[pos_] : '\0';
    }

    char get() {
        if (pos_ >= src_.size()) throw std::runtime_error("Unexpected end of JSON input");
        return src_[pos_++];
    }

    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7Fu) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
            out.push_back(static_cast<char>(0xC0u | ((cp >> 6) & 0x1Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            out.push_back(static_cast<char>(0xE0u | ((cp >> 12) & 0x0Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | ((cp >> 18) & 0x07u)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }

    uint32_t parse_hex4() {
        if (pos_ + 4 > src_.size()) throw std::runtime_error("Incomplete \\uXXXX escape");
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = src_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(10 + (c - 'a'));
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(10 + (c - 'A'));
            else throw std::runtime_error("Invalid hex digit in \\uXXXX escape");
        }
        return v;
    }

    std::string parse_string_raw() {
        if (get() != '"') throw std::runtime_error("Expected '\"'");
        std::string out;
        while (pos_ < src_.size()) {
            char c = get();
            if (c == '"') return out;
            if (c == '\\') {
                char esc = get();
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
                        uint32_t cp = parse_hex4();
                        if (cp >= 0xD800u && cp <= 0xDBFFu && pos_ + 2 <= src_.size() &&
                            src_[pos_] == '\\' && src_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            uint32_t lo = parse_hex4();
                            if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                                cp = 0x10000u + (((cp - 0xD800u) << 10) | (lo - 0xDC00u));
                            }
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default:
                        out.push_back(esc);
                        break;
                }
            } else {
                out.push_back(c);
            }
        }
        throw std::runtime_error("Unterminated JSON string");
    }

    JsonVal parse_number() {
        size_t start = pos_;
        bool is_neg = false;
        if (peek() == '-') {
            is_neg = true;
            ++pos_;
        }
        if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
            throw std::runtime_error("Invalid JSON number");
        }
        while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
        }
        bool is_float = false;
        if (pos_ < src_.size() && src_[pos_] == '.') {
            is_float = true;
            ++pos_;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                ++pos_;
            }
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            is_float = true;
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                ++pos_;
            }
        }

        std::string token(src_.substr(start, pos_ - start));
        JsonVal v;
        if (is_float) {
            v.kind = JsonVal::Kind::Float;
            v.f_val = std::strtod(token.c_str(), nullptr);
        } else if (is_neg) {
            v.kind = JsonVal::Kind::Int;
            v.i_val = std::strtoll(token.c_str(), nullptr, 10);
        } else {
            char* endp = nullptr;
            unsigned long long uv = std::strtoull(token.c_str(), &endp, 10);
            if (uv <= static_cast<unsigned long long>(INT64_MAX)) {
                v.kind = JsonVal::Kind::Int;
                v.i_val = static_cast<int64_t>(uv);
                v.u_val = static_cast<uint64_t>(uv);
            } else {
                v.kind = JsonVal::Kind::Uint;
                v.u_val = static_cast<uint64_t>(uv);
            }
        }
        return v;
    }

    JsonVal parse_value(int depth) {
        if (depth > 128) throw std::runtime_error("JSON nesting depth exceeded");
        skip_ws();
        char c = peek();
        if (c == '"') {
            JsonVal v;
            v.kind = JsonVal::Kind::String;
            v.s_val = parse_string_raw();
            return v;
        }
        if (c == '{') {
            ++pos_;
            JsonVal v;
            v.kind = JsonVal::Kind::Object;
            skip_ws();
            if (peek() == '}') {
                ++pos_;
                return v;
            }
            while (true) {
                skip_ws();
                std::string key = parse_string_raw();
                skip_ws();
                if (get() != ':') throw std::runtime_error("Expected ':' in JSON object");
                JsonVal val = parse_value(depth + 1);
                v.obj_val.emplace(std::move(key), std::move(val));
                skip_ws();
                char sep = get();
                if (sep == '}') break;
                if (sep != ',') throw std::runtime_error("Expected ',' or '}' in JSON object");
            }
            return v;
        }
        if (c == '[') {
            ++pos_;
            JsonVal v;
            v.kind = JsonVal::Kind::Array;
            skip_ws();
            if (peek() == ']') {
                ++pos_;
                return v;
            }
            while (true) {
                v.arr_val.push_back(parse_value(depth + 1));
                skip_ws();
                char sep = get();
                if (sep == ']') break;
                if (sep != ',') throw std::runtime_error("Expected ',' or ']' in JSON array");
            }
            return v;
        }
        if (c == 't' && src_.substr(pos_, 4) == "true") {
            pos_ += 4;
            JsonVal v;
            v.kind = JsonVal::Kind::Bool;
            v.b_val = true;
            return v;
        }
        if (c == 'f' && src_.substr(pos_, 5) == "false") {
            pos_ += 5;
            JsonVal v;
            v.kind = JsonVal::Kind::Bool;
            v.b_val = false;
            return v;
        }
        if (c == 'n' && src_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return JsonVal{};
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return parse_number();
        }
        throw std::runtime_error("Unexpected token in JSON input");
    }
};

// ---------------------------------------------------------------------------
// Minimal JSON Output Helpers
// ---------------------------------------------------------------------------

void append_json_string(std::string& out, std::string_view s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\b': out.append("\\b"); break;
            case '\f': out.append("\\f"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                if (c < 0x20u) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out.append(buf);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out.push_back('"');
}

void append_json_double(std::string& out, double v) {
    if (!std::isfinite(v)) {
        out.append("null");
        return;
    }
    if (v == 0.0) v = 0.0; // normalize -0.0
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    out.append(buf);
}

void append_json_float(std::string& out, float v) {
    if (!std::isfinite(v)) {
        out.append("null");
        return;
    }
    if (v == 0.0f) v = 0.0f; // normalize -0.0f
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
    out.append(buf);
}

std::string make_error_json(std::string_view id, std::string_view err_msg) {
    std::string out = "{\"id\":";
    append_json_string(out, id);
    out.append(",\"status\":\"error\",\"error\":");
    append_json_string(out, err_msg);
    out.push_back('}');
    return out;
}

std::string make_unsupported_json(std::string_view id, std::string_view reason) {
    std::string out = "{\"id\":";
    append_json_string(out, id);
    out.append(",\"status\":\"unsupported\",\"reason\":");
    append_json_string(out, reason);
    out.push_back('}');
    return out;
}

// ---------------------------------------------------------------------------
// Schema Validation & Pipeline Builders
// ---------------------------------------------------------------------------

struct UnsupportedQueryOp : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

bool snapshot_has_domain(const impulse_snapshot_t* snap, const std::string& dom_name) {
    if (!snap || dom_name.empty()) return false;
    uint16_t dcnt = impulse_snapshot_domain_count(snap);
    for (uint16_t d = 0; d < dcnt; ++d) {
        impulse_domain_catalog_entry_t entry{};
        const char* name = nullptr;
        if (impulse_snapshot_get_domain_entry(snap, d, &entry, &name) == IMPULSE_OK && name && dom_name == name) {
            return true;
        }
        if (dom_name == ("domain_" + std::to_string(d))) return true;
    }
    return false;
}

bool snapshot_has_relation(const impulse_snapshot_t* snap, const std::string& rel_name) {
    if (!snap || rel_name.empty()) return false;
    uint16_t rcnt = impulse_snapshot_relation_count(snap);
    for (uint16_t r = 0; r < rcnt; ++r) {
        const char* name = nullptr;
        if (impulse_snapshot_get_relation_name(snap, r, &name) == IMPULSE_OK && name && rel_name == name) {
            return true;
        }
        if (rel_name == ("rel_" + std::to_string(r))) return true;
    }
    return false;
}

bool is_multilane_mode(const std::string& mode) {
    return mode == "multilane_seeds" || mode == "multilane_groups";
}

impulse::vm::MultiLaneTraversal build_multilane_pipeline(
    const impulse::vm::SnapshotView& sv,
    const JsonVal& pipe_json
);

impulse::vm::Traversal build_singlelane_pipeline(
    const impulse::vm::SnapshotView& sv,
    const JsonVal& pipe_json
) {
    const JsonVal& anchor = pipe_json.at_or_null("anchor");
    if (!anchor.is_object()) {
        throw std::runtime_error("Missing 'anchor' object");
    }
    std::string dom_name = anchor.get_str("domain");
    if (!snapshot_has_domain(sv.raw(), dom_name)) {
        throw std::runtime_error("Unknown domain: " + dom_name);
    }
    std::string mode = anchor.get_str("mode", "all");

    impulse::vm::DomainView dv = sv.domain(dom_name);
    std::optional<impulse::vm::Traversal> tr_opt;
    if (mode == "all") {
        tr_opt.emplace(dv.all());
    } else if (mode == "seeds") {
        std::vector<uint64_t> seeds;
        const JsonVal& s_arr = anchor.at_or_null("seeds");
        if (s_arr.is_array()) {
            seeds.reserve(s_arr.arr_val.size());
            for (const auto& sv_item : s_arr.arr_val) {
                seeds.push_back(sv_item.as_u64());
            }
        }
        tr_opt.emplace(dv.from(std::move(seeds)));
    } else if (is_multilane_mode(mode)) {
        return build_multilane_pipeline(sv, pipe_json).toQuorumTraversal(1);
    } else {
        throw UnsupportedQueryOp("Unsupported anchor.mode: " + mode);
    }

    auto& tr = *tr_opt;
    const JsonVal& steps = pipe_json.at_or_null("steps");
    if (steps.is_array()) {
        for (const auto& step : steps.arr_val) {
            std::string op = step.get_str("op");
            if (op == "filter") {
                tr.filter(step.get_str("cel"));
            } else if (op == "out") {
                std::string rel = step.get_str("relation");
                if (!snapshot_has_relation(sv.raw(), rel)) throw std::runtime_error("Unknown relation: " + rel);
                tr.out(rel);
            } else if (op == "in") {
                std::string rel = step.get_str("relation");
                if (!snapshot_has_relation(sv.raw(), rel)) throw std::runtime_error("Unknown relation: " + rel);
                tr.in(rel);
            } else if (op == "both") {
                std::string rel = step.get_str("relation");
                if (!snapshot_has_relation(sv.raw(), rel)) throw std::runtime_error("Unknown relation: " + rel);
                tr.both(rel);
            } else if (op == "out_union") {
                std::vector<std::string> rels;
                const JsonVal& r_arr = step.at_or_null("relations");
                if (r_arr.is_array()) {
                    for (const auto& r_item : r_arr.arr_val) {
                        if (r_item.kind == JsonVal::Kind::String) {
                            if (!snapshot_has_relation(sv.raw(), r_item.s_val)) {
                                throw std::runtime_error("Unknown relation: " + r_item.s_val);
                            }
                            rels.push_back(r_item.s_val);
                        }
                    }
                }
                tr.out(rels);
            } else if (op == "in_union") {
                std::vector<std::string> rels;
                const JsonVal& r_arr = step.at_or_null("relations");
                if (r_arr.is_array()) {
                    for (const auto& r_item : r_arr.arr_val) {
                        if (r_item.kind == JsonVal::Kind::String) {
                            if (!snapshot_has_relation(sv.raw(), r_item.s_val)) {
                                throw std::runtime_error("Unknown relation: " + r_item.s_val);
                            }
                            rels.push_back(r_item.s_val);
                        }
                    }
                }
                tr.in(rels);
            } else if (op == "out_where") {
                std::string rel = step.get_str("relation");
                if (!snapshot_has_relation(sv.raw(), rel)) throw std::runtime_error("Unknown relation: " + rel);
                tr.outPredicate(rel, step.get_str("cel"));
            } else if (op == "in_where") {
                std::string rel = step.get_str("relation");
                if (!snapshot_has_relation(sv.raw(), rel)) throw std::runtime_error("Unknown relation: " + rel);
                tr.inPredicate(rel, step.get_str("cel"));
            } else if (op == "repeat_out") {
                std::string rel = step.get_str("relation");
                if (!snapshot_has_relation(sv.raw(), rel)) throw std::runtime_error("Unknown relation: " + rel);
                int hops = static_cast<int>(step.at_or_null("hops").as_i64(1));
                tr.repeat(hops, rel);
            } else if (op == "fixpoint_out") {
                std::string rel = step.get_str("relation");
                if (!snapshot_has_relation(sv.raw(), rel)) throw std::runtime_error("Unknown relation: " + rel);
                uint32_t max_hops = static_cast<uint32_t>(step.at_or_null("max_hops").as_u64(1024));
                tr.repeatUntilStable(rel, max_hops);
            } else if (op == "set_op") {
                std::string sop = step.get_str("set_op");
                const JsonVal& branch = step.at_or_null("branch");
                if (!branch.is_object()) throw std::runtime_error("Missing 'branch' in set_op");
                auto br_tr = build_singlelane_pipeline(sv, branch);
                if (sop == "union") tr.unionWith(br_tr);
                else if (sop == "intersect") tr.intersect(br_tr);
                else if (sop == "difference") tr.difference(br_tr);
                else throw UnsupportedQueryOp("Unsupported set_op: " + sop);
            } else {
                throw UnsupportedQueryOp("Unsupported single-lane step op: " + op);
            }
        }
    }
    return tr;
}

impulse::vm::MultiLaneTraversal build_multilane_pipeline(
    const impulse::vm::SnapshotView& sv,
    const JsonVal& pipe_json
) {
    const JsonVal& anchor = pipe_json.at_or_null("anchor");
    if (!anchor.is_object()) {
        throw std::runtime_error("Missing 'anchor' object");
    }
    std::string dom_name = anchor.get_str("domain");
    if (!snapshot_has_domain(sv.raw(), dom_name)) {
        throw std::runtime_error("Unknown domain: " + dom_name);
    }
    std::string mode = anchor.get_str("mode");

    impulse::vm::DomainView dv = sv.domain(dom_name);
    std::optional<impulse::vm::MultiLaneTraversal> ml_opt;
    if (mode == "multilane_seeds") {
        std::vector<uint64_t> seeds;
        const JsonVal& s_arr = anchor.at_or_null("seeds");
        if (s_arr.is_array()) {
            seeds.reserve(s_arr.arr_val.size());
            for (const auto& sv_item : s_arr.arr_val) {
                seeds.push_back(sv_item.as_u64());
            }
        }
        ml_opt.emplace(dv.multiLane().fromSeeds(std::move(seeds)));
    } else if (mode == "multilane_groups") {
        const JsonVal& g_arr = anchor.at_or_null("groups");
        uint32_t max_grp = 0;
        bool has_any_grp = false;
        std::vector<std::pair<uint64_t, uint32_t>> raw_pairs;
        if (g_arr.is_array()) {
            for (const auto& pair_v : g_arr.arr_val) {
                if (pair_v.is_array() && pair_v.arr_val.size() >= 2) {
                    uint64_t nid = pair_v.arr_val[0].as_u64();
                    uint32_t gidx = static_cast<uint32_t>(pair_v.arr_val[1].as_u64());
                    raw_pairs.emplace_back(nid, gidx);
                    if (!has_any_grp || gidx > max_grp) {
                        max_grp = gidx;
                        has_any_grp = true;
                    }
                }
            }
        }
        size_t num_groups = has_any_grp ? (static_cast<size_t>(max_grp) + 1) : 0;
        std::vector<std::pair<std::string, std::vector<uint64_t>>> groups(num_groups);
        for (size_t g = 0; g < num_groups; ++g) {
            groups[g].first = "g" + std::to_string(g);
        }
        for (const auto& rp : raw_pairs) {
            if (rp.second < groups.size()) {
                groups[rp.second].second.push_back(rp.first);
            }
        }
        ml_opt.emplace(dv.multiLane().fromGroups(groups));
    } else {
        throw UnsupportedQueryOp("Unsupported multi-lane anchor.mode: " + mode);
    }

    auto& ml = *ml_opt;
    const JsonVal& steps = pipe_json.at_or_null("steps");
    if (steps.is_array()) {
        for (const auto& step : steps.arr_val) {
            std::string op = step.get_str("op");
            if (op == "filter") {
                ml.filter(step.get_str("cel"));
            } else if (op == "out") {
                std::string rel = step.get_str("relation");
                if (!snapshot_has_relation(sv.raw(), rel)) throw std::runtime_error("Unknown relation: " + rel);
                ml.out(rel);
            } else if (op == "in") {
                std::string rel = step.get_str("relation");
                if (!snapshot_has_relation(sv.raw(), rel)) throw std::runtime_error("Unknown relation: " + rel);
                ml.in(rel);
            } else if (op == "both") {
                std::string rel = step.get_str("relation");
                if (!snapshot_has_relation(sv.raw(), rel)) throw std::runtime_error("Unknown relation: " + rel);
                ml.both(rel);
            } else if (op == "out_union") {
                std::vector<std::string> rels;
                const JsonVal& r_arr = step.at_or_null("relations");
                if (r_arr.is_array()) {
                    for (const auto& r_item : r_arr.arr_val) {
                        if (r_item.kind == JsonVal::Kind::String) {
                            if (!snapshot_has_relation(sv.raw(), r_item.s_val)) {
                                throw std::runtime_error("Unknown relation: " + r_item.s_val);
                            }
                            rels.push_back(r_item.s_val);
                        }
                    }
                }
                ml.out(rels);
            } else if (op == "in_union") {
                std::vector<std::string> rels;
                const JsonVal& r_arr = step.at_or_null("relations");
                if (r_arr.is_array()) {
                    for (const auto& r_item : r_arr.arr_val) {
                        if (r_item.kind == JsonVal::Kind::String) {
                            if (!snapshot_has_relation(sv.raw(), r_item.s_val)) {
                                throw std::runtime_error("Unknown relation: " + r_item.s_val);
                            }
                            rels.push_back(r_item.s_val);
                        }
                    }
                }
                ml.in(rels);
            } else if (op == "out_where") {
                std::string rel = step.get_str("relation");
                if (!snapshot_has_relation(sv.raw(), rel)) throw std::runtime_error("Unknown relation: " + rel);
                ml.outPredicate(rel, step.get_str("cel"));
            } else if (op == "in_where") {
                std::string rel = step.get_str("relation");
                if (!snapshot_has_relation(sv.raw(), rel)) throw std::runtime_error("Unknown relation: " + rel);
                ml.inPredicate(rel, step.get_str("cel"));
            } else if (op == "repeat_out") {
                std::string rel = step.get_str("relation");
                if (!snapshot_has_relation(sv.raw(), rel)) throw std::runtime_error("Unknown relation: " + rel);
                int hops = static_cast<int>(step.at_or_null("hops").as_i64(1));
                ml.repeat(hops, rel);
            } else if (op == "fixpoint_out") {
                std::string rel = step.get_str("relation");
                if (!snapshot_has_relation(sv.raw(), rel)) throw std::runtime_error("Unknown relation: " + rel);
                uint32_t max_hops = static_cast<uint32_t>(step.at_or_null("max_hops").as_u64(1024));
                ml.repeatUntilStable(rel, max_hops);
            } else if (op == "where_quorum") {
                uint8_t min_k = static_cast<uint8_t>(step.at_or_null("min_k").as_u64(1));
                if (step.has("req_mask") || step.has("forbid_mask")) {
                    uint64_t req_mask = step.has("req_mask") ? step.at_or_null("req_mask").as_u64(~0ULL) : ~0ULL;
                    uint64_t forbid_mask = step.has("forbid_mask") ? step.at_or_null("forbid_mask").as_u64(0ULL) : 0ULL;
                    ml.whereQuorum(min_k, req_mask, forbid_mask);
                } else {
                    ml.whereQuorum(min_k);
                }
            } else if (op == "set_op") {
                std::string sop = step.get_str("set_op");
                const JsonVal& branch = step.at_or_null("branch");
                if (!branch.is_object()) throw std::runtime_error("Missing 'branch' in set_op");
                auto br_ml = build_multilane_pipeline(sv, branch);
                if (sop == "union") ml.unionWith(br_ml);
                else if (sop == "intersect") ml.intersect(br_ml);
                else if (sop == "difference") ml.difference(br_ml);
                else throw UnsupportedQueryOp("Unsupported multi-lane set_op: " + sop);
            } else {
                throw UnsupportedQueryOp("Unsupported multi-lane step op: " + op);
            }
        }
    }
    return ml;
}

std::string execute_query_line(const impulse::vm::SnapshotView& sv, std::string_view line) {
    std::string qid = "unknown";
    try {
        JsonParser parser(line);
        JsonVal root = parser.parse();
        if (!root.is_object()) {
            return make_error_json(qid, "Query JSON must be an object");
        }
        qid = root.get_str("id", "unknown");

        const JsonVal& anchor = root.at_or_null("anchor");
        const JsonVal& terminal = root.at_or_null("terminal");
        if (!anchor.is_object() || !terminal.is_object()) {
            return make_error_json(qid, "Missing 'anchor' or 'terminal' object");
        }

        std::string mode = anchor.get_str("mode", "all");
        std::string term_op = terminal.get_str("op");
        bool multi = is_multilane_mode(mode);

        std::string out = "{\"id\":";
        append_json_string(out, qid);
        out.append(",\"status\":\"ok\",\"result_type\":");

        if (term_op == "lane_masks") {
            if (!multi) {
                return make_unsupported_json(qid, "Terminal 'lane_masks' requires a multi-lane anchor");
            }
            auto res = build_multilane_pipeline(sv, root).collect();
            out.append("\"lane_masks\",\"lanes\":[");
            bool first = true;
            uint32_t n = res.domainNodeCount();
            for (uint32_t u = 0; u < n; ++u) {
                uint64_t m = res.laneMaskFor(u);
                if (m != 0ULL) {
                    if (!first) out.push_back(',');
                    first = false;
                    out.push_back('[');
                    out.append(std::to_string(u));
                    out.push_back(',');
                    out.append(std::to_string(m));
                    out.push_back(']');
                }
            }
            out.append("]}");
            return out;
        }

        if (term_op == "set_cover") {
            if (!multi) {
                return make_unsupported_json(qid, "Terminal 'set_cover' requires a multi-lane anchor");
            }
            auto res = build_multilane_pipeline(sv, root).collect();
            uint64_t target_mask = ~0ULL;
            if (terminal.has("target_mask")) {
                target_mask = terminal.at_or_null("target_mask").as_u64(~0ULL);
            } else if (terminal.has("target")) {
                target_mask = terminal.at_or_null("target").as_u64(~0ULL);
            }
            uint32_t max_nodes = terminal.has("max_nodes")
                ? static_cast<uint32_t>(terminal.at_or_null("max_nodes").as_u64(UINT32_MAX))
                : UINT32_MAX;
            auto cover = res.findGreedySetCover(target_mask, max_nodes);
            out.append("\"nodes\",\"nodes\":[");
            for (size_t i = 0; i < cover.size(); ++i) {
                if (i > 0) out.push_back(',');
                out.append(std::to_string(cover[i]));
            }
            out.append("]}");
            return out;
        }

        // Remaining terminals operate on single-lane Traversal (or multi-lane active frontier with quorum >= 1)
        impulse::vm::Traversal tr = multi
            ? build_multilane_pipeline(sv, root).toQuorumTraversal(1)
            : build_singlelane_pipeline(sv, root);

        if (term_op == "nodes") {
            auto nodes = tr.collect();
            out.append("\"nodes\",\"nodes\":[");
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (i > 0) out.push_back(',');
                out.append(std::to_string(nodes[i]));
            }
            out.append("]}");
            return out;
        }

        if (term_op == "keys") {
            auto keys = tr.collectKeys();
            out.append("\"keys\",\"keys\":[");
            for (size_t i = 0; i < keys.size(); ++i) {
                if (i > 0) out.push_back(',');
                append_json_string(out, keys[i]);
            }
            out.append("]}");
            return out;
        }

        if (term_op == "count") {
            size_t cnt = tr.count();
            out.append("\"count\",\"count\":");
            out.append(std::to_string(cnt));
            out.push_back('}');
            return out;
        }

        if (term_op == "sum" || term_op == "min" || term_op == "max") {
            std::string expr = terminal.get_str("expr_cel");
            if (expr.empty()) expr = terminal.get_str("attr");
            if (expr.empty()) {
                return make_error_json(qid, "Missing 'attr' or 'expr_cel' for scalar reduction");
            }
            impulse::vm::ReducerOp rop = impulse::vm::ReducerOp::SUM;
            if (term_op == "min") rop = impulse::vm::ReducerOp::MIN;
            else if (term_op == "max") rop = impulse::vm::ReducerOp::MAX;

            auto val = tr.reduceAttr(expr, rop);
            out.append("\"scalar\",\"scalar\":");
            if (!val.has_value()) {
                out.append("null");
            } else {
                append_json_double(out, *val);
            }
            out.push_back('}');
            return out;
        }

        if (term_op == "top_k") {
            std::string attr = terminal.get_str("attr");
            uint8_t metric = static_cast<uint8_t>(terminal.at_or_null("metric").as_u64(0));
            size_t k = static_cast<size_t>(
                terminal.has("top_k") ? terminal.at_or_null("top_k").as_u64(10)
                                      : terminal.at_or_null("k").as_u64(10));
            std::vector<float> qvec;
            const JsonVal& qv = terminal.at_or_null("query_vec");
            if (qv.is_array()) {
                qvec.reserve(qv.arr_val.size());
                for (const auto& elem : qv.arr_val) {
                    qvec.push_back(static_cast<float>(elem.as_f64()));
                }
            }
            auto pairs = tr.rerank(qvec, attr, metric).topK(k);
            out.append("\"top_k\",\"nodes\":[");
            for (size_t i = 0; i < pairs.size(); ++i) {
                if (i > 0) out.push_back(',');
                out.append(std::to_string(pairs[i].first));
            }
            out.append("],\"scores\":[");
            for (size_t i = 0; i < pairs.size(); ++i) {
                if (i > 0) out.push_back(',');
                append_json_float(out, pairs[i].second);
            }
            out.append("]}");
            return out;
        }

        return make_unsupported_json(qid, "Unsupported terminal.op: " + term_op);
    } catch (const UnsupportedQueryOp& uex) {
        return make_unsupported_json(qid, uex.what());
    } catch (const std::exception& ex) {
        return make_error_json(qid, ex.what());
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <snapshot.imps> <queries.jsonl>\n", argv[0]);
        return 2;
    }

    const char* snap_path = argv[1];
    const char* jsonl_path = argv[2];

    impulse_status_t status = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(snap_path, &status);
    if (!snap || status != IMPULSE_OK) {
        std::fprintf(stderr, "Failed to open snapshot '%s' (status=%d)\n", snap_path, static_cast<int>(status));
        return 1;
    }

    std::ifstream infile(jsonl_path);
    if (!infile.is_open()) {
        std::fprintf(stderr, "Failed to open queries file '%s'\n", jsonl_path);
        impulse_snapshot_close(snap);
        return 1;
    }

    std::ios::sync_with_stdio(false);
    impulse::vm::SnapshotView sv(snap);

    std::string line;
    while (std::getline(infile, line)) {
        size_t first = 0;
        while (first < line.size() && (line[first] == ' ' || line[first] == '\t' || line[first] == '\r')) {
            ++first;
        }
        if (first == line.size()) continue;
        std::string resp = execute_query_line(sv, std::string_view(line).substr(first));
        std::cout << resp << '\n';
    }
    std::cout.flush();

    impulse_snapshot_close(snap);
    return 0;
}
