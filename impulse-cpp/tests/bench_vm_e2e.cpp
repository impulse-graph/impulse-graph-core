/**
 * @file bench_vm_e2e.cpp
 * @brief Empirical C++20 v0.11 E2E benchmark (2-hop, fixpoint BFS/ReBAC, CC) on real .imps snapshots.
 */
#include "vm/impulse_bytecode_emitter.hpp"
#include "impulse_vm.h"
#include "impulse_graph.h"

#undef OP_HALT
#undef OP_SET_CARDINALITY

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static double run_timed(
    const std::vector<uint8_t>& impb,
    const impulse_snapshot_t* snap,
    const impulse_vm_params_t* params,
    impulse_vm_workspace_t* ws,
    int warmup,
    int iters,
    int64_t* out_val
) {
    for (int i = 0; i < warmup; ++i) {
        impulse_vm_status_t st = impulse_vm_execute(
            impb.data(), impb.size(), snap, params, ws, nullptr, 0, nullptr);
        if (st != IMPULSE_VM_OK) {
            const auto* stf = impulse_vm_get_state(ws);
            std::fprintf(stderr, "VM error during warmup: %u at pc=%u\n", st, stf ? stf->pc : 9999u);
            std::exit(2);
        }
    }
    std::vector<double> us;
    us.reserve(static_cast<size_t>(iters));
    for (int i = 0; i < iters; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        impulse_vm_status_t st = impulse_vm_execute(
            impb.data(), impb.size(), snap, params, ws, nullptr, 0, nullptr);
        auto t1 = std::chrono::steady_clock::now();
        if (st != IMPULSE_VM_OK) {
            std::fprintf(stderr, "VM error during iter %d: %u\n", i, st);
            std::exit(2);
        }
        us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    impulse_vm_output_view_t ov{};
    if (impulse_vm_get_output(ws, 0, &ov) == 1 && ov.length > 0 && ov.i64_data) {
        *out_val = ov.i64_data[0];
    }
    std::sort(us.begin(), us.end());
    return us[us.size() / 2];
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <snapshot.imps> [seed_node=0] [warmup=20] [iters=100]\n", argv[0]);
        return 1;
    }
    const char* path = argv[1];
    uint64_t seed = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 0ULL;
    int warmup = (argc > 3) ? std::atoi(argv[3]) : 20;
    int iters = (argc > 4) ? std::atoi(argv[4]) : 100;

    impulse_status_t open_st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(path, &open_st);
    if (!snap || open_st != IMPULSE_OK) {
        std::fprintf(stderr, "Failed to open snapshot %s (status=%d)\n", path, open_st);
        return 1;
    }
    if (impulse_vm_prepare_snapshot(snap) != IMPULSE_VM_OK) {
        std::fprintf(stderr, "Failed to prepare v11 snapshot view\n");
        return 1;
    }

    // Allocate 64MB 128-byte aligned caller workspace (reused across all iterations)
    const size_t ws_bytes = 64u * 1024u * 1024u;
    std::vector<uint8_t> raw_buf(ws_bytes + 128u);
    void* raw = reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(raw_buf.data()) + 127u) & ~uintptr_t(127u));
    impulse_vm_workspace_t* ws = nullptr;
    if (impulse_workspace_init(raw, ws_bytes, &ws) != IMPULSE_VM_OK) return 1;

    using namespace impulse::compiler;
    using namespace impulse::vm;

    // 1. 2-Hop Walk + Cardinality
    VmProgramBuilder b2;
    b2.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_FRONTIER, 0, 0, 0, 0, 0, 0, 0);
    b2.emit_u16(Opcode::OP_WALK, subop::kWALK_FWD, 0, 1, 0, 0, 0, 0, 0);
    b2.emit_u16(Opcode::OP_WALK, subop::kWALK_FWD, 0, 0, 1, 0, 0, 0, 0);
    b2.emit(Opcode::OP_SET_CARDINALITY, subop::kSET_CARDINALITY_CARD_1BIT, 0, 0, 0, 0, 0, 0);
    b2.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, 0, 0, 0, 0, 0, 0, 0);
    b2.emit(Opcode::OP_HALT, 0, 0, 0, 0, 0, 0, 0);
    auto impb_2hop = b2.build_impb(0, 0, 1);

    // 2. Fixpoint Kleene* Reachability (BFS / ReBAC) + Cardinality
    VmProgramBuilder bf;
    bf.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_FRONTIER, 0, 0, 0, 0, 0, 0, 0);
    bf.emit_u16(Opcode::OP_WALK, subop::kWALK_FWD_FIXPOINT_STAR, 0, 1, 0, 0, 0, 0, 0);
    bf.emit(Opcode::OP_SET_CARDINALITY, subop::kSET_CARDINALITY_CARD_1BIT, 0, 0, 1, 0, 0, 0);
    bf.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, 0, 0, 0, 0, 0, 0, 0);
    bf.emit(Opcode::OP_HALT, 0, 0, 0, 0, 0, 0, 0);
    auto impb_bfs = bf.build_impb(0, 0, 1);

    // 3. Morsel 2-Hop (WALK 1-hop + WALK_MORSEL.fwd + MORSEL_COMMIT.yield_frontier + CARD)
    VmProgramBuilder bm;
    bm.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_FRONTIER, 0, 0, 0, 0, 0, 0, 0);
    bm.emit_u16(Opcode::OP_WALK, subop::kWALK_FWD, 0, 1, 0, 0, 0, 0, 0);
    bm.emit_u16(Opcode::OP_WALK_MORSEL, subop::kWALK_MORSEL_FWD, 0, 0, 1, 0, 0, 0, 2);
    bm.emit_u16(Opcode::OP_MORSEL_COMMIT, subop::kMORSEL_COMMIT_YIELD_FRONTIER, 0, 2, 9, 0, 0, 0, 0);
    bm.emit_u16(Opcode::OP_MORSEL_END, 0, 0, 0, 0, 0, 0, 0, 2);
    bm.emit(Opcode::OP_SET_CARDINALITY, subop::kSET_CARDINALITY_CARD_1BIT, 0, 0, 2, 0, 0, 0);
    bm.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, 0, 0, 0, 0, 0, 0, 0);
    bm.emit(Opcode::OP_HALT, 0, 0, 0, 0, 0, 0, 0);
    auto impb_morsel_2hop = bm.build_impb(0, 0, 1);

    // 4. Morsel Edge Count (WALK 1-hop + WALK_MORSEL.fwd + MORSEL_COMMIT.count_add_r)
    VmProgramBuilder bmc;
    bmc.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_FRONTIER, 0, 0, 0, 0, 0, 0, 0);
    bmc.emit_u16(Opcode::OP_WALK, subop::kWALK_FWD, 0, 1, 0, 0, 0, 0, 0);
    bmc.emit(Opcode::OP_LOAD_CONST, subop::kLOAD_CONST_I32, 0, 0, 0, 0, 0, 0);
    bmc.emit_u16(Opcode::OP_WALK_MORSEL, subop::kWALK_MORSEL_FWD, 0, 0, 1, 0, 0, 0, 2);
    bmc.emit_u16(Opcode::OP_MORSEL_COMMIT, subop::kMORSEL_COMMIT_COUNT_ADD_R, 0, 0, 0, 0, 0, 0, 0);
    bmc.emit_u16(Opcode::OP_MORSEL_END, 0, 0, 0, 0, 0, 0, 0, 2);
    bmc.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, 0, 0, 0, 0, 0, 0, 0);
    bmc.emit(Opcode::OP_HALT, 0, 0, 0, 0, 0, 0, 0);
    auto impb_morsel_cnt = bmc.build_impb(0, 0, 1);

    impulse_vm_param_binding_t bind{};
    bind.param_id = 0u;
    bind.kind = IMPULSE_VM_PARAM_FRONTIER_IDS;
    bind.domain_id = 0u;
    bind.count = 1u;
    bind.data_ptr = &seed;
    impulse_vm_params_t params{};
    params.gas_limit = 0ULL;
    params.bindings = &bind;
    params.binding_count = 1u;

    int64_t card_2hop = -1, card_bfs = -1, card_m2 = -1, cnt_mc = -1;
    double us_2hop = run_timed(impb_2hop, snap, &params, ws, warmup, iters, &card_2hop);
    double us_bfs  = run_timed(impb_bfs,  snap, &params, ws, warmup, iters, &card_bfs);
    double us_m2   = run_timed(impb_morsel_2hop, snap, &params, ws, warmup, iters, &card_m2);
    double us_mc   = run_timed(impb_morsel_cnt,  snap, &params, ws, warmup, iters, &cnt_mc);

    std::printf("{\"engine\":\"cpp-v11\",\"snapshot\":\"%s\",\"2hop_us\":%.2f,\"2hop_card\":%lld,\"bfs_us\":%.2f,\"bfs_card\":%lld,\"morsel_2hop_us\":%.2f,\"morsel_2hop_card\":%lld,\"morsel_cnt_us\":%.2f,\"morsel_cnt\":%lld}\n",
                path, us_2hop, static_cast<long long>(card_2hop), us_bfs, static_cast<long long>(card_bfs),
                us_m2, static_cast<long long>(card_m2), us_mc, static_cast<long long>(cnt_mc));

    impulse_snapshot_close(snap);
    return 0;
}
