/**
 * @file bench_storage_coverage.cpp
 * @brief Self-describing, metadata-driven C++20 v0.11 Storage Coverage,
 *        Multi-Lane vs Single-Lane, Morsel, and Attribute-Type Benchmark & Equivalence Runner.
 */
#include "vm/impulse_bytecode_emitter.hpp"
#include "impulse_vm_v11.h"
#include "impulse_graph.h"

#undef OP_HALT
#undef OP_SET_CARDINALITY
#undef OP_GATHER_EDGE_ATTR
#undef OP_LANE_INIT_SEEDS

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace impulse::compiler::v11;
using namespace impulse::vm;

struct RunRes {
    uint32_t status = 0;
    double us = 0.0;
    int64_t val = 0;
    double fval = 0.0;
    bool has_fval = false;
};

static RunRes run_timed_prog(
    const std::vector<uint8_t>& impb,
    const impulse_snapshot_t* snap,
    const impulse_vm_params_v11_t* params,
    impulse_vm_workspace_v11_t* ws,
    int warmup,
    int iters
) {
    RunRes res{};
    for (int i = 0; i < warmup; ++i) {
        uint32_t st = impulse_vm_execute_v11(
            impb.data(), impb.size(), snap, params, ws, nullptr, 0, nullptr);
        if (st != IMPULSE_VM_V11_OK) {
            res.status = st;
            return res;
        }
    }
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(iters));
    for (int i = 0; i < iters; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        uint32_t st = impulse_vm_execute_v11(
            impb.data(), impb.size(), snap, params, ws, nullptr, 0, nullptr);
        auto t1 = std::chrono::steady_clock::now();
        if (st != IMPULSE_VM_V11_OK) {
            res.status = st;
            return res;
        }
        samples.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    std::sort(samples.begin(), samples.end());
    res.status = IMPULSE_VM_V11_OK;
    res.us = samples.empty() ? 0.0 : samples[samples.size() / 2];

    impulse_vm_output_view_v11_t ov{};
    if (impulse_vm_v11_get_output(ws, 0, &ov) == 1 && ov.length > 0) {
        if ((ov.elem_tag == 0 || ov.elem_tag == 1) && ov.i64_data) {
            res.val = ov.i64_data[0];
        } else if (ov.elem_tag == 4 && ov.f64_data) {
            res.fval = ov.f64_data[0];
            res.has_fval = true;
        }
    }
    return res;
}

static std::string get_meta(const impulse_snapshot_t* snap, const char* key, const char* def = "") {
    char buf[4096];
    std::memset(buf, 0, sizeof(buf));
    if (impulse_snapshot_get_metadata(snap, key, buf, sizeof(buf)) == IMPULSE_OK && buf[0] != '\0') {
        return std::string(buf);
    }
    return std::string(def);
}

static std::vector<uint64_t> parse_u64_csv(const std::string& s) {
    std::vector<uint64_t> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (!tok.empty()) out.push_back(std::strtoull(tok.c_str(), nullptr, 10));
    }
    return out;
}

static bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

static std::vector<uint8_t> build_walk_2hop(uint16_t rel_id, uint16_t in_dom, uint8_t walk_subop, uint16_t dom_req) {
    V11ProgramBuilder b;
    b.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_FRONTIER, 0, 0, 0, 0, 0, 0, in_dom);
    b.emit_u16(Opcode::OP_WALK, walk_subop, 0, 1, 0, 0, 0, rel_id, 0);
    b.emit_u16(Opcode::OP_WALK, walk_subop, 0, 0, 1, 0, 0, rel_id, 0);
    b.emit(Opcode::OP_SET_CARDINALITY, subop::kSET_CARDINALITY_CARD_1BIT, 0, 0, 0, 0, 0, 0);
    b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, 0, 0, 0, 0, 0, 0, 0);
    b.emit(Opcode::OP_HALT, 0, 0, 0, 0, 0, 0, 0);
    return b.build_impb(0, 0, dom_req);
}

static std::vector<uint8_t> build_walk_1hop(uint16_t rel_id, uint16_t in_dom, uint8_t walk_subop, uint16_t dom_req) {
    V11ProgramBuilder b;
    b.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_FRONTIER, 0, 0, 0, 0, 0, 0, in_dom);
    b.emit_u16(Opcode::OP_WALK, walk_subop, 0, 1, 0, 0, 0, rel_id, 0);
    b.emit(Opcode::OP_SET_CARDINALITY, subop::kSET_CARDINALITY_CARD_1BIT, 0, 0, 1, 0, 0, 0);
    b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, 0, 0, 0, 0, 0, 0, 0);
    b.emit(Opcode::OP_HALT, 0, 0, 0, 0, 0, 0, 0);
    return b.build_impb(0, 0, dom_req);
}

static std::vector<uint8_t> build_morsel_1hop(uint16_t rel_id, uint16_t in_dom, uint16_t tgt_dom, uint8_t morsel_subop, uint16_t dom_req) {
    V11ProgramBuilder b;
    b.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_FRONTIER, 0, 0, 0, 0, 0, 0, in_dom);
    b.emit_u16(Opcode::OP_WALK_MORSEL, morsel_subop, 0, 0, 0, 0, 0, rel_id, 2);
    b.emit_u16(Opcode::OP_MORSEL_COMMIT, subop::kMORSEL_COMMIT_YIELD_FRONTIER, 0, 1, 9, 0, 0, tgt_dom, 0);
    b.emit_u16(Opcode::OP_MORSEL_END, 0, 0, 0, 0, 0, 0, 0, 2);
    b.emit(Opcode::OP_SET_CARDINALITY, subop::kSET_CARDINALITY_CARD_1BIT, 0, 0, 1, 0, 0, 0);
    b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, 0, 0, 0, 0, 0, 0, 0);
    b.emit(Opcode::OP_HALT, 0, 0, 0, 0, 0, 0, 0);
    return b.build_impb(0, 0, dom_req);
}

static int64_t pow10_i64(uint8_t s) {
    int64_t r = 1;
    for (uint8_t i = 0; i < s; ++i) r *= 10;
    return r;
}

static std::vector<uint8_t> build_node_attr_eq40(uint16_t dom_id, uint16_t attr_idx, bool is_float, int64_t target_i64, uint16_t dom_req) {
    V11ProgramBuilder b;
    b.emit_u16(Opcode::OP_LOAD_COLUMN, 0, 0, 0, 0, 0, 0, dom_id, attr_idx);
    if (is_float) {
        float f40 = 40.0f;
        uint32_t fbits = 0;
        std::memcpy(&fbits, &f40, 4);
        b.emit(Opcode::OP_LOAD_CONST, subop::kLOAD_CONST_F32, 0, 1, 0, 0, 0, fbits);
    } else {
        b.emit_load_const_i64(1, target_i64);
    }
    b.emit(Opcode::OP_VEC_CMP, subop::kVEC_CMP_EQ, FLAG_SCALAR_RHS, 1, 0, 1, 0, 0);
    b.emit_u16(Opcode::OP_SET_CONVERT, 2, 0, 0, 1, 0, 0, dom_id, 0); // bool_vec -> frontier
    b.emit(Opcode::OP_SET_CARDINALITY, subop::kSET_CARDINALITY_CARD_1BIT, 0, 0, 0, 0, 0, 0);
    b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, 0, 0, 0, 0, 0, 0, 0);
    b.emit(Opcode::OP_HALT, 0, 0, 0, 0, 0, 0, 0);
    return b.build_impb(0, 0, dom_req);
}

static std::vector<uint8_t> build_edge_attr_morsel_eq40(uint16_t rel_id, uint16_t in_dom, uint16_t attr_idx, bool is_float, int64_t target_i64, uint16_t dom_req) {
    V11ProgramBuilder b;
    b.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_FRONTIER, 0, 0, 0, 0, 0, 0, in_dom);
    b.emit_u16(Opcode::OP_WALK, subop::kWALK_FWD, 0, 1, 0, 0, 0, rel_id, 0);
    if (is_float) {
        float f40 = 40.0f;
        uint32_t fbits = 0;
        std::memcpy(&fbits, &f40, 4);
        b.emit(Opcode::OP_LOAD_CONST, subop::kLOAD_CONST_F32, 0, 1, 0, 0, 0, fbits);
    } else {
        b.emit_load_const_i64(1, target_i64);
    }
    b.emit(Opcode::OP_LOAD_CONST, subop::kLOAD_CONST_I32, 0, 0, 0, 0, 0, 0u);
    b.emit_u16(Opcode::OP_WALK_MORSEL, subop::kWALK_MORSEL_FWD, 0, 0, 1, 0, 0, rel_id, 5);
    b.emit_u16(Opcode::OP_GATHER_EDGE_ATTR, subop::kGATHER_EDGE_ATTR_VEC, 0, 12, 10, 0, 0, rel_id, attr_idx);
    b.emit(Opcode::OP_VEC_CMP, subop::kVEC_CMP_EQ, FLAG_SCALAR_RHS, 13, 12, 1, 0, 0);
    b.emit_u16(Opcode::OP_SET_CONVERT, 2, 0, 9, 13, 0, 0, 0xFFFF, 0);
    b.emit_u16(Opcode::OP_MORSEL_COMMIT, subop::kMORSEL_COMMIT_COUNT_ADD_R, FLAG_MASKED_S3, 0, 0, 0, 9, 0, 0);
    b.emit_u16(Opcode::OP_MORSEL_END, 0, 0, 0, 0, 0, 0, 0, 5);
    b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, 0, 0, 0, 0, 0, 0, 0);
    b.emit(Opcode::OP_HALT, 0, 0, 0, 0, 0, 0, 0);
    return b.build_impb(0, 0, dom_req);
}

static std::vector<uint8_t> build_node_attr_reduce(uint16_t dom_id, uint16_t attr_idx, uint8_t red_subop, uint16_t dom_req) {
    V11ProgramBuilder b;
    b.emit_u16(Opcode::OP_LOAD_COLUMN, 0, 0, 0, 0, 0, 0, dom_id, attr_idx);
    b.emit(Opcode::OP_VEC_REDUCE, red_subop, 0, 0, 0, 0, 0, 0);
    b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, 0, 0, 0, 0, 0, 0, 0);
    b.emit(Opcode::OP_HALT, 0, 0, 0, 0, 0, 0, 0);
    return b.build_impb(0, 0, dom_req);
}

static std::vector<uint8_t> build_ml_2hop(uint16_t rel_id, uint16_t dom_id, bool force_w64, uint16_t dom_req) {
    V11ProgramBuilder b;
    b.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_VEC, 0, 0, 0, 0, 0, 0, 2);
    b.emit_u16(Opcode::OP_LANE_INIT_SEEDS, force_w64 ? subop::kLANE_INIT_SEEDS_FORCE_W64 : subop::kLANE_INIT_SEEDS_AUTO_WIDTH,
               0, 0, 0, 0, 0, dom_id, 0);
    b.emit_u16(Opcode::OP_LANE_WALK, subop::kLANE_WALK_FWD, 0, 1, 0, 0, 0, rel_id, 0);
    b.emit_u16(Opcode::OP_LANE_WALK, subop::kLANE_WALK_FWD, 0, 0, 1, 0, 0, rel_id, 0);
    b.emit(Opcode::OP_SET_CARDINALITY, subop::kSET_CARDINALITY_LANE_POP_SUM, 0, 0, 0, 0, 0, 0);
    b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, 0, 0, 0, 0, 0, 0, 0);
    b.emit(Opcode::OP_HALT, 0, 0, 0, 0, 0, 0, 0);
    return b.build_impb(0, 0, dom_req);
}

static std::vector<uint8_t> build_ml_morsel_2hop(uint16_t rel_id, uint16_t dom_id, bool force_w64, uint16_t dom_req) {
    V11ProgramBuilder b;
    b.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_VEC, 0, 0, 0, 0, 0, 0, 2);
    b.emit_u16(Opcode::OP_LANE_INIT_SEEDS, force_w64 ? subop::kLANE_INIT_SEEDS_FORCE_W64 : subop::kLANE_INIT_SEEDS_AUTO_WIDTH,
               0, 0, 0, 0, 0, dom_id, 0);
    b.emit_u16(Opcode::OP_LANE_WALK, subop::kLANE_WALK_FWD, 0, 1, 0, 0, 0, rel_id, 0);
    b.emit_u16(Opcode::OP_WALK_MORSEL, subop::kWALK_MORSEL_FWD, 0, 0, 1, 0, 0, rel_id, 2);
    b.emit_u16(Opcode::OP_MORSEL_COMMIT, subop::kMORSEL_COMMIT_YIELD_LANE_OR, 0, 2, 9, 11, 0, dom_id, 0);
    b.emit_u16(Opcode::OP_MORSEL_END, 0, 0, 0, 0, 0, 0, 0, 2);
    b.emit(Opcode::OP_SET_CARDINALITY, subop::kSET_CARDINALITY_LANE_POP_SUM, 0, 0, 2, 0, 0, 0);
    b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, 0, 0, 0, 0, 0, 0, 0);
    b.emit(Opcode::OP_HALT, 0, 0, 0, 0, 0, 0, 0);
    return b.build_impb(0, 0, dom_req);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <storage_coverage.imps> [warmup=10] [iters=50]\n", argv[0]);
        return 1;
    }
    const char* path = argv[1];
    int warmup = (argc > 2) ? std::atoi(argv[2]) : 10;
    int iters  = (argc > 3) ? std::atoi(argv[3]) : 50;

    impulse_status_t open_st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(path, &open_st);
    if (!snap || open_st != IMPULSE_OK) {
        std::fprintf(stderr, "Failed to open snapshot %s (status=%d)\n", path, open_st);
        return 1;
    }
    if (impulse_vm_v11_prepare_snapshot(snap) != IMPULSE_VM_V11_OK) {
        std::fprintf(stderr, "Failed to prepare v11 snapshot view\n");
        return 1;
    }

    const size_t ws_bytes = 64u * 1024u * 1024u;
    std::vector<uint8_t> raw_buf(ws_bytes + 128u);
    void* raw = reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(raw_buf.data()) + 127u) & ~uintptr_t(127u));
    impulse_vm_workspace_v11_t* ws = nullptr;
    if (impulse_workspace_init(raw, ws_bytes, &ws) != IMPULSE_VM_V11_OK) return 1;

    uint16_t dom_cnt = impulse_snapshot_domain_count(snap);
    uint16_t rel_cnt = impulse_snapshot_relation_count(snap);

    std::string canon_dom_name = get_meta(snap, "coverage.canonical_dom", "EQ_DOM_U32_I32");
    std::string canon_rel_name = get_meta(snap, "coverage.canonical_rel", "EQ_REL_ALL3_U32");
    std::vector<uint64_t> seeds = parse_u64_csv(get_meta(snap, "coverage.seed_nodes", "1,2,5,10,40,63,64,65"));
    if (seeds.empty()) seeds.push_back(5);
    uint64_t main_seed = (seeds.size() > 4) ? seeds[4] : seeds[0]; // supernode 5

    uint16_t canon_dom_id = 0;
    for (uint16_t d = 0; d < dom_cnt; ++d) {
        impulse_domain_catalog_entry_t de{};
        const char* dname = nullptr;
        if (impulse_snapshot_get_domain_entry(snap, d, &de, &dname) == IMPULSE_OK && dname && canon_dom_name == dname) {
            canon_dom_id = de.domain_id;
            break;
        }
    }

    uint16_t canon_rel_id = 0;
    for (uint16_t r = 0; r < rel_cnt; ++r) {
        const char* rname = nullptr;
        if (impulse_snapshot_get_relation_name(snap, r, &rname) == IMPULSE_OK && rname && canon_rel_name == rname) {
            canon_rel_id = r;
            break;
        }
    }

    std::printf("{\"engine\":\"cpp-v11\",\"snapshot\":\"%s\",\"domains\":%u,\"relations\":%u,\"topology\":[",
                path, dom_cnt, rel_cnt);

    bool first_rel = true;
    for (uint16_t r = 0; r < rel_cnt; ++r) {
        const char* rname_c = nullptr;
        if (impulse_snapshot_get_relation_name(snap, r, &rname_c) != IMPULSE_OK || !rname_c) continue;
        std::string rname(rname_c);
        if (!starts_with(rname, "EQ_REL_")) continue;

        impulse_relation_directory_entry_t re{};
        impulse_snapshot_get_relation_entry(snap, r, &re);
        bool has_csr = (re.csr_row_off_bytes > 0 && re.csr_col_idx_bytes > 0);
        bool has_csc = (re.csc_row_off_bytes > 0 && re.csc_col_idx_bytes > 0);
        bool has_coo = ((re.section_features & IMPULSE_SECTION_FEAT_HAS_COO) != 0);

        auto run_rel = [&](uint8_t subop, uint16_t in_dom, bool is_morsel) -> RunRes {
            auto prog = is_morsel ? build_morsel_1hop(r, in_dom, (subop == 1 || subop == 3) ? re.src_domain_id : re.tgt_domain_id, subop, dom_cnt)
                                  : build_walk_1hop(r, in_dom, subop, dom_cnt);
            impulse_vm_param_binding_v11_t bind{};
            bind.param_id = 0;
            bind.kind = IMPULSE_VM_V11_PARAM_FRONTIER_IDS;
            bind.domain_id = in_dom;
            bind.count = 1;
            bind.data_ptr = &main_seed;
            impulse_vm_params_v11_t params{};
            params.bindings = &bind;
            params.binding_count = 1;
            return run_timed_prog(prog, snap, &params, ws, warmup, iters);
        };

        RunRes r_csr  = run_rel(subop::kWALK_FWD, re.src_domain_id, false);
        RunRes r_csc  = run_rel(subop::kWALK_REV, re.tgt_domain_id, false);
        RunRes r_coof = run_rel(subop::kWALK_COO_FWD, re.src_domain_id, false);
        RunRes r_coor = run_rel(subop::kWALK_COO_REV, re.tgt_domain_id, false);
        RunRes r_mors = has_csr ? run_rel(subop::kWALK_MORSEL_FWD, re.src_domain_id, true) : RunRes{IMPULSE_VM_V11_ERR_INVALID_OPERAND, 0.0, -1};

        if (!first_rel) std::printf(",");
        first_rel = false;
        std::printf("{\"name\":\"%s\",\"rel_id\":%u,\"src_dom\":%u,\"tgt_dom\":%u,\"has_csr\":%s,\"has_csc\":%s,\"has_coo\":%s,"
                    "\"csr\":{\"st\":%u,\"us\":%.2f,\"card\":%lld},\"csc\":{\"st\":%u,\"us\":%.2f,\"card\":%lld},"
                    "\"coo_fwd\":{\"st\":%u,\"us\":%.2f,\"card\":%lld},\"coo_rev\":{\"st\":%u,\"us\":%.2f,\"card\":%lld},"
                    "\"morsel_fwd\":{\"st\":%u,\"us\":%.2f,\"card\":%lld}}",
                    rname.c_str(), r, re.src_domain_id, re.tgt_domain_id,
                    has_csr ? "true" : "false", has_csc ? "true" : "false", has_coo ? "true" : "false",
                    r_csr.status, r_csr.us, (long long)r_csr.val,
                    r_csc.status, r_csc.us, (long long)r_csc.val,
                    r_coof.status, r_coof.us, (long long)r_coof.val,
                    r_coor.status, r_coor.us, (long long)r_coor.val,
                    r_mors.status, r_mors.us, (long long)r_mors.val);
    }

    // Attribute type sweep (Node + Edge EQ_NUM_* and EQ_NUM_NULL_*)
    std::printf("],\"attrs\":[");
    bool first_attr = true;
    uint16_t dom_attr_cnt = impulse_snapshot_get_domain_attribute_count(snap, canon_dom_id);
    for (uint16_t a = 0; a < dom_attr_cnt; ++a) {
        const char* aname_c = nullptr;
        if (impulse_snapshot_get_domain_attribute_name(snap, canon_dom_id, a, &aname_c) != IMPULSE_OK || !aname_c) continue;
        std::string aname(aname_c);
        if (!starts_with(aname, "EQ_NUM_")) continue;
        bool is_flt = (aname.find("_F32") != std::string::npos || aname.find("_F64") != std::string::npos);
        int64_t target_i64 = 40;
        uint16_t dec_p = 0;
        uint8_t dec_s = 0;
        if (impulse_snapshot_get_domain_attribute_decimal64_meta(snap, canon_dom_id, a, &dec_p, &dec_s) == IMPULSE_OK) {
            target_i64 = 40LL * pow10_i64(dec_s);
        }
        auto prog = build_node_attr_eq40(canon_dom_id, a, is_flt, target_i64, dom_cnt);
        impulse_vm_params_v11_t params{};
        RunRes rr = run_timed_prog(prog, snap, &params, ws, warmup, iters);
        if (!first_attr) std::printf(",");
        first_attr = false;
        std::printf("{\"scope\":\"node\",\"name\":\"%s\",\"idx\":%u,\"st\":%u,\"us\":%.2f,\"count\":%lld}",
                    aname.c_str(), a, rr.status, rr.us, (long long)rr.val);
    }

    impulse_relation_directory_entry_t cre{};
    impulse_snapshot_get_relation_entry(snap, canon_rel_id, &cre);
    for (uint16_t a = 0; a < cre.attr_count; ++a) {
        const char* aname_c = nullptr;
        if (impulse_snapshot_get_attribute_name(snap, canon_rel_id, a, &aname_c) != IMPULSE_OK || !aname_c) continue;
        std::string aname(aname_c);
        if (!starts_with(aname, "EQ_NUM_")) continue;
        bool is_flt = (aname.find("_F32") != std::string::npos || aname.find("_F64") != std::string::npos);
        int64_t target_i64 = 40;
        uint16_t dec_p = 0;
        uint8_t dec_s = 0;
        if (impulse_snapshot_get_attribute_decimal64_meta(snap, canon_rel_id, a, &dec_p, &dec_s) == IMPULSE_OK) {
            target_i64 = 40LL * pow10_i64(dec_s);
        }
        auto prog = build_edge_attr_morsel_eq40(canon_rel_id, cre.src_domain_id, a, is_flt, target_i64, dom_cnt);
        impulse_vm_param_binding_v11_t bind{};
        bind.param_id = 0;
        bind.kind = IMPULSE_VM_V11_PARAM_FRONTIER_IDS;
        bind.domain_id = cre.src_domain_id;
        bind.count = 1;
        bind.data_ptr = &main_seed;
        impulse_vm_params_v11_t params{};
        params.bindings = &bind;
        params.binding_count = 1;
        RunRes rr = run_timed_prog(prog, snap, &params, ws, warmup, iters);
        if (!first_attr) std::printf(",");
        first_attr = false;
        std::printf("{\"scope\":\"edge_morsel\",\"name\":\"%s\",\"idx\":%u,\"st\":%u,\"us\":%.2f,\"count\":%lld}",
                    aname.c_str(), a, rr.status, rr.us, (long long)rr.val);
    }

    // Boundary checks (BOUNDARY_FP_*, BOUNDARY_INT_*)
    std::printf("],\"boundaries\":[");
    bool first_b = true;
    for (uint16_t a = 0; a < dom_attr_cnt; ++a) {
        const char* aname_c = nullptr;
        if (impulse_snapshot_get_domain_attribute_name(snap, canon_dom_id, a, &aname_c) != IMPULSE_OK || !aname_c) continue;
        std::string aname(aname_c);
        if (!starts_with(aname, "BOUNDARY_")) continue;
        impulse_vm_params_v11_t params{};
        RunRes r_cnt = run_timed_prog(build_node_attr_reduce(canon_dom_id, a, subop::kVEC_REDUCE_COUNT_VALID, dom_cnt), snap, &params, ws, 2, 5);
        RunRes r_min = run_timed_prog(build_node_attr_reduce(canon_dom_id, a, subop::kVEC_REDUCE_MIN, dom_cnt), snap, &params, ws, 2, 5);
        RunRes r_max = run_timed_prog(build_node_attr_reduce(canon_dom_id, a, subop::kVEC_REDUCE_MAX, dom_cnt), snap, &params, ws, 2, 5);
        if (!first_b) std::printf(",");
        first_b = false;
        if (r_min.has_fval) {
            auto fmt_f = [](double v) -> std::string {
                if (std::isnan(v)) return "\"NaN\"";
                if (std::isinf(v)) return v < 0 ? "\"-Inf\"" : "\"Inf\"";
                char b[64]; std::snprintf(b, sizeof(b), "%.6g", v); return std::string(b);
            };
            std::printf("{\"name\":\"%s\",\"count_valid\":%lld,\"min\":%s,\"max\":%s}",
                        aname.c_str(), (long long)r_cnt.val, fmt_f(r_min.fval).c_str(), fmt_f(r_max.fval).c_str());
        } else {
            std::printf("{\"name\":\"%s\",\"count_valid\":%lld,\"min\":%lld,\"max\":%lld}",
                        aname.c_str(), (long long)r_cnt.val, (long long)r_min.val, (long long)r_max.val);
        }
    }

    // Multi-lane vs Single-lane sweep (degenerate k=1 w=8/w=64 + batched k=8,16,32,64)
    std::printf("],\"multilane\":[");
    bool first_ml = true;
    auto emit_ml = [&](const char* label, uint32_t k, const RunRes& rr) {
        if (!first_ml) std::printf(",");
        first_ml = false;
        std::printf("{\"label\":\"%s\",\"k\":%u,\"st\":%u,\"us\":%.2f,\"us_per_lane\":%.2f,\"pop_sum\":%lld}",
                    label, k, rr.status, rr.us, (k > 0 ? rr.us / k : rr.us), (long long)rr.val);
    };

    // Baseline single-lane 1-seed 2hop
    {
        auto prog = build_walk_2hop(canon_rel_id, cre.src_domain_id, subop::kWALK_FWD, dom_cnt);
        impulse_vm_param_binding_v11_t bind{};
        bind.param_id = 0; bind.kind = IMPULSE_VM_V11_PARAM_FRONTIER_IDS;
        bind.domain_id = cre.src_domain_id; bind.count = 1; bind.data_ptr = &seeds[0];
        impulse_vm_params_v11_t params{}; params.bindings = &bind; params.binding_count = 1;
        emit_ml("single_1seed_2hop", 1, run_timed_prog(prog, snap, &params, ws, warmup, iters));
    }

    // Degenerate 1-seed multi-lane (auto w=8 and forced w=64) + batched k=8,16,32,64
    const uint32_t ks[] = {1, 8, 16, 32, 64};
    for (uint32_t k : ks) {
        if (k > seeds.size()) continue;
        std::vector<uint64_t> sub_seeds(seeds.begin(), seeds.begin() + k);
        impulse_vm_param_binding_v11_t bind{};
        bind.param_id = 0; bind.kind = IMPULSE_VM_V11_PARAM_VEC_U64;
        bind.domain_id = cre.src_domain_id; bind.count = k; bind.data_ptr = sub_seeds.data();
        impulse_vm_params_v11_t params{}; params.bindings = &bind; params.binding_count = 1;

        char lbl[64];
        std::snprintf(lbl, sizeof(lbl), "ml_k%u_auto_2hop", k);
        emit_ml(lbl, k, run_timed_prog(build_ml_2hop(canon_rel_id, cre.src_domain_id, false, dom_cnt), snap, &params, ws, warmup, iters));

        if (k == 1) {
            emit_ml("ml_k1_force_w64_2hop", 1, run_timed_prog(build_ml_2hop(canon_rel_id, cre.src_domain_id, true, dom_cnt), snap, &params, ws, warmup, iters));
        }

        std::snprintf(lbl, sizeof(lbl), "ml_k%u_morsel_2hop", k);
        emit_ml(lbl, k, run_timed_prog(build_ml_morsel_2hop(canon_rel_id, cre.src_domain_id, false, dom_cnt), snap, &params, ws, warmup, iters));
    }

    // Reference: 64 independent single-lane 2hop runs (exact ground truth for ml_k64 pop_sum + speedup baseline)
    {
        uint32_t k_ref = std::min<uint32_t>(64, static_cast<uint32_t>(seeds.size()));
        auto prog = build_walk_2hop(canon_rel_id, cre.src_domain_id, subop::kWALK_FWD, dom_cnt);
        int64_t total_card = 0;
        double total_us = 0.0;
        for (uint32_t i = 0; i < k_ref; ++i) {
            impulse_vm_param_binding_v11_t bind{};
            bind.param_id = 0; bind.kind = IMPULSE_VM_V11_PARAM_FRONTIER_IDS;
            bind.domain_id = cre.src_domain_id; bind.count = 1; bind.data_ptr = &seeds[i];
            impulse_vm_params_v11_t params{}; params.bindings = &bind; params.binding_count = 1;
            RunRes rr = run_timed_prog(prog, snap, &params, ws, 2, 10);
            total_card += rr.val;
            total_us += rr.us;
        }
        emit_ml("single_loop_64x_2hop", k_ref, RunRes{0, total_us, total_card, 0.0, false});
    }

    std::printf("]}\n");
    impulse_snapshot_close(snap);
    return 0;
}
