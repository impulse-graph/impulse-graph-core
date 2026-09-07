/**
 * @file fuzz_vm_bytecode.cpp
 * @brief Fuzzing harness for ImpulseVM instruction decoder, validator, and execution engine.
 *
 * Supports both:
 * 1. Native LLVM libFuzzer (`-fsanitize=fuzzer`) via LLVMFuzzerTestOneInput
 * 2. Standalone deterministic runner generating pseudo-random instruction streams.
 */

#include "impulse_vm.h"
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <iostream>
#include <string>
#include <chrono>

namespace {
struct FuzzRng {
    uint64_t state;
    explicit FuzzRng(uint64_t seed = 0x9e3779b97f4a7c15ULL) : state(seed ? seed : 1ULL) {}
    inline uint64_t next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }
    inline uint32_t next_u32() {
        return static_cast<uint32_t>(next() >> 32);
    }
    inline uint32_t bounded(uint32_t bound) {
        if (bound == 0) return 0;
        return static_cast<uint32_t>(next() % bound);
    }
};
} // anonymous namespace

static impulse_vm_context_t* g_fuzz_ctx = nullptr;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (!data || size < sizeof(impulse_instruction_t)) {
        return 0;
    }

    size_t num_instructions = size / sizeof(impulse_instruction_t);
    if (num_instructions > 512) {
        num_instructions = 512;
    }

    const auto* instructions = reinterpret_cast<const impulse_instruction_t*>(data);

    // 1. Invariant: Validator must never crash or fault
    impulse_vm_status_t val_status = impulse_vm_validate(instructions, num_instructions);
    (void)val_status;

    // 2. Invariant: VM execution under fuel limit must terminate cleanly without crash or UB
    impulse_vm_state_t state{};
    state.pc = 0;
    state.flags = 0;

    bool local_ctx = false;
    impulse_vm_context_t* ctx = g_fuzz_ctx;
    if (!ctx) {
        ctx = impulse_vm_context_create(nullptr);
        local_ctx = true;
    } else {
        impulse_vm_context_reset(ctx);
    }

    impulse_vm_context_set_fuel(ctx, 2000);
    state.query_context = ctx;

    impulse_vm_status_t exec_status = impulse_vm_execute(
        instructions, num_instructions, &state, 0
    );

    if (local_ctx) {
        impulse_vm_context_destroy(ctx);
    }
    state.query_context = nullptr;

    (void)exec_status;
    return 0;
}

#if !defined(IMPULSE_LIBFUZZER_ENABLED)

static const uint8_t kKnownOpcodes[] = {
    OP_HALT, OP_NOP, OP_INIT_INPUT_NODE, OP_INIT_INPUT_SET, OP_LOAD_CONST_INT,
    OP_MAP_KEYS_TO_DENSE, OP_LOAD_CONST_FLOAT, OP_LOAD_CONST_STR_PREFIX,
    OP_LOAD_INLINE_ARRAY, OP_INIT_MOCK_GRAPH, OP_LOAD_INLINE_SET, OP_INIT_MOCK_NODE_ATTR,
    OP_INIT_MOCK_EDGE_ATTR, OP_CSR_WALK_2HOP, OP_CSR_WALK_STATE, OP_CSR_WALK,
    OP_CSR_WALK_FILTERED, OP_CSR_DEGREE, OP_CSR_WALK_PREDICATE, OP_NODE_FILTER,
    OP_NODE_FILTER_STR_PREFIX, OP_CSR_WALK_REDUCE_SUM, OP_CSR_WALK_REDUCE, OP_CSC_WALK,
    OP_HAS_CSR, OP_HAS_CSC, OP_HAS_COO, OP_HAS_KEY_CATALOG, OP_ADAPTIVE_WALK,
    OP_CREATE_SCRATCH_INDEX, OP_DROP_SCRATCH_INDEX, OP_VEC_CMP_EQ, OP_VEC_CMP_GT,
    OP_VEC_CMP_LT, OP_VEC_CMP_BETWEEN, OP_MASK_AND, OP_MASK_OR, OP_MASK_NOT,
    OP_VEC_BLEND, OP_ASSERT_FINITE, OP_VEC_MATH_UNARY, OP_VEC_MATH_BINARY,
    OP_VEC_MATH_TERNARY, OP_SET_UNION, OP_SET_INTERSECT, OP_SET_DIFFERENCE,
    OP_SET_CARDINALITY, OP_VECTOR_MUL_ATTR, OP_VECTOR_REDUCE_SUM, OP_VECTOR_DIV,
    OP_VECTOR_STR_CONCAT, OP_FLOAT_VECTOR_SCALE, OP_L1_NORM_DIFF, OP_PROJECT_STATE,
    OP_COALESCE, OP_EXTRACT_VALIDITY, OP_VECTOR_TIME_VALID_AT, OP_CC_AFFOREST,
    OP_MXV, OP_VXM, OP_EWISE_ADD, OP_EWISE_MULT, OP_REDUCE, OP_CC_HOOK_COMPRESS,
    OP_TC_SWEEP_BATCH, OP_BRANDES_FORWARD, OP_BRANDES_BACKWARD, OP_DELTA_STEP_RELAX,
    OP_READ_EDGE_WEIGHT, OP_JMP, OP_JZ, OP_JNZ, OP_LOOP_DECR, OP_STABLE_CHECK,
    OP_CALL, OP_RET, OP_ENTER_FRAME, OP_LEAVE_FRAME, OP_THROW, OP_ASSERT, OP_TRAP,
    OP_SAMPLE_NEIGHBORS, OP_RANDOM_WALK, OP_SCATTER_GATHER, OP_REBAC_CHECK,
    OP_ROARING_BITMAP_AND, OP_ISLAND_DETECT, OP_SPARSE_MATVEC, OP_LOUVAIN_MODULARITY,
    OP_KCORE_DECOMPOSITION, OP_MOTIF_MATCH_3, OP_GRAPH_ISOMORPHISM, OP_ROARING_BITMAP_OR,
    OP_ROARING_BITMAP_AND_NOT, OP_MOV, OP_CLEAR_REG, OP_LOAD_INDIRECT, OP_ALLOC_SCRATCH,
    OP_ASSERT_SCRATCH_BYTES, OP_SET_MAX_DOP
};

static std::vector<impulse_instruction_t> generate_random_program(FuzzRng& rng, size_t length) {
    std::vector<impulse_instruction_t> prog;
    prog.reserve(length + 1);

    for (size_t i = 0; i < length; ++i) {
        impulse_instruction_t inst{};
        if (rng.bounded(5) != 0) {
            inst.opcode = kKnownOpcodes[rng.bounded(sizeof(kKnownOpcodes))];
        } else {
            inst.opcode = static_cast<uint8_t>(rng.next_u32() & 0xFF);
        }

        inst.flags = static_cast<uint8_t>(rng.next_u32() & 0x0F);
        if (rng.bounded(10) != 0) {
            inst.dst_reg = static_cast<uint16_t>(rng.bounded(64));
        } else {
            inst.dst_reg = static_cast<uint16_t>(rng.bounded(256));
        }

        inst.payload = rng.next_u32();
        prog.push_back(inst);
    }

    prog.push_back(impulse_instruction_t{OP_HALT, 0, 0, 0});
    return prog;
}

int main(int argc, char** argv) {
    size_t iterations = 20000;
    uint64_t seed = 987654321ULL;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--iterations" && i + 1 < argc) {
            iterations = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = std::strtoull(argv[++i], nullptr, 10);
        }
    }

    std::cout << "============================================================" << std::endl;
    std::cout << " ImpulseVM Instruction Fuzzing Suite (Deterministic Mode)" << std::endl;
    std::cout << " Iterations: " << iterations << " | Seed: " << seed << std::endl;
    std::cout << "============================================================" << std::endl;

    FuzzRng rng(seed);
    auto start_time = std::chrono::steady_clock::now();

    size_t valid_programs = 0;
    size_t invalid_programs = 0;

    g_fuzz_ctx = impulse_vm_context_create(nullptr);

    for (size_t iter = 0; iter < iterations; ++iter) {
        size_t len = 1 + rng.bounded(32);
        auto prog = generate_random_program(rng, len);

        const uint8_t* raw_bytes = reinterpret_cast<const uint8_t*>(prog.data());
        size_t raw_size = prog.size() * sizeof(impulse_instruction_t);

        impulse_vm_status_t st = impulse_vm_validate(prog.data(), prog.size());
        if (st == IMPULSE_VM_OK) {
            valid_programs++;
        } else {
            invalid_programs++;
        }

        LLVMFuzzerTestOneInput(raw_bytes, raw_size);

        if ((iter + 1) % 5000 == 0 || iter + 1 == iterations) {
            std::cout << "[Fuzz VM] Progress: " << (iter + 1) << " / " << iterations
                      << " | Valid: " << valid_programs
                      << " | Invalid: " << invalid_programs << std::endl;
        }
    }

    if (g_fuzz_ctx) {
        impulse_vm_context_destroy(g_fuzz_ctx);
        g_fuzz_ctx = nullptr;
    }

    auto end_time = std::chrono::steady_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::cout << "[Fuzz VM] COMPLETED " << iterations << " iterations in "
              << duration_ms << " ms (" << (static_cast<double>(iterations) / (duration_ms / 1000.0))
              << " exec/sec). Zero crashes." << std::endl;

    return 0;
}
#endif
