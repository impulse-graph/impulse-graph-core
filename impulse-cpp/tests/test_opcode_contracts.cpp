/**
 * @file test_opcode_contracts.cpp
 * @brief Formal Opcode Contract Test Suite for ImpulseVM.
 *
 * Automatically verifies:
 *  - Pass 1: Disallowed register type rejection (IMPULSE_VM_ERR_INVALID_REGISTER).
 *  - Pass 2: Allowed register type acceptance and output register type conformity.
 *  - Pass 3: Out-of-bounds register index validation (reg >= 64).
 *  - Pass 4: In-place register aliasing safety (dst == src, dst == aux1, dst == aux2, src == aux1).
 *  - Pass 5: Null context / snapshot safety (IMPULSE_VM_ERR_NULL_SNAPSHOT).
 */

#include "impulse_opcode_contracts.hpp"
#include "impulse_vm.h"
#include "impulse_graph.h"
#include "../src/impulse_vm_internal.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cmath>
#include <csignal>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <new>

static thread_local bool g_track_heap_allocs = false;
static thread_local size_t g_heap_alloc_count = 0;

void* operator new(std::size_t sz) {
    if (g_track_heap_allocs) {
        ++g_heap_alloc_count;
    }
    void* p = std::malloc(sz == 0 ? 1 : sz);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t sz) {
    if (g_track_heap_allocs) {
        ++g_heap_alloc_count;
    }
    void* p = std::malloc(sz == 0 ? 1 : sz);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept {
    std::free(p);
}

void operator delete[](void* p) noexcept {
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}

using namespace impulse::vm;

namespace {

// ============================================================================
// Crash Guard (SIGSEGV / SIGBUS / SIGILL / SIGFPE recovery for audit)
// ============================================================================
#ifndef _WIN32
static sigjmp_buf g_crash_env;
static volatile sig_atomic_t g_crash_signal = 0;

static void crash_handler(int sig) {
    g_crash_signal = sig;
    siglongjmp(g_crash_env, 1);
}

struct ScopedCrashHandler {
    struct sigaction old_segv;
    struct sigaction old_bus;
    struct sigaction old_ill;
    struct sigaction old_fpe;

    ScopedCrashHandler() {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = crash_handler;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, &old_segv);
        sigaction(SIGBUS, &sa, &old_bus);
        sigaction(SIGILL, &sa, &old_ill);
        sigaction(SIGFPE, &sa, &old_fpe);
    }

    ~ScopedCrashHandler() {
        sigaction(SIGSEGV, &old_segv, nullptr);
        sigaction(SIGBUS, &old_bus, nullptr);
        sigaction(SIGILL, &old_ill, nullptr);
        sigaction(SIGFPE, &sa_noop, nullptr);
        sigaction(SIGFPE, &old_fpe, nullptr);
    }
private:
    struct sigaction sa_noop{};
};

#define EXECUTE_WITH_CRASH_GUARD(code_block, crashed_flag) do { \
    crashed_flag = 0; \
    if (sigsetjmp(g_crash_env, 1) == 0) { \
        code_block; \
    } else { \
        crashed_flag = (g_crash_signal != 0) ? g_crash_signal : SIGSEGV; \
        sigset_t unblock_mask; \
        sigemptyset(&unblock_mask); \
        sigaddset(&unblock_mask, SIGSEGV); \
        sigaddset(&unblock_mask, SIGBUS); \
        sigaddset(&unblock_mask, SIGILL); \
        sigaddset(&unblock_mask, SIGFPE); \
        sigprocmask(SIG_UNBLOCK, &unblock_mask, nullptr); \
    } \
} while(0)
#else
struct ScopedCrashHandler {};
#define EXECUTE_WITH_CRASH_GUARD(code_block, crashed_flag) do { \
    crashed_flag = 0; \
    code_block; \
} while(0)
#endif

struct SafeExecResult {
    impulse_vm_status_t status;
    int crash_signal;
};

static SafeExecResult safe_execute(const impulse_instruction_t* prog, size_t count,
                                   impulse_vm_state_t* st, uint64_t input_param) {
    SafeExecResult res{IMPULSE_VM_OK, 0};
    EXECUTE_WITH_CRASH_GUARD({
        res.status = impulse_vm_execute(prog, count, st, input_param);
    }, res.crash_signal);
    return res;
}

// ============================================================================
// Failure Collector
// ============================================================================
struct ContractViolation {
    uint8_t opcode;
    std::string mnemonic;
    std::string variant;
    std::string pass_name;
    std::string details;
};

static std::vector<ContractViolation> g_violations;
static size_t g_total_assertions = 0;
static size_t g_passed_assertions = 0;

static void record_violation(const ImpulseOpcodeVariantContract& c,
                             const std::string& pass,
                             const std::string& details) {
    g_violations.push_back({
        c.opcode,
        c.mnemonic ? c.mnemonic : "UNKNOWN",
        c.variant_name ? c.variant_name : "default",
        pass,
        details
    });
}

static void record_check(bool passed,
                         const ImpulseOpcodeVariantContract& c,
                         const std::string& pass,
                         const std::string& details) {
    g_total_assertions++;
    if (passed) {
        g_passed_assertions++;
    } else {
        record_violation(c, pass, details);
    }
}

// ============================================================================
// Snapshot Test Harness Setup
// ============================================================================
#ifdef _WIN32
static const char* TEST_SNAP_PATH = "test_impulse_opcode_contracts.imps";
#else
static const char* TEST_SNAP_PATH = "/tmp/test_impulse_opcode_contracts.imps";
#endif

static impulse_snapshot_t* create_test_snapshot() {
    impulse_status_t st = IMPULSE_OK;
    impulse_writer_t* w = impulse_writer_create(TEST_SNAP_PATH, 0);
    if (!w) {
        std::cerr << "Failed to create snapshot writer at " << TEST_SNAP_PATH << std::endl;
        return nullptr;
    }

    // Domain 0: User (16 nodes)
    impulse_writer_add_domain(w, 0, IMPULSE_KEY_TYPE_INT32, "User");

    // Relation 0: KNOWS (16 nodes, 32 edges)
    std::vector<uint32_t> row_offsets(17, 0);
    std::vector<uint32_t> col_indices;
    for (uint32_t u = 0; u < 16; ++u) {
        col_indices.push_back((u + 1) % 16);
        col_indices.push_back((u + 3) % 16);
        row_offsets[u + 1] = static_cast<uint32_t>(col_indices.size());
    }
    impulse_writer_add_relation(w, 0, 0, IMPULSE_ENC_RAW, 16, col_indices.size(), 0,
                               row_offsets.data(), row_offsets.size() * sizeof(uint32_t),
                               col_indices.data(), col_indices.size() * sizeof(uint32_t));

    // Float Attribute 0: "weight" (16 floats)
    std::vector<float> weights(16);
    for (size_t i = 0; i < 16; ++i) weights[i] = static_cast<float>(i + 1) * 0.5f;
    impulse_writer_add_attribute(w, 0, "weight", 6, 16, weights.data(), weights.size() * sizeof(float), nullptr, 0);

    // Float Attribute 1: "emb" (16 nodes * 4 dimensions)
    std::vector<float> embs(16 * 4, 0.25f);
    impulse_writer_add_attribute(w, 0, "emb", 6, 16, embs.data(), embs.size() * sizeof(float), nullptr, 0);

    // Secondary Index: Vamana ANN
    std::vector<uint8_t> idx_payload(640, 0);
    impulse_writer_add_index(w, 0, 0, 0, 8, "vamana_emb", idx_payload.data(), idx_payload.size(), 0);

    st = impulse_writer_finalize(w);
    impulse_writer_destroy(w);
    if (st != IMPULSE_OK) {
        std::cerr << "Failed to finalize test snapshot!" << std::endl;
        return nullptr;
    }

    impulse_snapshot_t* snap = impulse_snapshot_open(TEST_SNAP_PATH, &st);
    if (!snap || st != IMPULSE_OK) {
        std::cerr << "Failed to open test snapshot!" << std::endl;
        return nullptr;
    }
    return snap;
}

// ============================================================================
// Register Population Helpers
// ============================================================================
static void populate_register(impulse_vm_state_t* st, uint16_t reg, impulse_register_type_t type) {
    if (reg >= 64) return;
    st->register_types[reg] = static_cast<uint8_t>(type);
    auto* ctx = st->query_context;

    switch (type) {
        case TYPE_NULL:
            st->registers[reg] = 0;
            break;
        case TYPE_INT64:
            st->registers[reg] = 2;
            break;
        case TYPE_FLOAT: {
            float val = 3.14159f;
            uint64_t u = 0;
            std::memcpy(&u, &val, sizeof(float));
            st->registers[reg] = u;
            break;
        }
        case TYPE_BITSET_HANDLE: {
            if (ctx) {
                int h = impulse_vm_context_acquire_bitset(ctx);
                if (h >= 0) {
                    impulse_vm_context_bitset_add(ctx, h, 0);
                    impulse_vm_context_bitset_add(ctx, h, 1);
                    impulse_vm_context_bitset_add(ctx, h, 2);
                    st->registers[reg] = static_cast<uint64_t>(h);
                } else {
                    st->registers[reg] = 0;
                }
            } else {
                st->registers[reg] = 0;
            }
            break;
        }
        case TYPE_NODE_ID:
            st->registers[reg] = 1;
            break;
        case TYPE_NODE_VECTOR: {
            if (ctx) {
                int h = impulse_vm_context_acquire_node_vector(ctx);
                if (h >= 0) {
                    ctx->node_vectors[h] = {0, 1, 2, 3};
                    ctx->node_vector_lengths[h] = 4;
                    st->registers[reg] = static_cast<uint64_t>(h);
                } else {
                    st->registers[reg] = 0;
                }
            } else {
                st->registers[reg] = 0;
            }
            break;
        }
        case TYPE_FLOAT_VECTOR: {
            if (ctx) {
                int h = impulse_vm_context_acquire_float_vector(ctx);
                if (h >= 0) {
                    for (size_t i = 0; i < 4; ++i) {
                        impulse_vm_context_float_vector_set(ctx, h, i, 1.5f * static_cast<float>(i + 1));
                    }
                    st->registers[reg] = static_cast<uint64_t>(h);
                } else {
                    st->registers[reg] = 0;
                }
            } else {
                st->registers[reg] = 0;
            }
            break;
        }
        case TYPE_DOUBLE_VECTOR: {
            if (ctx) {
                int h = impulse_vm_context_acquire_double_vector(ctx);
                if (h >= 0) {
                    for (size_t i = 0; i < 4; ++i) {
                        ctx->double_vectors[h][i] = 2.5 * static_cast<double>(i + 1);
                    }
                    ctx->double_vector_lengths[h] = 4;
                    st->registers[reg] = static_cast<uint64_t>(h);
                } else {
                    st->registers[reg] = 0;
                }
            } else {
                st->registers[reg] = 0;
            }
            break;
        }
        case TYPE_UINT64_VECTOR: {
            if (ctx) {
                int h = impulse_vm_context_acquire_node_vector(ctx);
                if (h >= 0) {
                    ctx->node_vectors[h] = {10, 20, 30, 40};
                    ctx->node_vector_lengths[h] = 4;
                    st->registers[reg] = static_cast<uint64_t>(h);
                } else {
                    st->registers[reg] = 0;
                }
            } else {
                st->registers[reg] = 0;
            }
            break;
        }
        case TYPE_RELATION_ID:
        case TYPE_CSR_SPAN:
            st->registers[reg] = 0;
            break;
        case TYPE_BOOLEAN:
            st->registers[reg] = 1;
            break;
        case TYPE_STRING_VECTOR: {
            if (ctx) {
                int h = impulse_vm_context_acquire_string_vector(ctx);
                st->registers[reg] = (h >= 0) ? static_cast<uint64_t>(h) : 0;
            } else {
                st->registers[reg] = 0;
            }
            break;
        }
        case TYPE_VALUE_MAP: {
            if (ctx) {
                int h = impulse_vm_context_acquire_value_map(ctx);
                st->registers[reg] = (h >= 0) ? static_cast<uint64_t>(h) : 0;
            } else {
                st->registers[reg] = 0;
            }
            break;
        }
        case TYPE_FRONTIER_STATE:
            st->registers[reg] = 0;
            break;
        case TYPE_LANE_FRONTIER: {
            if (ctx) {
                int h = acquire_lane_frontier(ctx, 16, 4);
                if (h >= 0) {
                    auto& lf = ctx->lane_frontiers[h];
                    lf.active_mask = 0x0FULL; // 4 lanes
                    lf.lanes[0] = 0x1ULL;
                    lf.lanes[1] = 0x2ULL;
                    lf.lanes[2] = 0x4ULL;
                    lf.lanes[3] = 0x8ULL;
                    lf.active_words[0] = 0x0FULL;
                    st->registers[reg] = static_cast<uint64_t>(h);
                } else {
                    st->registers[reg] = 0;
                }
            } else {
                st->registers[reg] = 0;
            }
            break;
        }
        default:
            st->registers[reg] = 0;
            break;
    }
}

static impulse_register_type_t pick_first_type_from_mask(uint32_t mask) {
    for (uint32_t t = 0; t <= 0x10; ++t) {
        if ((mask & (1u << t)) != 0) {
            return static_cast<impulse_register_type_t>(t);
        }
    }
    return TYPE_NULL;
}

static const char* reg_type_to_str(uint8_t t) {
    switch (t) {
        case TYPE_NULL:           return "TYPE_NULL";
        case TYPE_INT64:          return "TYPE_INT64";
        case TYPE_FLOAT:          return "TYPE_FLOAT";
        case TYPE_BITSET_HANDLE:  return "TYPE_BITSET_HANDLE";
        case TYPE_NODE_ID:        return "TYPE_NODE_ID";
        case TYPE_NODE_VECTOR:    return "TYPE_NODE_VECTOR";
        case TYPE_FLOAT_VECTOR:   return "TYPE_FLOAT_VECTOR";
        case TYPE_DOUBLE_VECTOR:  return "TYPE_DOUBLE_VECTOR";
        case TYPE_UINT64_VECTOR:  return "TYPE_UINT64_VECTOR";
        case TYPE_RELATION_ID:    return "TYPE_RELATION_ID";
        case TYPE_CSR_SPAN:       return "TYPE_CSR_SPAN";
        case TYPE_BOOLEAN:        return "TYPE_BOOLEAN";
        case TYPE_VALUE_MAP:      return "TYPE_VALUE_MAP";
        case TYPE_STRING_VECTOR:  return "TYPE_STRING_VECTOR";
        case TYPE_FRONTIER_STATE: return "TYPE_FRONTIER_STATE";
        case TYPE_LANE_FRONTIER:  return "TYPE_LANE_FRONTIER";
        default:                  return "TYPE_UNKNOWN";
    }
}

static impulse_instruction_t build_instruction(const ImpulseOpcodeVariantContract& c,
                                              uint16_t dst, uint16_t src,
                                              uint16_t aux1, uint16_t aux2) {
    impulse_instruction_t inst{};
    inst.opcode = c.opcode;
    inst.flags = c.flags_match;
    inst.payload = c.fixed_payload_bits;

    if (c.dst_layout != RegFieldLayout::NONE) {
        inject_reg_into_layout(inst, c.dst_layout, dst);
    }
    if (c.src_layout != RegFieldLayout::NONE) {
        inject_reg_into_layout(inst, c.src_layout, src);
    }
    if (c.aux1_layout != RegFieldLayout::NONE) {
        inject_reg_into_layout(inst, c.aux1_layout, aux1);
    }
    if (c.aux2_layout != RegFieldLayout::NONE) {
        inject_reg_into_layout(inst, c.aux2_layout, aux2);
    }
    return inst;
}

static uint32_t g_test_inline_buffer[256] = {0};

static impulse_vm_context_t* create_contract_test_context(impulse_snapshot_t* snap) {
    impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
    if (ctx) {
        impulse_vm_context_bind_inline_data(ctx, g_test_inline_buffer, sizeof(g_test_inline_buffer));
        for (size_t i = 0; i < 4 && i < ctx->float_vectors.size(); ++i) {
            ctx->float_vectors[i] = {1.0f, 2.0f, 3.0f, 4.0f};
            ctx->float_vector_lengths[i] = 4;
        }
        if (!ctx->slots.empty() && !ctx->slots[0].csc_offsets_ptr && ctx->slots[0].dynamic_csc_offsets.empty()) {
            auto& slot = ctx->slots[0];
            uint32_t nc = static_cast<uint32_t>(slot.node_count > 0 ? slot.node_count : ctx->max_nodes);
            uint32_t ec = static_cast<uint32_t>(slot.edge_count);
            slot.dynamic_csc_offsets.assign(nc + 1, 0);
            for (uint32_t e = 0; e < ec; ++e) {
                uint64_t tgt = slot.get_csr_target(e);
                if (tgt < nc) slot.dynamic_csc_offsets[tgt + 1]++;
            }
            for (uint32_t i = 0; i < nc; ++i) {
                slot.dynamic_csc_offsets[i + 1] += slot.dynamic_csc_offsets[i];
            }
            slot.dynamic_csc_targets.resize(ec, 0);
            std::vector<uint32_t> cur_pos = slot.dynamic_csc_offsets;
            for (uint32_t u = 0; u < nc; ++u) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end = slot.get_csr_offset(u + 1);
                for (uint64_t e = start; e < end; ++e) {
                    uint64_t v = slot.get_csr_target(e);
                    if (v < nc) slot.dynamic_csc_targets[cur_pos[v]++] = u;
                }
            }
            slot.csc_offsets_ptr = slot.dynamic_csc_offsets.data();
            slot.csc_targets_ptr = slot.dynamic_csc_targets.data();
            slot.csc_node_count = nc;
        }
    }
    return ctx;
}

// ============================================================================
// Pass 1: Disallowed Register Type Sweep
// ============================================================================
static void test_pass1_disallowed_types(const ImpulseOpcodeVariantContract& c,
                                        impulse_snapshot_t* snap) {
    const uint16_t dst_reg  = 10;
    const uint16_t src_reg  = 1;
    const uint16_t aux1_reg = 2;
    const uint16_t aux2_reg = 3;

    impulse_register_type_t valid_src_t  = pick_first_type_from_mask(c.allowed_src_types);
    impulse_register_type_t valid_aux1_t = pick_first_type_from_mask(c.allowed_aux1_types);
    impulse_register_type_t valid_aux2_t = pick_first_type_from_mask(c.allowed_aux2_types);

    // 1A. Test src_reg disallowed types
    if (c.src_layout != RegFieldLayout::NONE && c.allowed_src_types != IMP_TYPES_ANY) {
        for (uint32_t t = 0; t <= 0x10; ++t) {
            if ((c.allowed_src_types & (1u << t)) == 0) {
                impulse_vm_context_t* ctx = create_contract_test_context(snap);
                impulse_vm_state_t st{};
                st.query_context = ctx;

                populate_register(&st, src_reg, static_cast<impulse_register_type_t>(t));
                if (c.aux1_layout != RegFieldLayout::NONE) populate_register(&st, aux1_reg, valid_aux1_t);
                if (c.aux2_layout != RegFieldLayout::NONE) populate_register(&st, aux2_reg, valid_aux2_t);

                impulse_instruction_t prog[2] = {
                    build_instruction(c, dst_reg, src_reg, aux1_reg, aux2_reg),
                    {OP_HALT, 0, 0, 0}
                };

                SafeExecResult res = safe_execute(prog, 2, &st, 0);
                if (res.crash_signal != 0) {
                    std::string detail = "Disallowed src_type " + std::string(reg_type_to_str(t)) +
                                         " caused CRASH/SIGSEGV (signal " + std::to_string(res.crash_signal) + ")";
                    record_check(false, c, "Pass 1 (Crash)", detail);
                } else {
                    bool rejected = (res.status == IMPULSE_VM_ERR_INVALID_REGISTER ||
                                     res.status == IMPULSE_VM_ERR_OUT_OF_BOUNDS);
                    std::string detail = "Disallowed src_type " + std::string(reg_type_to_str(t)) +
                                         " (" + std::to_string(t) + ") was accepted with status " +
                                         std::to_string(res.status);
                    record_check(rejected, c, "Pass 1 (Disallowed src_type)", detail);
                }

                impulse_vm_context_destroy(ctx);
            }
        }
    }

    // 1B. Test aux1_reg disallowed types
    if (c.aux1_layout != RegFieldLayout::NONE && c.allowed_aux1_types != IMP_TYPES_ANY && c.allowed_aux1_types != IMP_TYPES_NONE) {
        for (uint32_t t = 0; t <= 0x10; ++t) {
            if ((c.allowed_aux1_types & (1u << t)) == 0) {
                impulse_vm_context_t* ctx = create_contract_test_context(snap);
                impulse_vm_state_t st{};
                st.query_context = ctx;

                if (c.src_layout != RegFieldLayout::NONE) populate_register(&st, src_reg, valid_src_t);
                populate_register(&st, aux1_reg, static_cast<impulse_register_type_t>(t));
                if (c.aux2_layout != RegFieldLayout::NONE) populate_register(&st, aux2_reg, valid_aux2_t);

                impulse_instruction_t prog[2] = {
                    build_instruction(c, dst_reg, src_reg, aux1_reg, aux2_reg),
                    {OP_HALT, 0, 0, 0}
                };

                SafeExecResult res = safe_execute(prog, 2, &st, 0);
                if (res.crash_signal != 0) {
                    std::string detail = "Disallowed aux1_type " + std::string(reg_type_to_str(t)) +
                                         " caused CRASH/SIGSEGV (signal " + std::to_string(res.crash_signal) + ")";
                    record_check(false, c, "Pass 1 (Crash)", detail);
                } else {
                    bool rejected = (res.status == IMPULSE_VM_ERR_INVALID_REGISTER ||
                                     res.status == IMPULSE_VM_ERR_OUT_OF_BOUNDS);
                    std::string detail = "Disallowed aux1_type " + std::string(reg_type_to_str(t)) +
                                         " was accepted with status " + std::to_string(res.status);
                    record_check(rejected, c, "Pass 1 (Disallowed aux1_type)", detail);
                }

                impulse_vm_context_destroy(ctx);
            }
        }
    }

    // 1C. Test aux2_reg disallowed types
    if (c.aux2_layout != RegFieldLayout::NONE && c.allowed_aux2_types != IMP_TYPES_ANY && c.allowed_aux2_types != IMP_TYPES_NONE) {
        for (uint32_t t = 0; t <= 0x10; ++t) {
            if ((c.allowed_aux2_types & (1u << t)) == 0) {
                impulse_vm_context_t* ctx = create_contract_test_context(snap);
                impulse_vm_state_t st{};
                st.query_context = ctx;

                if (c.src_layout != RegFieldLayout::NONE) populate_register(&st, src_reg, valid_src_t);
                if (c.aux1_layout != RegFieldLayout::NONE) populate_register(&st, aux1_reg, valid_aux1_t);
                populate_register(&st, aux2_reg, static_cast<impulse_register_type_t>(t));

                impulse_instruction_t prog[2] = {
                    build_instruction(c, dst_reg, src_reg, aux1_reg, aux2_reg),
                    {OP_HALT, 0, 0, 0}
                };

                SafeExecResult res = safe_execute(prog, 2, &st, 0);
                if (res.crash_signal != 0) {
                    std::string detail = "Disallowed aux2_type " + std::string(reg_type_to_str(t)) +
                                         " caused CRASH/SIGSEGV (signal " + std::to_string(res.crash_signal) + ")";
                    record_check(false, c, "Pass 1 (Crash)", detail);
                } else {
                    bool rejected = (res.status == IMPULSE_VM_ERR_INVALID_REGISTER ||
                                     res.status == IMPULSE_VM_ERR_OUT_OF_BOUNDS);
                    std::string detail = "Disallowed aux2_type " + std::string(reg_type_to_str(t)) +
                                         " was accepted with status " + std::to_string(res.status);
                    record_check(rejected, c, "Pass 1 (Disallowed aux2_type)", detail);
                }

                impulse_vm_context_destroy(ctx);
            }
        }
    }
}

// ============================================================================
// Pass 2: Allowed Register Types & Output Type Verification
// ============================================================================
static void test_pass2_allowed_types(const ImpulseOpcodeVariantContract& c,
                                     impulse_snapshot_t* snap) {
    const uint16_t dst_reg  = 10;
    const uint16_t src_reg  = 1;
    const uint16_t aux1_reg = 2;
    const uint16_t aux2_reg = 3;

    impulse_register_type_t valid_src_t  = pick_first_type_from_mask(c.allowed_src_types);
    impulse_register_type_t valid_aux1_t = pick_first_type_from_mask(c.allowed_aux1_types);
    impulse_register_type_t valid_aux2_t = pick_first_type_from_mask(c.allowed_aux2_types);

    impulse_vm_context_t* ctx = create_contract_test_context(snap);
    impulse_vm_state_t st{};
    st.query_context = ctx;

    if (c.src_layout != RegFieldLayout::NONE) populate_register(&st, src_reg, valid_src_t);
    if (c.aux1_layout != RegFieldLayout::NONE) populate_register(&st, aux1_reg, valid_aux1_t);
    if (c.aux2_layout != RegFieldLayout::NONE) populate_register(&st, aux2_reg, valid_aux2_t);

    bool is_wide = (c.flags_match & 0x80) != 0;
    impulse_instruction_t prog[3] = {
        build_instruction(c, dst_reg, src_reg, aux1_reg, aux2_reg),
        {OP_HALT, 0, 0, 0},
        {OP_HALT, 0, 0, 0}
    };

    SafeExecResult res = safe_execute(prog, is_wide ? 3 : 2, &st, 0);
    if (res.crash_signal != 0) {
        record_check(false, c, "Pass 2 (Crash)",
                     "Allowed types execution caused CRASH/SIGSEGV (signal " + std::to_string(res.crash_signal) + ")");
    } else {
        bool execution_success = (res.status == IMPULSE_VM_OK ||
                                  (c.opcode == OP_RET && res.status == IMPULSE_VM_ERR_STACK_UNDERFLOW) ||
                                  (c.opcode == OP_THROW && res.status == IMPULSE_VM_ERR_USER_THROW) ||
                                  (c.opcode == OP_TRAP && res.status == IMPULSE_VM_ERR_TRAP));
        record_check(execution_success, c, "Pass 2 (Execution Success)",
                     "Execution of allowed types returned status " + std::to_string(res.status));

        if (res.status == IMPULSE_VM_OK && c.modifies_dst_state && c.output_dst_types != 0) {
            uint8_t out_t = st.register_types[dst_reg];
            bool matches = (c.output_dst_types & (1u << out_t)) != 0;
            std::string detail = "Output type " + std::string(reg_type_to_str(out_t)) +
                                 " (" + std::to_string(out_t) + ") not in expected mask 0x" +
                                 [&]() {
                                     std::stringstream ss;
                                     ss << std::hex << c.output_dst_types;
                                     return ss.str();
                                 }();
            record_check(matches, c, "Pass 2 (Output Type Check)", detail);
        }
    }

    impulse_vm_context_destroy(ctx);
}

// ============================================================================
// Pass 3: Out-of-Bounds Register Index Sweep
// ============================================================================
static void test_pass3_oob_registers(const ImpulseOpcodeVariantContract& c,
                                     impulse_snapshot_t* snap) {
    const uint16_t valid_dst  = 10;
    const uint16_t valid_src  = 1;
    const uint16_t valid_aux1 = 2;
    const uint16_t valid_aux2 = 3;

    impulse_register_type_t valid_src_t  = pick_first_type_from_mask(c.allowed_src_types);
    impulse_register_type_t valid_aux1_t = pick_first_type_from_mask(c.allowed_aux1_types);
    impulse_register_type_t valid_aux2_t = pick_first_type_from_mask(c.allowed_aux2_types);

    const uint16_t oob_indices[] = {64, 255};

    for (uint16_t oob_reg : oob_indices) {
        // 3A. OOB dst_reg
        if (c.dst_layout != RegFieldLayout::NONE) {
            impulse_vm_context_t* ctx = create_contract_test_context(snap);
            impulse_vm_state_t st{};
            st.query_context = ctx;
            if (c.src_layout != RegFieldLayout::NONE) populate_register(&st, valid_src, valid_src_t);
            if (c.aux1_layout != RegFieldLayout::NONE) populate_register(&st, valid_aux1, valid_aux1_t);
            if (c.aux2_layout != RegFieldLayout::NONE) populate_register(&st, valid_aux2, valid_aux2_t);

            impulse_instruction_t prog[2] = {
                build_instruction(c, oob_reg, valid_src, valid_aux1, valid_aux2),
                {OP_HALT, 0, 0, 0}
            };

            SafeExecResult res = safe_execute(prog, 2, &st, 0);
            if (res.crash_signal != 0) {
                record_check(false, c, "Pass 3 (Crash)",
                             "OOB dst_reg=" + std::to_string(oob_reg) + " caused CRASH/SIGSEGV");
            } else {
                record_check(res.status == IMPULSE_VM_ERR_INVALID_REGISTER || res.status == IMPULSE_VM_ERR_OUT_OF_BOUNDS,
                             c, "Pass 3 (OOB dst_reg)",
                             "OOB dst_reg=" + std::to_string(oob_reg) + " accepted with status " + std::to_string(res.status));
            }
            impulse_vm_context_destroy(ctx);
        }

        // 3B. OOB src_reg
        if (c.src_layout != RegFieldLayout::NONE) {
            impulse_vm_context_t* ctx = create_contract_test_context(snap);
            impulse_vm_state_t st{};
            st.query_context = ctx;
            if (c.aux1_layout != RegFieldLayout::NONE) populate_register(&st, valid_aux1, valid_aux1_t);
            if (c.aux2_layout != RegFieldLayout::NONE) populate_register(&st, valid_aux2, valid_aux2_t);

            impulse_instruction_t prog[2] = {
                build_instruction(c, valid_dst, oob_reg, valid_aux1, valid_aux2),
                {OP_HALT, 0, 0, 0}
            };

            SafeExecResult res = safe_execute(prog, 2, &st, 0);
            if (res.crash_signal != 0) {
                record_check(false, c, "Pass 3 (Crash)",
                             "OOB src_reg=" + std::to_string(oob_reg) + " caused CRASH/SIGSEGV");
            } else {
                record_check(res.status == IMPULSE_VM_ERR_INVALID_REGISTER || res.status == IMPULSE_VM_ERR_OUT_OF_BOUNDS,
                             c, "Pass 3 (OOB src_reg)",
                             "OOB src_reg=" + std::to_string(oob_reg) + " accepted with status " + std::to_string(res.status));
            }
            impulse_vm_context_destroy(ctx);
        }

        // 3C. OOB aux1_reg
        if (c.aux1_layout != RegFieldLayout::NONE) {
            impulse_vm_context_t* ctx = create_contract_test_context(snap);
            impulse_vm_state_t st{};
            st.query_context = ctx;
            if (c.src_layout != RegFieldLayout::NONE) populate_register(&st, valid_src, valid_src_t);
            if (c.aux2_layout != RegFieldLayout::NONE) populate_register(&st, valid_aux2, valid_aux2_t);

            impulse_instruction_t prog[2] = {
                build_instruction(c, valid_dst, valid_src, oob_reg, valid_aux2),
                {OP_HALT, 0, 0, 0}
            };

            SafeExecResult res = safe_execute(prog, 2, &st, 0);
            if (res.crash_signal != 0) {
                record_check(false, c, "Pass 3 (Crash)",
                             "OOB aux1_reg=" + std::to_string(oob_reg) + " caused CRASH/SIGSEGV");
            } else {
                record_check(res.status == IMPULSE_VM_ERR_INVALID_REGISTER || res.status == IMPULSE_VM_ERR_OUT_OF_BOUNDS,
                             c, "Pass 3 (OOB aux1_reg)",
                             "OOB aux1_reg=" + std::to_string(oob_reg) + " accepted with status " + std::to_string(res.status));
            }
            impulse_vm_context_destroy(ctx);
        }

        // 3D. OOB aux2_reg
        if (c.aux2_layout != RegFieldLayout::NONE) {
            impulse_vm_context_t* ctx = create_contract_test_context(snap);
            impulse_vm_state_t st{};
            st.query_context = ctx;
            if (c.src_layout != RegFieldLayout::NONE) populate_register(&st, valid_src, valid_src_t);
            if (c.aux1_layout != RegFieldLayout::NONE) populate_register(&st, valid_aux1, valid_aux1_t);

            impulse_instruction_t prog[2] = {
                build_instruction(c, valid_dst, valid_src, valid_aux1, oob_reg),
                {OP_HALT, 0, 0, 0}
            };

            SafeExecResult res = safe_execute(prog, 2, &st, 0);
            if (res.crash_signal != 0) {
                record_check(false, c, "Pass 3 (Crash)",
                             "OOB aux2_reg=" + std::to_string(oob_reg) + " caused CRASH/SIGSEGV");
            } else {
                record_check(res.status == IMPULSE_VM_ERR_INVALID_REGISTER || res.status == IMPULSE_VM_ERR_OUT_OF_BOUNDS,
                             c, "Pass 3 (OOB aux2_reg)",
                             "OOB aux2_reg=" + std::to_string(oob_reg) + " accepted with status " + std::to_string(res.status));
            }
            impulse_vm_context_destroy(ctx);
        }
    }
}

// ============================================================================
// Pass 4: In-Place Register Aliasing Verification
// ============================================================================
static void test_pass4_in_place_aliasing(const ImpulseOpcodeVariantContract& c,
                                         impulse_snapshot_t* snap) {
    if (!c.modifies_dst_state) return;

    const uint16_t dst_baseline = 10;
    const uint16_t src_reg      = 1;
    const uint16_t aux1_reg     = 2;
    const uint16_t aux2_reg     = 3;

    impulse_register_type_t valid_src_t  = pick_first_type_from_mask(c.allowed_src_types);
    impulse_register_type_t valid_aux1_t = pick_first_type_from_mask(c.allowed_aux1_types);
    impulse_register_type_t valid_aux2_t = pick_first_type_from_mask(c.allowed_aux2_types);

    // 4A. Run out-of-place baseline
    impulse_vm_context_t* ctx_base = create_contract_test_context(snap);
    impulse_vm_state_t st_base{};
    st_base.query_context = ctx_base;

    if (c.src_layout != RegFieldLayout::NONE) populate_register(&st_base, src_reg, valid_src_t);
    if (c.aux1_layout != RegFieldLayout::NONE) populate_register(&st_base, aux1_reg, valid_aux1_t);
    if (c.aux2_layout != RegFieldLayout::NONE) populate_register(&st_base, aux2_reg, valid_aux2_t);

    bool is_wide = (c.flags_match & 0x80) != 0;
    impulse_instruction_t prog_base[3] = {
        build_instruction(c, dst_baseline, src_reg, aux1_reg, aux2_reg),
        {OP_HALT, 0, 0, 0},
        {OP_HALT, 0, 0, 0}
    };

    SafeExecResult res_base = safe_execute(prog_base, is_wide ? 3 : 2, &st_base, 0);
    if (res_base.crash_signal != 0 || res_base.status != IMPULSE_VM_OK) {
        impulse_vm_context_destroy(ctx_base);
        return; // Baseline failed; caught in Pass 2
    }

    uint8_t base_dst_type = st_base.register_types[dst_baseline];
    uint64_t base_dst_val = st_base.registers[dst_baseline];
    uint64_t base_flags   = st_base.flags;

    // Helper lambda to compare aliased execution with baseline
    auto verify_aliased_result = [&](uint16_t aliased_dst, const std::string& alias_case) {
        impulse_vm_context_t* ctx_alias = create_contract_test_context(snap);
        impulse_vm_state_t st_alias{};
        st_alias.query_context = ctx_alias;

        if (c.src_layout != RegFieldLayout::NONE) populate_register(&st_alias, src_reg, valid_src_t);
        if (c.aux1_layout != RegFieldLayout::NONE) populate_register(&st_alias, aux1_reg, valid_aux1_t);
        if (c.aux2_layout != RegFieldLayout::NONE) populate_register(&st_alias, aux2_reg, valid_aux2_t);

        impulse_instruction_t prog_alias[3] = {
            build_instruction(c, aliased_dst, src_reg, aux1_reg, aux2_reg),
            {OP_HALT, 0, 0, 0},
            {OP_HALT, 0, 0, 0}
        };

        SafeExecResult res_alias = safe_execute(prog_alias, is_wide ? 3 : 2, &st_alias, 0);
        if (res_alias.crash_signal != 0) {
            record_check(false, c, "Pass 4 (Crash)",
                         alias_case + " caused CRASH/SIGSEGV (signal " + std::to_string(res_alias.crash_signal) + ")");
            impulse_vm_context_destroy(ctx_alias);
            return;
        }

        bool status_ok = (res_alias.status == IMPULSE_VM_OK);
        record_check(status_ok, c, "Pass 4 (" + alias_case + " status)",
                     alias_case + " returned error status " + std::to_string(res_alias.status));

        if (status_ok) {
            bool type_match = (st_alias.register_types[aliased_dst] == base_dst_type);
            record_check(type_match, c, "Pass 4 (" + alias_case + " type)",
                         alias_case + " type mismatch: got " +
                         std::string(reg_type_to_str(st_alias.register_types[aliased_dst])) +
                         ", expected " + std::string(reg_type_to_str(base_dst_type)));

            bool flags_match = ((st_alias.flags & (IMPULSE_VM_FLAG_ZF | IMPULSE_VM_FLAG_ST)) ==
                                (base_flags & (IMPULSE_VM_FLAG_ZF | IMPULSE_VM_FLAG_ST)));
            record_check(flags_match, c, "Pass 4 (" + alias_case + " flags)",
                         alias_case + " flags mismatch: got " + std::to_string(st_alias.flags) +
                         ", expected " + std::to_string(base_flags));

            if (base_dst_type == TYPE_INT64 || base_dst_type == TYPE_NODE_ID || base_dst_type == TYPE_FLOAT) {
                bool val_match = (st_alias.registers[aliased_dst] == base_dst_val);
                record_check(val_match, c, "Pass 4 (" + alias_case + " scalar val)",
                             alias_case + " scalar value mismatch");
            } else if (base_dst_type == TYPE_BITSET_HANDLE) {
                int h_b = static_cast<int>(base_dst_val);
                int h_a = static_cast<int>(st_alias.registers[aliased_dst]);
                if (h_b >= 0 && h_a >= 0 &&
                    static_cast<size_t>(h_b) < VM_MAX_BITSET_HANDLES &&
                    static_cast<size_t>(h_a) < VM_MAX_BITSET_HANDLES) {
                    bool words_match = true;
                    for (size_t w = 0; w < ctx_base->words_per_bitset; ++w) {
                        if (ctx_base->bitsets[h_b].words[w] != ctx_alias->bitsets[h_a].words[w]) {
                            words_match = false;
                            break;
                        }
                    }
                    record_check(words_match, c, "Pass 4 (" + alias_case + " bitset data)",
                                 alias_case + " bitset contents differ from out-of-place execution");
                }
            } else if (base_dst_type == TYPE_LANE_FRONTIER) {
                size_t h_b = static_cast<size_t>(base_dst_val);
                size_t h_a = static_cast<size_t>(st_alias.registers[aliased_dst]);
                if (h_b < VM_MAX_LANE_FRONTIER_HANDLES && h_a < VM_MAX_LANE_FRONTIER_HANDLES &&
                    h_b < ctx_base->lane_frontiers_count && h_a < ctx_alias->lane_frontiers_count) {
                    const auto& lf_b = ctx_base->lane_frontiers[h_b];
                    const auto& lf_a = ctx_alias->lane_frontiers[h_a];
                    bool lanes_match = (lf_b.lanes == lf_a.lanes && lf_b.active_words == lf_a.active_words);
                    record_check(lanes_match, c, "Pass 4 (" + alias_case + " lane frontier data)",
                                 alias_case + " lane frontier contents differ from out-of-place execution");
                }
            }
        }

        impulse_vm_context_destroy(ctx_alias);
    };

    // Case 1: dst == src
    if (c.src_layout != RegFieldLayout::NONE) {
        verify_aliased_result(src_reg, "dst == src");
    }

    // Case 2: dst == aux1
    if (c.aux1_layout != RegFieldLayout::NONE) {
        verify_aliased_result(aux1_reg, "dst == aux1");
    }

    // Case 3: dst == aux2
    if (c.aux2_layout != RegFieldLayout::NONE) {
        verify_aliased_result(aux2_reg, "dst == aux2");
    }

    impulse_vm_context_destroy(ctx_base);
}

// ============================================================================
// Pass 5: Null Context Guard
// ============================================================================
static void test_pass5_null_context(const ImpulseOpcodeVariantContract& c) {
    if (!c.requires_context) return;

    const uint16_t dst_reg  = 10;
    const uint16_t src_reg  = 1;
    const uint16_t aux1_reg = 2;
    const uint16_t aux2_reg = 3;

    impulse_register_type_t valid_src_t  = pick_first_type_from_mask(c.allowed_src_types);
    impulse_register_type_t valid_aux1_t = pick_first_type_from_mask(c.allowed_aux1_types);
    impulse_register_type_t valid_aux2_t = pick_first_type_from_mask(c.allowed_aux2_types);

    impulse_vm_state_t st{};
    st.query_context = nullptr; // Intentionally null

    populate_register(&st, src_reg, valid_src_t);
    populate_register(&st, aux1_reg, valid_aux1_t);
    populate_register(&st, aux2_reg, valid_aux2_t);

    bool is_wide = (c.flags_match & 0x80) != 0;
    impulse_instruction_t prog[3] = {
        build_instruction(c, dst_reg, src_reg, aux1_reg, aux2_reg),
        {OP_HALT, 0, 0, 0},
        {OP_HALT, 0, 0, 0}
    };

    SafeExecResult res = safe_execute(prog, is_wide ? 3 : 2, &st, 0);
    if (res.crash_signal != 0) {
        record_violation(c, "Pass 5 (Null Context CRASH)",
                         "Execution with null query_context crashed with signal " + std::to_string(res.crash_signal));
        g_total_assertions++;
    } else {
        bool rejected = (res.status == IMPULSE_VM_ERR_NULL_SNAPSHOT ||
                         res.status == IMPULSE_VM_ERR_INVALID_REGISTER ||
                         res.status == IMPULSE_VM_ERR_OUT_OF_BOUNDS);
        record_check(rejected, c, "Pass 5 (Null Context Check)",
                     "Null context returned status " + std::to_string(res.status) +
                     ", expected IMPULSE_VM_ERR_NULL_SNAPSHOT");
    }
}

// ============================================================================
// Pass 6: VMContext Invariant Audit
// ============================================================================
static void test_pass6_vmcontext_invariants(const ImpulseOpcodeVariantContract& c,
                                            impulse_snapshot_t* snap) {
    if (!c.requires_context) return;

    const uint16_t dst_reg  = 10;
    const uint16_t src_reg  = 1;
    const uint16_t aux1_reg = 2;
    const uint16_t aux2_reg = 3;

    impulse_register_type_t valid_src_t  = pick_first_type_from_mask(c.allowed_src_types);
    impulse_register_type_t valid_aux1_t = pick_first_type_from_mask(c.allowed_aux1_types);
    impulse_register_type_t valid_aux2_t = pick_first_type_from_mask(c.allowed_aux2_types);

    impulse_vm_context_t* ctx = create_contract_test_context(snap);
    if (!ctx) return;

    // Establish Invariant Baselines
    const impulse_snapshot_t* snap_before = ctx->snapshot;
    uint64_t* arena_before = ctx->arena_memory;
    const uint8_t* inline_before = ctx->inline_data_ptr;
    size_t inline_bytes_before = ctx->inline_data_bytes;

    // Populate Sentinel Handles (Handle 0 in each pool)
    int sentinel_bs = acquire_bitset(ctx); // Handle 0
    if (sentinel_bs >= 0 && static_cast<size_t>(sentinel_bs) < ctx->bitsets.size()) {
        ctx->bitsets[sentinel_bs].set(7);
        ctx->bitsets[sentinel_bs].set(42);
    }
    int sentinel_fv = acquire_float_vector(ctx); // Handle 0
    if (sentinel_fv >= 0 && static_cast<size_t>(sentinel_fv) < ctx->float_vectors.size() && !ctx->float_vectors[sentinel_fv].empty()) {
        ctx->float_vectors[sentinel_fv][0] = 777.7f;
    }
    int sentinel_nv = acquire_node_vector(ctx); // Handle 0
    if (sentinel_nv >= 0 && static_cast<size_t>(sentinel_nv) < ctx->node_vectors.size() && !ctx->node_vectors[sentinel_nv].empty()) {
        ctx->node_vectors[sentinel_nv][0] = 9999ULL;
    }

    impulse_vm_state_t st{};
    st.query_context = ctx;

    if (c.src_layout != RegFieldLayout::NONE) populate_register(&st, src_reg, valid_src_t);
    if (c.aux1_layout != RegFieldLayout::NONE) populate_register(&st, aux1_reg, valid_aux1_t);
    if (c.aux2_layout != RegFieldLayout::NONE) populate_register(&st, aux2_reg, valid_aux2_t);

    bool is_wide = (c.flags_match & 0x80) != 0;
    impulse_instruction_t prog[3] = {
        build_instruction(c, dst_reg, src_reg, aux1_reg, aux2_reg),
        {OP_HALT, 0, 0, 0},
        {OP_HALT, 0, 0, 0}
    };

    // Execute with heap allocation tracking active
    g_heap_alloc_count = 0;
    g_track_heap_allocs = true;
    safe_execute(prog, is_wide ? 3 : 2, &st, 0);
    g_track_heap_allocs = false;

    // Assert Invariants
    // Invariant A: Zero Heap Allocations on Hot Path
    if (c.opcode != OP_INIT_MOCK_GRAPH) {
        record_check(g_heap_alloc_count == 0, c, "Pass 6 (Zero Heap Allocations)",
                     "Observed " + std::to_string(g_heap_alloc_count) + " heap allocations during opcode execution");
    }

    // Invariant B: Snapshot Pointer Immutability
    record_check(ctx->snapshot == snap_before, c, "Pass 6 (Snapshot Immutability)",
                 "ctx->snapshot pointer was modified during opcode execution");

    // Invariant C: Arena Memory Pointer Immutability
    record_check(ctx->arena_memory == arena_before, c, "Pass 6 (Arena Memory Immutability)",
                 "ctx->arena_memory base pointer was modified during opcode execution");

    // Invariant D: Inline Data Pointer Immutability
    record_check(ctx->inline_data_ptr == inline_before && ctx->inline_data_bytes == inline_bytes_before,
                 c, "Pass 6 (Inline Data Immutability)",
                 "ctx->inline_data_ptr or inline_data_bytes was modified during opcode execution");

    // Invariant E: Handle Isolation (Sentinel BitSet)
    if (sentinel_bs >= 0 && static_cast<size_t>(sentinel_bs) < ctx->bitsets.size()) {
        bool bit7 = ctx->bitsets[sentinel_bs].test(7);
        bool bit42 = ctx->bitsets[sentinel_bs].test(42);
        record_check(bit7 && bit42, c, "Pass 6 (BitSet Handle Isolation)",
                     "Sentinel BitSet handle was mutated by unrelated opcode execution");
    }

    // Invariant F: Handle Isolation (Sentinel Float Vector)
    if (sentinel_fv >= 0 && static_cast<size_t>(sentinel_fv) < ctx->float_vectors.size() && !ctx->float_vectors[sentinel_fv].empty()) {
        record_check(ctx->float_vectors[sentinel_fv][0] == 777.7f, c, "Pass 6 (Float Vector Handle Isolation)",
                     "Sentinel Float Vector handle was mutated by unrelated opcode execution");
    }

    // Invariant G: Handle Isolation (Sentinel Node Vector)
    if (sentinel_nv >= 0 && static_cast<size_t>(sentinel_nv) < ctx->node_vectors.size() && !ctx->node_vectors[sentinel_nv].empty()) {
        record_check(ctx->node_vectors[sentinel_nv][0] == 9999ULL, c, "Pass 6 (Node Vector Handle Isolation)",
                     "Sentinel Node Vector handle was mutated by unrelated opcode execution");
    }

    impulse_vm_context_destroy(ctx);
}

} // anonymous namespace

// ============================================================================
// Main Suite Runner
// ============================================================================
int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--dump-json") == 0) {
        std::vector<ImpulseOpcodeVariantContract> contracts = get_all_opcode_contracts();
        std::ostream* out = &std::cout;
        std::ofstream fout;
        if (argc > 2) {
            fout.open(argv[2]);
            if (fout.is_open()) out = &fout;
        }
        *out << "[\n";
        for (size_t i = 0; i < contracts.size(); ++i) {
            const auto& c = contracts[i];
            *out << "  {\n";
            *out << "    \"opcode\": " << static_cast<int>(c.opcode) << ",\n";
            *out << "    \"mnemonic\": \"" << c.mnemonic << "\",\n";
            *out << "    \"variant_name\": \"" << c.variant_name << "\",\n";
            *out << "    \"flags_mask\": " << static_cast<int>(c.flags_mask) << ",\n";
            *out << "    \"flags_match\": " << static_cast<int>(c.flags_match) << ",\n";
            *out << "    \"defined_flags_mask\": " << static_cast<int>(c.defined_flags_mask) << ",\n";
            *out << "    \"fixed_payload_bits\": " << c.fixed_payload_bits << ",\n";
            *out << "    \"dst_layout\": " << static_cast<int>(c.dst_layout) << ",\n";
            *out << "    \"src_layout\": " << static_cast<int>(c.src_layout) << ",\n";
            *out << "    \"aux1_layout\": " << static_cast<int>(c.aux1_layout) << ",\n";
            *out << "    \"aux2_layout\": " << static_cast<int>(c.aux2_layout) << ",\n";
            *out << "    \"allowed_src_types\": " << c.allowed_src_types << ",\n";
            *out << "    \"allowed_aux1_types\": " << c.allowed_aux1_types << ",\n";
            *out << "    \"allowed_aux2_types\": " << c.allowed_aux2_types << ",\n";
            *out << "    \"output_dst_types\": " << c.output_dst_types << ",\n";
            *out << "    \"allow_zero_skip_aux1\": " << (c.allow_zero_skip_aux1 ? "true" : "false") << ",\n";
            *out << "    \"allow_zero_skip_aux2\": " << (c.allow_zero_skip_aux2 ? "true" : "false") << ",\n";
            *out << "    \"sets_vm_flags_mask\": " << static_cast<int>(c.sets_vm_flags_mask) << ",\n";
            *out << "    \"requires_context\": " << (c.requires_context ? "true" : "false") << ",\n";
            *out << "    \"modifies_dst_state\": " << (c.modifies_dst_state ? "true" : "false") << "\n";
            *out << "  }" << (i + 1 < contracts.size() ? "," : "") << "\n";
        }
        *out << "]\n";
        return 0;
    }

    (void)argc;
    (void)argv;

    // Install crash handler for full suite run
    ScopedCrashHandler crash_guard;

    std::cout << "================================================================================" << std::endl;
    std::cout << "           ImpulseVM Formal Opcode Contract Test Suite (Red-to-Green)           " << std::endl;
    std::cout << "================================================================================" << std::endl;

    impulse_snapshot_t* snap = create_test_snapshot();
    if (!snap) {
        std::cerr << "CRITICAL: Unable to initialize test snapshot. Aborting test suite." << std::endl;
        return 1;
    }

    std::vector<ImpulseOpcodeVariantContract> contracts = get_all_opcode_contracts();
    std::cout << "[INFO] Loaded " << contracts.size() << " formal opcode variant contracts." << std::endl;

    for (size_t idx = 0; idx < contracts.size(); ++idx) {
        const auto& c = contracts[idx];
        test_pass1_disallowed_types(c, snap);
        test_pass2_allowed_types(c, snap);
        test_pass3_oob_registers(c, snap);
        test_pass4_in_place_aliasing(c, snap);
        test_pass5_null_context(c);
        test_pass6_vmcontext_invariants(c, snap);
    }

    impulse_snapshot_close(snap);
    std::remove(TEST_SNAP_PATH);

    // Print Detailed Failure Report
    std::cout << "\n================================================================================" << std::endl;
    std::cout << "                                CONTRACT AUDIT REPORT                           " << std::endl;
    std::cout << "================================================================================" << std::endl;

    if (g_violations.empty()) {
        std::cout << "\n>>> ALL CONTRACT CHECKS PASSED PERFECTLY (100% GREEN) <<<\n" << std::endl;
    } else {
        std::cout << "\n>>> " << g_violations.size() << " CONTRACT VIOLATIONS DETECTED (RED PHASE) <<<\n" << std::endl;
        for (size_t i = 0; i < g_violations.size(); ++i) {
            const auto& v = g_violations[i];
            std::cout << " [" << std::setw(3) << (i + 1) << "] "
                      << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(v.opcode)
                      << std::dec << std::setfill(' ') << " (" << v.mnemonic << "): "
                      << v.variant << "\n"
                      << "       " << v.pass_name << " -> " << v.details << "\n";
        }
    }

    std::cout << "\n--------------------------------------------------------------------------------" << std::endl;
    std::cout << "SUMMARY STATISTICS:" << std::endl;
    std::cout << "  Contracts Audited:   " << contracts.size() << std::endl;
    std::cout << "  Total Assertions:    " << g_total_assertions << std::endl;
    std::cout << "  Passed Assertions:   " << g_passed_assertions << " ("
              << std::fixed << std::setprecision(1)
              << (g_total_assertions > 0 ? (100.0 * g_passed_assertions / g_total_assertions) : 0.0)
              << "%)" << std::endl;
    std::cout << "  Violations / Flaws:  " << g_violations.size() << std::endl;
    std::cout << "================================================================================" << std::endl;

    return g_violations.empty() ? 0 : 1;
}
