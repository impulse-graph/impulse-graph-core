/**
 * @file test_vm_v11.cpp
 * @brief Native C++20 unit & invariant tests for ImpulseVM v0.11.0 (M3).
 *
 * Verifies:
 *   1. Exact 256B / 64B-aligned impulse_vm_state_v11_t C-ABI layout.
 *   2. Canonical XXH3-64 known-answer test vectors.
 *   3. Caller-owned workspace alignment, overflow, and CAS non-reentrancy guards.
 *   4. Zero-allocation hot-path execution (0 heap allocations during execute).
 *   5. Pillar 1 scalar/control/window/spill invariants.
 */
#include "impulse_vm_v11.h"
#include "v11/v11_internal.hpp"
#include "v11/v11_xxh3.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "CHECK FAILED at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        std::abort(); \
    } \
} while (0)

using namespace impulse::vm::v11;

static std::atomic<size_t> g_alloc_count{0};
static std::atomic<bool>   g_track_allocs{false};

void* operator new(size_t sz) {
    if (g_track_allocs.load(std::memory_order_relaxed)) {
        g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(sz == 0 ? 1 : sz);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept {
    std::free(p);
}

void operator delete(void* p, size_t) noexcept {
    std::free(p);
}

static uint64_t enc(uint8_t op, uint8_t flags, uint8_t subop,
                    uint8_t dst, uint8_t s1, uint8_t s2, uint8_t s3, uint32_t imm32) {
    uint64_t b0 = op;
    uint64_t b1 = static_cast<uint64_t>(((flags & 0xFu) << 4) | (subop & 0xFu));
    uint64_t b2 = static_cast<uint64_t>(((dst & 0xFu) << 4) | (s1 & 0xFu));
    uint64_t b3 = static_cast<uint64_t>(((s2 & 0xFu) << 4) | (s3 & 0xFu));
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24) | (static_cast<uint64_t>(imm32) << 32);
}

static std::vector<uint8_t> build_impb(const std::vector<uint64_t>& words) {
    impulse_impb_header_v11_t hdr{};
    hdr.magic = kImpbMagic;
    hdr.isa_version = kImpbVersion;
    hdr.header_flags = 0;
    hdr.max_call_depth = 16;
    hdr.code_word_count = static_cast<uint32_t>(words.size());
    hdr.const_pool_bytes = 0;
    hdr.entry_pc = 0;
    hdr.required_domains = 0;
    hdr.reserved = 0;
    hdr.program_xxh3_64 = V11Xxh3::hash64(
        reinterpret_cast<const uint8_t*>(words.data()),
        words.size() * sizeof(uint64_t));

    std::vector<uint8_t> out(sizeof(hdr) + words.size() * sizeof(uint64_t));
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    std::memcpy(out.data() + sizeof(hdr), words.data(), words.size() * sizeof(uint64_t));
    return out;
}

static void test_xxh3_known_answers() {
    // Official xxHash v0.8.2 XXH3_64bits("") == 0x2d06800538d394c2
    uint64_t h0 = V11Xxh3::hash64(nullptr, 0);
    CHECK(h0 == 0x2d06800538d394c2ULL);

    // Sanity across all length regimes (1..3, 4..8, 9..16, 17..128, 129..240, >240)
    uint8_t buf[1024];
    for (size_t i = 0; i < sizeof(buf); ++i) buf[i] = static_cast<uint8_t>((i * 37u + 13u) & 0xFFu);
    uint64_t h_short = V11Xxh3::hash64_with_seed(buf, 3, 0x494D505630313130ULL);
    uint64_t h_mid   = V11Xxh3::hash64_with_seed(buf, 160, 0x494D505630313130ULL);
    uint64_t h_long  = V11Xxh3::hash64_with_seed(buf, 1024, 0x494D505630313130ULL);
    CHECK(h_short != 0 && h_mid != 0 && h_long != 0);
    CHECK(h_short != h_mid && h_mid != h_long);
}

static void test_workspace_guards_and_zero_malloc() {
    alignas(128) uint8_t raw[1024 * 1024];
    impulse_vm_workspace_v11_t* ws = nullptr;

    // 1. Misaligned scratch_buf rejected
    CHECK(impulse_workspace_init(raw + 16, sizeof(raw) - 16, &ws) == IMPULSE_VM_V11_ERR_INVALID_OPERAND);
    // 2. Undersized scratch_buf rejected
    CHECK(impulse_workspace_init(raw, 512, &ws) == IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW);
    // 3. Valid 128B-aligned init succeeds
    CHECK(impulse_workspace_init(raw, sizeof(raw), &ws) == IMPULSE_VM_V11_OK);
    CHECK(ws != nullptr);

    // Build simple Pillar 1 program: R0 = 10; R1 = 32; R2 = R0 + R1; HALT
    std::vector<uint64_t> words = {
        enc(0x05, 0, 0, 0, 0, 0, 0, 10),
        enc(0x05, 0, 0, 1, 0, 0, 0, 32),
        enc(0x0C, 0, 0, 2, 0, 1, 0, 0),
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),
    };
    auto impb = build_impb(words);

    // 4. Memory planner
    impulse_vm_memory_plan_t plan{};
    CHECK(impulse_vm_plan_memory(impb.data(), impb.size(), nullptr, 1, &plan) == IMPULSE_VM_V11_OK);
    CHECK(plan.min_scratch_bytes > 0 && plan.guaranteed_scratch_bytes >= plan.min_scratch_bytes);

    // 5. Misaligned result_buf rejected
    alignas(64) uint8_t res_buf[1024];
    size_t res_bytes = 0;
    CHECK(impulse_vm_execute_v11(impb.data(), impb.size(), nullptr, nullptr, ws,
                                  res_buf + 8, sizeof(res_buf) - 8, &res_bytes)
           == IMPULSE_VM_V11_ERR_INVALID_OPERAND);

    // 6. Non-reentrancy CAS guard
    ws->in_use.store(1u, std::memory_order_relaxed);
    CHECK(execute_words_internal(words.data(), words.size(), nullptr, *ws, 1000)
           == IMPULSE_VM_V11_ERR_INVALID_OPERAND);
    ws->in_use.store(0u, std::memory_order_relaxed);

    // 7. Zero-malloc hot-path verification (direct words execution after validation)
    ws->reset_execution_state();
    g_alloc_count.store(0, std::memory_order_relaxed);
    g_track_allocs.store(true, std::memory_order_relaxed);
    uint64_t r_before = g_alloc_count.load(std::memory_order_relaxed);
    for (int iter = 0; iter < 10; ++iter) {
        ws->reset_execution_state();
        CHECK(execute_words_internal(words.data(), words.size(), nullptr, *ws, 1000, 0u, true) == IMPULSE_VM_V11_OK);
        CHECK(ws->read_r_raw(2) == 42ULL);
    }
    uint64_t r_after = g_alloc_count.load(std::memory_order_relaxed);
    g_track_allocs.store(false, std::memory_order_relaxed);
    CHECK(r_after == r_before);

    // 8. End-to-end C-ABI execute
    CHECK(impulse_vm_execute_v11(impb.data(), impb.size(), nullptr, nullptr, ws,
                                  res_buf, sizeof(res_buf), &res_bytes) == IMPULSE_VM_V11_OK);
    const impulse_vm_state_v11_t* st = impulse_vm_v11_get_state(ws);
    CHECK(st != nullptr);
    CHECK(st->r_regs[2] == 42ULL);
    CHECK(st->pc == 3u);
    impulse_vm_ictx_v2_t d1 = impulse_vm_v11_compute_ictx_v2(ws);
    impulse_vm_ictx_v2_t d2 = impulse_vm_v11_compute_ictx_v2(ws);
    CHECK(d1.logical_hash == d2.logical_hash && d1.physical_hash == d2.physical_hash);
}

static void test_swe_sweep_p1_regressions() {
    alignas(128) uint8_t raw[2 * 1024 * 1024];
    impulse_vm_workspace_v11_t* ws = nullptr;
    CHECK(impulse_workspace_init(raw, sizeof(raw), &ws) == IMPULSE_VM_V11_OK);

    // Finding #1: impulse_vm_execute_v11 must check in_use CAS BEFORE mutating workspace
    std::vector<uint64_t> simple = {
        enc(0x05, 0, 0, 0, 0, 0, 0, 7),
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),
    };
    auto simple_impb = build_impb(simple);
    ws->state.pc = 99u;
    ws->status = IMPULSE_VM_V11_ERR_TRAP;
    ws->in_use.store(1u, std::memory_order_relaxed);
    alignas(64) uint8_t res_buf[256];
    size_t res_bytes = 0;
    CHECK(impulse_vm_execute_v11(simple_impb.data(), simple_impb.size(), nullptr, nullptr, ws,
                                 res_buf, sizeof(res_buf), &res_bytes) == IMPULSE_VM_V11_ERR_INVALID_OPERAND);
    // Workspace state must NOT have been clobbered!
    CHECK(ws->state.pc == 99u);
    CHECK(ws->status == IMPULSE_VM_V11_ERR_TRAP);
    ws->in_use.store(0u, std::memory_order_relaxed);

    // Finding #2a: .impb hdr.entry_pc must be respected at execution
    std::vector<uint64_t> entry_prog = {
        enc(0x05, 0, 0, 0, 0, 0, 0, 111), // pc=0 (skipped)
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),   // pc=1
        enc(0x05, 0, 0, 0, 0, 0, 0, 222), // pc=2 (entry_pc)
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),   // pc=3
    };
    auto entry_impb = build_impb(entry_prog);
    {
        impulse_impb_header_v11_t hdr{};
        std::memcpy(&hdr, entry_impb.data(), sizeof(hdr));
        hdr.entry_pc = 2u;
        std::memcpy(entry_impb.data(), &hdr, sizeof(hdr));
    }
    CHECK(impulse_vm_execute_v11(entry_impb.data(), entry_impb.size(), nullptr, nullptr, ws,
                                 res_buf, sizeof(res_buf), &res_bytes) == IMPULSE_VM_V11_OK);
    CHECK(impulse_vm_v11_get_state(ws)->r_regs[0] == 222ULL);

    // Finding #2b: .impb hdr.max_call_depth enforced at runtime
    std::vector<uint64_t> call_prog = {
        enc(0x12, 0, 0, 0, 0, 0, 0, 2), // pc=0: CALL +2
        enc(0x00, 0, 0, 0, 0, 0, 0, 0), // pc=1: HALT
        enc(0x13, 0, 0, 0, 0, 0, 0, 0), // pc=2: RET
    };
    auto call_impb = build_impb(call_prog);
    {
        impulse_impb_header_v11_t hdr{};
        std::memcpy(&hdr, call_impb.data(), sizeof(hdr));
        hdr.max_call_depth = 0u;
        std::memcpy(call_impb.data(), &hdr, sizeof(hdr));
    }
    CHECK(impulse_vm_execute_v11(call_impb.data(), call_impb.size(), nullptr, nullptr, ws,
                                 res_buf, sizeof(res_buf), &res_bytes) == IMPULSE_VM_V11_ERR_STACK_OVERFLOW);

    // Finding #9: entry_pc pointing to 0xFF OP_EXTENSION_PAYLOAD rejected by validator
    std::vector<uint64_t> ext_prog = {
        enc(0x05, 0x8, 4, 0, 0, 0, 0, 1), // pc=0: LOAD_CONST.i64.ext
        encode_extension_word(0),          // pc=1: 0xFF
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),   // pc=2: HALT
    };
    auto ext_impb = build_impb(ext_prog);
    {
        impulse_impb_header_v11_t hdr{};
        std::memcpy(&hdr, ext_impb.data(), sizeof(hdr));
        hdr.entry_pc = 1u;
        std::memcpy(ext_impb.data(), &hdr, sizeof(hdr));
    }
    CHECK(impulse_vm_validate_v11(ext_impb.data(), ext_impb.size()).status == IMPULSE_VM_V11_ERR_VALIDATION_FAILED);

    // Finding #6: OP_STABLE_CHECK.vec_l1 converts kVecTagU64 as signed int64_t (matching OCaml/Java)
    ws->reset_execution_state();
    int64_t u64_a[1] = {static_cast<int64_t>(0xFFFFFFFFFFFFFFFFULL)}; // -1 signed
    int64_t u64_b[1] = {0};
    uint64_t vbits[1] = {1ULL};
    V11Vector va{};
    va.elem_tag = kVecTagU64;
    va.length = 1;
    va.i64_data = u64_a;
    va.valid_bits = vbits;
    V11Vector vb{};
    vb.elem_tag = kVecTagU64;
    vb.length = 1;
    vb.i64_data = u64_b;
    vb.valid_bits = vbits;
    ws->v_regs[0] = &va;
    ws->v_regs[1] = &vb;
    double eps = 2.0;
    uint64_t eps_bits = 0;
    std::memcpy(&eps_bits, &eps, 8);
    ws->write_r(2, static_cast<uint8_t>(ScalarType::SCALAR_F64), eps_bits);
    uint64_t stable_prog[2] = {
        enc(0x27, 0, 1, 3, 0, 1, 2, 0), // STABLE_CHECK.vec_l1 R3, V0, V1, R2
        enc(0x00, 0, 0, 0, 0, 0, 0, 0), // HALT
    };
    CHECK(execute_words_internal(stable_prog, 2, nullptr, *ws, 100, 0u, true) == IMPULSE_VM_V11_OK);
    double diff = 0.0;
    std::memcpy(&diff, &ws->state.r_regs[3], 8);
    CHECK(diff == 1.0); // |-1.0 - 0.0| == 1.0, NOT 1.84467e19!
    CHECK(ws->flag_st());

    // Finding #7: load_frontier_ids .accum on kTierAllSpan must recompute tier with is_all_span=false
    ws->reset_execution_state();
    uint64_t dom_cards[1] = {4ULL};
    V11SnapshotView snap{};
    snap.domain_cards = dom_cards;
    snap.domain_count = 1u;
    ws->f_regs[0].reset_empty(0u, 4ULL);
    ws->f_regs[0].tier = kTierAllSpan;
    ws->f_regs[0].cached_card = 4;
    ws->set_f_init(0, true);
    uint64_t f_ids[1] = {1ULL};
    ws->const_pool[0] = {ConstPoolKind::Frontier, 0u, nullptr, f_ids, 1u, {}};
    ws->const_pool_count = 1u;
    uint64_t lcp_prog[2] = {
        enc(0x06, FLAG_ACCUM, 1, 0, 0, 0, 0, 0), // LOAD_CONST_POOL.frontier.accum F0, pool=0, dom=0
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),          // HALT
    };
    CHECK(execute_words_internal(lcp_prog, 2, &snap, *ws, 100, 0u, true) == IMPULSE_VM_V11_OK);
    CHECK(ws->f_regs[0].tier == kTier0Inline);
    CHECK(ws->f_regs[0].node_count == 4u);

    // Finding #10: V11Frontier::init_from_sorted_ids with know_card=false still caches card for Tier0_inline
    uint32_t small_ids[2] = {0u, 2u};
    CHECK(ws->f_regs[1].init_from_sorted_ids(ws->arena, 0u, 100ULL, small_ids, 2u, kGeomP0, false));
    CHECK(ws->f_regs[1].tier == kTier0Inline);
    CHECK(ws->f_regs[1].cached_card == 2);
}

static void test_swe_sweep_p2_regressions() {
    alignas(128) uint8_t raw[2 * 1024 * 1024];
    impulse_vm_workspace_v11_t* ws = nullptr;
    CHECK(impulse_workspace_init(raw, sizeof(raw), &ws) == IMPULSE_VM_V11_OK);

    // P2 Finding #1: OP_SET_CARDINALITY.single_lane OOB in switch-dispatch must NOT fall through
    ws->reset_execution_state();
    uint64_t ml_nodes[1] = {0ULL};
    uint64_t ml_masks[1] = {1ULL};
    CHECK(ws->f_regs[0].init_multi_lane(ws->arena, 0u, 8ULL, 8u, 2u, ml_nodes, ml_masks, 1u, kGeomP0));
    ws->set_f_init(0, true);
    ws->write_r(1, static_cast<uint8_t>(ScalarType::SCALAR_U64), 999ULL);
    uint64_t card_oob[2] = {
        enc(0x24, 0, 4, 1, 0, 0, 0, 8u), // SET_CARDINALITY.single_lane R1, F0, lane=8 (OOB for width=8)
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),
    };
    CHECK(execute_words_internal(card_oob, 2, nullptr, *ws, 100, 0u, true) == IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS);
    CHECK(ws->state.pc == 0u);
    CHECK(ws->read_r_raw(1) == 999ULL);

    // P2 Finding #2: OP_SET_CONVERT.f2v_ids must memoize cached_card on F[s1]
    ws->reset_execution_state();
    uint32_t ids4[4] = {0u, 1u, 2u, 3u};
    CHECK(ws->f_regs[0].init_from_sorted_ids(ws->arena, 0u, 100ULL, ids4, 4u, kGeomP1, false));
    CHECK(ws->f_regs[0].cached_card == -1);
    ws->set_f_init(0, true);
    uint64_t f2v_prog[2] = {
        enc(0x26, 0, 0, 0, 0, 0, 0, 0), // SET_CONVERT.f2v_ids V0, F0
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),
    };
    CHECK(execute_words_internal(f2v_prog, 2, nullptr, *ws, 100, 0u, true) == IMPULSE_VM_V11_OK);
    CHECK(ws->f_regs[0].cached_card == 4);

    // P2 Finding #3: OP_STABLE_CHECK.frontier_eq with kTierAllSpan and mismatched domain_card
    ws->reset_execution_state();
    ws->f_regs[0].init_all_span(0u, 5ULL); // {0,1,2,3,4}
    ws->set_f_init(0, true);
    uint32_t ids5[5] = {0u, 1u, 2u, 3u, 9u};
    CHECK(ws->f_regs[1].init_from_sorted_ids(ws->arena, 0u, 10ULL, ids5, 5u, kGeomP0, true));
    ws->set_f_init(1, true);
    uint64_t st_prog[2] = {
        enc(0x27, 0, 0, 0, 0, 1, 0, 0), // STABLE_CHECK.frontier_eq F0, F1
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),
    };
    CHECK(execute_words_internal(st_prog, 2, nullptr, *ws, 100, 0u, true) == IMPULSE_VM_V11_OK);
    CHECK(!ws->flag_st());
    CHECK(!ws->flag_zf());
}

static void test_swe_sweep_p3_regressions() {
    alignas(128) uint8_t raw[2 * 1024 * 1024];
    impulse_vm_workspace_v11_t* ws = nullptr;
    CHECK(impulse_workspace_init(raw, sizeof(raw), &ws) == IMPULSE_VM_V11_OK);

    uint64_t dom_cards[1] = {4ULL};
    uint32_t row_off[5] = {0u, 1u, 2u, 2u, 2u};
    uint64_t col_tgt[2] = {1ULL, 2ULL};
    V11RelView rel{};
    rel.src_dom = 0u;
    rel.tgt_dom = 0u;
    rel.row_offsets = row_off;
    rel.row_off_len = 5u;
    rel.col_targets = col_tgt;
    rel.col_tgt_len = 2u;
    rel.has_csr = true;
    V11SnapshotView snap{};
    snap.domain_cards = dom_cards;
    snap.domain_count = 1u;
    snap.relations = &rel;
    snap.relation_count = 1u;

    // P3 Finding #1 (P0): OP_SAMPLE_NEIGHBORS.vec_wr OOB node ID in switch-dispatch must trap at pc=0 without overwriting V[dst]
    ws->reset_execution_state();
    int64_t seed_ids[2] = {0, 99}; // 99 >= dcard(4) -> OOB
    uint64_t vbits_all[1] = {3ULL};
    V11Vector v_seeds{};
    v_seeds.elem_tag = kVecTagU64;
    v_seeds.length = 2u;
    v_seeds.i64_data = seed_ids;
    v_seeds.valid_bits = vbits_all;
    ws->v_regs[0] = &v_seeds;
    ws->v_regs[1] = &v_seeds; // sentinel
    ws->write_r(2, static_cast<uint8_t>(ScalarType::SCALAR_U64), 42ULL);
    uint64_t samp_oob[2] = {
        enc(0x39, 0, 1, 1, 0, 2, 0, (1u << 16) | 0u), // SAMPLE_NEIGHBORS.vec_wr V1, V0, R2, rel=0, k=1
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),
    };
    CHECK(execute_words_internal(samp_oob, 2, &snap, *ws, 100, 0u, true) == IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS);
    CHECK(ws->state.pc == 0u);
    CHECK(ws->v_regs[1] == &v_seeds);

    // P3 Finding #4 (P1): OP_MORSEL_COMMIT.scatter_add targeting valid zero-length V0
    ws->reset_execution_state();
    V11Vector v_empty_f64{};
    v_empty_f64.elem_tag = kVecTagF64;
    v_empty_f64.length = 0u;
    ws->v_regs[0] = &v_empty_f64;
    ws->f_regs[0].reset_empty(0u, 4ULL);
    ws->set_f_init(0, true);
    double one = 1.0;
    uint64_t one_bits = 0;
    std::memcpy(&one_bits, &one, 8);
    ws->write_r(1, static_cast<uint8_t>(ScalarType::SCALAR_F64), one_bits);
    uint64_t morsel_empty_vec[4] = {
        enc(0x36, 0, 0, 0, 0, 0, 0, (2u << 16)),           // FILTER_MORSEL F0, end_off=2
        enc(0x37, FLAG_SCALAR_RHS, 2, 0, 8, 1, 0, 0xFFFF), // MORSEL_COMMIT.scatter_add.s V0, M0(V8), R1
        enc(0x38, 0, 0, 0, 0, 0, 0, 0),                    // MORSEL_END
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),                    // HALT
    };
    // With 0 items -> OK
    CHECK(execute_words_internal(morsel_empty_vec, 4, &snap, *ws, 100, 0u, true) == IMPULSE_VM_V11_OK);
    // With 1 item -> ERR_OUT_OF_BOUNDS (not ERR_TYPE_MISMATCH)
    ws->v_regs[0] = &v_empty_f64;
    uint32_t id0[1] = {0u};
    CHECK(ws->f_regs[0].init_from_sorted_ids(ws->arena, 0u, 4ULL, id0, 1u, kGeomP0, true));
    CHECK(execute_words_internal(morsel_empty_vec, 4, &snap, *ws, 100, 0u, true) == IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS);

    // P3 Finding #2 (P0): OP_MORSEL_COMMIT with dst >= 8 on unvalidated bytecode traps ERR_VALIDATION_FAILED
    ws->reset_execution_state();
    ws->f_regs[0].reset_empty(0u, 4ULL);
    ws->set_f_init(0, true);
    uint64_t morsel_bad_dst[4] = {
        enc(0x36, 0, 0, 0, 0, 0, 0, (2u << 16)),
        enc(0x37, 0, 8, 9, 0, 0, 0, 0), // dst = 9 >= 8
        enc(0x38, 0, 0, 0, 0, 0, 0, 0),
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),
    };
    CHECK(execute_words_internal(morsel_bad_dst, 4, &snap, *ws, 100, 0u, true) == IMPULSE_VM_V11_ERR_VALIDATION_FAILED);

    // P3 Finding #14 (P2): OP_WALK_FILTERED .is_not_null on kVecTagLaneState edge attr returns empty
    ws->reset_execution_state();
    V11Vector ls_attr{};
    ls_attr.elem_tag = kVecTagLaneState;
    ls_attr.length = 2u;
    uint32_t ea_ids[1] = {0u};
    V11Vector* ea_ptrs[1] = {&ls_attr};
    rel.edge_attr_ids = ea_ids;
    rel.edge_attrs = ea_ptrs;
    rel.edge_attr_count = 1u;
    CHECK(ws->f_regs[0].init_from_sorted_ids(ws->arena, 0u, 4ULL, id0, 1u, kGeomP0, true));
    ws->set_f_init(0, true);
    uint64_t wf_ls[2] = {
        enc(0x31, 0, 6, 1, 0, 0, 0, (0u << 16) | 0u), // WALK_FILTERED.is_not_null F1, F0, rel=0, attr=0
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),
    };
    CHECK(execute_words_internal(wf_ls, 2, &snap, *ws, 100, 0u, true) == IMPULSE_VM_V11_OK);
    CHECK(ws->flag_zf());
}

static void test_swe_sweep_p4_regressions() {
    alignas(128) uint8_t raw[2 * 1024 * 1024];
    impulse_vm_workspace_v11_t* ws = nullptr;
    CHECK(impulse_workspace_init(raw, sizeof(raw), &ws) == IMPULSE_VM_V11_OK);

    // P4 Finding #2: OP_VEC_3VL_OP invalid subop 12..15 must return ERR_INVALID_OPCODE
    ws->reset_execution_state();
    uint64_t bvalid[1] = {1ULL};
    uint64_t bdata[1] = {1ULL};
    V11Vector vb{};
    vb.elem_tag = kVecTagBool3vl;
    vb.length = 1u;
    vb.valid_bits = bvalid;
    vb.bool_data_bits = bdata;
    ws->v_regs[0] = &vb;
    uint64_t bad_3vl[2] = {
        enc(0x46, 0, 12, 1, 0, 0, 0, 0), // VEC_3VL_OP subop=12 (invalid)
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),
    };
    CHECK(execute_words_internal(bad_3vl, 2, nullptr, *ws, 100, 0u, true) == IMPULSE_VM_V11_ERR_INVALID_OPCODE);

    // P4 Finding #7: OP_VEC_INT_BITWISE invalid subop 14..15 must return ERR_INVALID_OPCODE
    ws->reset_execution_state();
    int64_t ivals[1] = {7};
    V11Vector vi{};
    vi.elem_tag = kVecTagI64;
    vi.length = 1u;
    vi.i64_data = ivals;
    vi.valid_bits = bvalid;
    ws->v_regs[0] = &vi;
    uint64_t bad_bw[2] = {
        enc(0x4C, 0, 14, 1, 0, 0, 0, 0), // VEC_INT_BITWISE subop=14 (invalid)
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),
    };
    CHECK(execute_words_internal(bad_bw, 2, nullptr, *ws, 100, 0u, true) == IMPULSE_VM_V11_ERR_INVALID_OPCODE);

    // P4 Finding #9: OP_NODE_FILTER .ne on F64 column with valid-bit-set NaN must drop NaN (ZF=true)
    ws->reset_execution_state();
    double fvals[1] = {std::numeric_limits<double>::quiet_NaN()};
    V11Vector vf{};
    vf.elem_tag = kVecTagF64;
    vf.length = 1u;
    vf.f64_data = fvals;
    vf.valid_bits = bvalid;
    uint64_t dom_cards[1] = {1ULL};
    V11DomAttrEntry na[1] = {{0u, 0u, &vf}};
    V11SnapshotView snap{};
    snap.domain_cards = dom_cards;
    snap.domain_count = 1u;
    snap.node_attrs = na;
    snap.node_attr_count = 1u;
    uint32_t id0[1] = {0u};
    CHECK(ws->f_regs[0].init_from_sorted_ids(ws->arena, 0u, 1ULL, id0, 1u, kGeomP0, true));
    ws->set_f_init(0, true);
    double rhs = 1.0;
    uint64_t rhs_bits = 0;
    std::memcpy(&rhs_bits, &rhs, 8);
    ws->write_r(2, static_cast<uint8_t>(ScalarType::SCALAR_F64), rhs_bits);
    uint64_t nf_nan[2] = {
        enc(0x44, FLAG_SCALAR_RHS, 1, 1, 0, 2, 0, 0), // NODE_FILTER.ne.s F1, F0, R2, dom=0, attr=0
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),
    };
    CHECK(execute_words_internal(nf_nan, 2, &snap, *ws, 100, 0u, true) == IMPULSE_VM_V11_OK);
    CHECK(ws->flag_zf());
}

static void test_swe_sweep_p5_regressions() {
    alignas(128) uint8_t raw[4 * 1024 * 1024];
    impulse_vm_workspace_v11_t* ws = nullptr;
    CHECK(impulse_workspace_init(raw, sizeof(raw), &ws) == IMPULSE_VM_V11_OK);

    uint64_t dom_cards[1] = {2ULL};
    V11SnapshotView snap{};
    snap.domain_cards = dom_cards;
    snap.domain_count = 1u;

    // P5 Finding #1 (P0) & #2 (P1): LANE_STATE_INIT k=256 -> to_lane_frontier must clamp active_lane_count <= 64 (not wrap to 0)
    // and k=65 -> to_lane_frontier -> LANE_REDUCE_ATTR.count must NOT shift 1ULL << 64 (UBSan clean).
    ws->reset_execution_state();
    uint64_t p5_clamp_prog[4] = {
        enc(0x6B, 0, 0, 0, 0, 0, 0, (256u << 16) | 0u), // LANE_STATE_INIT.zero_f32 V0, dom=0, k=256
        enc(0x6D, 0, 3, 0, 0, 0, 0, 0),                 // LANE_STATE_OP.to_lane_frontier F0, V0
        enc(0x67, 0, 3, 1, 0, 0, 0, 0),                 // LANE_REDUCE_ATTR.count V1, F0
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),
    };
    CHECK(execute_words_internal(p5_clamp_prog, 4, &snap, *ws, 100, 0u, true) == IMPULSE_VM_V11_OK);
    CHECK(ws->f_regs[0].active_lane_count == 64u);
    CHECK(ws->v_regs[1] != nullptr && ws->v_regs[1]->length == 64u);

    ws->reset_execution_state();
    uint64_t p5_shift_prog[4] = {
        enc(0x6B, 0, 0, 0, 0, 0, 0, (65u << 16) | 0u),  // LANE_STATE_INIT.zero_f32 V0, dom=0, k=65
        enc(0x6D, 0, 3, 0, 0, 0, 0, 0),                 // LANE_STATE_OP.to_lane_frontier F0, V0
        enc(0x67, 0, 3, 1, 0, 0, 0, 0),                 // LANE_REDUCE_ATTR.count V1, F0 (would UB-shift 1ULL<<64)
        enc(0x00, 0, 0, 0, 0, 0, 0, 0),
    };
    CHECK(execute_words_internal(p5_shift_prog, 4, &snap, *ws, 100, 0u, true) == IMPULSE_VM_V11_OK);
    CHECK(ws->f_regs[0].active_lane_count == 64u);
}

int main() {
    test_xxh3_known_answers();
    test_workspace_guards_and_zero_malloc();
    test_swe_sweep_p1_regressions();
    test_swe_sweep_p2_regressions();
    test_swe_sweep_p3_regressions();
    test_swe_sweep_p4_regressions();
    test_swe_sweep_p5_regressions();
    std::printf("test_vm_v11: ALL PASSED\n");
    return 0;
}
