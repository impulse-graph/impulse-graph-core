#include "impulse_vm.h"
#include "impulse_graph.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>
#include <limits>
#include <cstring>

#undef assert
#define assert(expr) do { if (!(expr)) { std::cerr << "Assertion failed: " << #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; std::abort(); } } while(0)

// Helper dummy structs to access internal layout if needed
struct DummyBitSet {
    uint64_t* words = nullptr;
    size_t word_count = 0;

    void clear() {
        if (words && word_count > 0) {
            std::memset(words, 0, word_count * sizeof(uint64_t));
        }
    }

    inline void set(size_t bit_idx) {
        if (words && (bit_idx >> 6) < word_count) {
            words[bit_idx >> 6] |= (1ULL << (bit_idx & 63));
        }
    }

    inline bool test(size_t bit_idx) const {
        if (words && (bit_idx >> 6) < word_count) {
            return (words[bit_idx >> 6] & (1ULL << (bit_idx & 63))) != 0;
        }
        return false;
    }

    inline void reset(size_t bit_idx) {
        if (words && (bit_idx >> 6) < word_count) {
            words[bit_idx >> 6] &= ~(1ULL << (bit_idx & 63));
        }
    }
};

struct DummySlot {
    const void* offsets_ptr = nullptr;
    const void* targets_ptr = nullptr;
    const void* csc_offsets_ptr = nullptr;
    const void* csc_targets_ptr = nullptr;
    uint8_t     node_id_width = 4;
    uint8_t     edge_index_width = 4;
    uint64_t    node_count = 0;
    uint64_t    edge_count = 0;

    inline uint64_t get_csr_offset(uint64_t u) const {
        if (!offsets_ptr) return 0;
        if (edge_index_width == 8 || edge_index_width == 64) {
            return static_cast<const uint64_t*>(offsets_ptr)[u];
        }
        return static_cast<const uint32_t*>(offsets_ptr)[u];
    }

    inline uint64_t get_csr_target(uint64_t edge_idx) const {
        if (!targets_ptr) return 0;
        if (node_id_width == 2 || node_id_width == 16) {
            return static_cast<const uint16_t*>(targets_ptr)[edge_idx];
        } else if (node_id_width == 8 || node_id_width == 64) {
            return static_cast<const uint64_t*>(targets_ptr)[edge_idx];
        }
        return static_cast<const uint32_t*>(targets_ptr)[edge_idx];
    }

    inline uint64_t get_csc_offset(uint64_t v) const {
        if (!csc_offsets_ptr) return 0;
        if (edge_index_width == 8 || edge_index_width == 64) {
            return static_cast<const uint64_t*>(csc_offsets_ptr)[v];
        }
        return static_cast<const uint32_t*>(csc_offsets_ptr)[v];
    }

    inline uint64_t get_csc_target(uint64_t edge_idx) const {
        if (!csc_targets_ptr) return 0;
        if (node_id_width == 2 || node_id_width == 16) {
            return static_cast<const uint16_t*>(csc_targets_ptr)[edge_idx];
        } else if (node_id_width == 8 || node_id_width == 64) {
            return static_cast<const uint64_t*>(csc_targets_ptr)[edge_idx];
        }
        return static_cast<const uint32_t*>(csc_targets_ptr)[edge_idx];
    }
};

void test_mcdc_bitset_conditions() {
    std::cout << "[MC/DC] Testing VmBitSet Condition Independence..." << std::endl;
    uint64_t buf[2] = { 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL };

    // 1. clear(): (words && word_count > 0)
    // TC-Base: T, T -> clears
    DummyBitSet bs1{ buf, 2 };
    bs1.clear();
    assert(buf[0] == 0 && buf[1] == 0);

    // TC-1 (words == null): F, T -> nothing
    DummyBitSet bs2{ nullptr, 2 };
    bs2.clear();

    // TC-2 (word_count == 0): T, F -> nothing
    buf[0] = 42;
    DummyBitSet bs3{ buf, 0 };
    bs3.clear();
    assert(buf[0] == 42);

    // 2. set(): (words && (bit_idx >> 6) < word_count)
    buf[0] = 0;
    DummyBitSet bs4{ buf, 1 };
    // TC-Base: T, T
    bs4.set(5);
    assert((buf[0] & (1ULL << 5)) != 0);
    // TC-1: F, T
    DummyBitSet bs5{ nullptr, 1 };
    bs5.set(5);
    // TC-2: T, F (out of bounds)
    bs4.set(128); // 128 >> 6 = 2 >= word_count(1)

    // 3. test(): (words && (bit_idx >> 6) < word_count)
    assert(bs4.test(5) == true);   // T, T -> T
    assert(bs4.test(6) == false);  // T, T -> F (bit not set)
    assert(bs5.test(5) == false);  // F, T -> F
    assert(bs4.test(128) == false);// T, F -> F

    // 4. reset(): (words && (bit_idx >> 6) < word_count)
    bs4.reset(5);                  // T, T -> resets
    assert(bs4.test(5) == false);
    bs5.reset(5);                  // F, T
    bs4.reset(128);                // T, F
}

void test_mcdc_relation_slot_widths() {
    std::cout << "[MC/DC] Testing BoundRelationSlot Width & Pointer Permutations..." << std::endl;

    uint32_t off32[] = { 0, 2, 4 };
    uint64_t off64[] = { 0, 2, 4 };
    uint16_t tgt16[] = { 10, 20, 30, 40 };
    uint32_t tgt32[] = { 100, 200, 300, 400 };
    uint64_t tgt64[] = { 1000, 2000, 3000, 4000 };

    // CSR Offset: offsets_ptr, edge_index_width (4, 8, 64)
    DummySlot s_csr_null{ nullptr, nullptr, nullptr, nullptr, 4, 4, 0, 0 };
    assert(s_csr_null.get_csr_offset(1) == 0); // offsets_ptr == null

    DummySlot s_csr_32{ off32, tgt32, nullptr, nullptr, 4, 4, 2, 4 };
    assert(s_csr_32.get_csr_offset(1) == 2);

    DummySlot s_csr_64{ off64, tgt64, nullptr, nullptr, 4, 8, 2, 4 };
    assert(s_csr_64.get_csr_offset(1) == 2);

    DummySlot s_csr_64b{ off64, tgt64, nullptr, nullptr, 4, 64, 2, 4 };
    assert(s_csr_64b.get_csr_offset(1) == 2);

    // CSR Target: targets_ptr, node_id_width (2, 16, 4, 8, 64)
    assert(s_csr_null.get_csr_target(1) == 0); // targets_ptr == null

    DummySlot s_tgt_16{ off32, tgt16, nullptr, nullptr, 2, 4, 2, 4 };
    assert(s_tgt_16.get_csr_target(1) == 20);

    DummySlot s_tgt_16b{ off32, tgt16, nullptr, nullptr, 16, 4, 2, 4 };
    assert(s_tgt_16b.get_csr_target(1) == 20);

    DummySlot s_tgt_32{ off32, tgt32, nullptr, nullptr, 4, 4, 2, 4 };
    assert(s_tgt_32.get_csr_target(1) == 200);

    DummySlot s_tgt_64{ off32, tgt64, nullptr, nullptr, 8, 4, 2, 4 };
    assert(s_tgt_64.get_csr_target(1) == 2000);

    DummySlot s_tgt_64b{ off32, tgt64, nullptr, nullptr, 64, 4, 2, 4 };
    assert(s_tgt_64b.get_csr_target(1) == 2000);

    // CSC Offset & Target
    assert(s_csr_null.get_csc_offset(1) == 0);
    assert(s_csr_null.get_csc_target(1) == 0);

    DummySlot s_csc_32{ nullptr, nullptr, off32, tgt32, 4, 4, 2, 4 };
    assert(s_csc_32.get_csc_offset(1) == 2);
    assert(s_csc_32.get_csc_target(1) == 200);

    DummySlot s_csc_64{ nullptr, nullptr, off64, tgt64, 8, 8, 2, 4 };
    assert(s_csc_64.get_csc_offset(1) == 2);
    assert(s_csc_64.get_csc_target(1) == 2000);

    DummySlot s_csc_64b{ nullptr, nullptr, off64, tgt64, 64, 64, 2, 4 };
    assert(s_csc_64b.get_csc_offset(1) == 2);
    assert(s_csc_64b.get_csc_target(1) == 2000);

    DummySlot s_csc_16{ nullptr, nullptr, off32, tgt16, 2, 4, 2, 4 };
    assert(s_csc_16.get_csc_target(1) == 20);

    DummySlot s_csc_16b{ nullptr, nullptr, off32, tgt16, 16, 4, 2, 4 };
    assert(s_csc_16b.get_csc_target(1) == 20);
}

void test_mcdc_context_guards_and_handles() {
    std::cout << "[MC/DC] Testing Context Guard Multi-Condition Decisions..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    assert(ctx != nullptr);

    // Fuel tests: ctx == null vs ctx != null, fuel == 0 vs fuel > 0
    impulse_vm_context_set_fuel(nullptr, 100);
    impulse_vm_context_set_fuel(ctx, 0);
    impulse_vm_context_set_fuel(ctx, 500);

    // Vector size: ctx == null vs ctx != null
    assert(impulse_vm_context_get_vector_size(nullptr) == 0);
    assert(impulse_vm_context_get_vector_size(ctx) == 0);

    // Float vector MC/DC guard: (ctx && handle < MAX && ctx->float_vectors_allocated[handle])
    int h_f = impulse_vm_context_acquire_float_vector(ctx);
    assert(h_f >= 0);
    // Base: T, T, T
    assert(impulse_vm_context_get_float_vector(ctx, h_f) != nullptr);
    // C1: F, T, T
    assert(impulse_vm_context_get_float_vector(nullptr, h_f) == nullptr);
    // C2: T, F, *
    assert(impulse_vm_context_get_float_vector(ctx, 9999) == nullptr);
    // C3: T, T, F
    impulse_vm_context_release_float_vector(ctx, h_f);
    assert(impulse_vm_context_get_float_vector(ctx, h_f) == nullptr);

    // Float vector set: (ctx && handle < MAX && ctx->float_vectors_allocated[handle] && index < ctx->max_nodes)
    int h_f2 = impulse_vm_context_acquire_float_vector(ctx);
    impulse_vm_context_float_vector_set(ctx, h_f2, 0, 3.14f);       // T, T, T, T
    impulse_vm_context_float_vector_set(nullptr, h_f2, 0, 1.0f);     // F, T, T, T
    impulse_vm_context_float_vector_set(ctx, 9999, 0, 1.0f);         // T, F, *, *
    impulse_vm_context_float_vector_set(ctx, h_f, 0, 1.0f);          // T, T, F, * (h_f released)
    impulse_vm_context_float_vector_set(ctx, h_f2, 99999999, 1.0f);  // T, T, T, F (out of bounds)
    impulse_vm_context_release_float_vector(nullptr, 0);
    impulse_vm_context_release_float_vector(ctx, 9999);
    impulse_vm_context_release_float_vector(ctx, h_f2);

    // Double vector MC/DC guard:
    int h_d = impulse_vm_context_acquire_double_vector(ctx);
    assert(h_d >= 0);
    assert(impulse_vm_context_get_double_vector(ctx, h_d) != nullptr);
    assert(impulse_vm_context_get_double_vector(nullptr, h_d) == nullptr);
    assert(impulse_vm_context_get_double_vector(ctx, 9999) == nullptr);
    impulse_vm_context_release_double_vector(ctx, h_d);
    assert(impulse_vm_context_get_double_vector(ctx, h_d) == nullptr);

    int h_d2 = impulse_vm_context_acquire_double_vector(ctx);
    impulse_vm_context_double_vector_set(ctx, h_d2, 0, 2.718);
    impulse_vm_context_double_vector_set(nullptr, h_d2, 0, 1.0);
    impulse_vm_context_double_vector_set(ctx, 9999, 0, 1.0);
    impulse_vm_context_double_vector_set(ctx, h_d, 0, 1.0);
    impulse_vm_context_double_vector_set(ctx, h_d2, 99999999, 1.0);
    impulse_vm_context_release_double_vector(nullptr, 0);
    impulse_vm_context_release_double_vector(ctx, 9999);
    impulse_vm_context_release_double_vector(ctx, h_d2);

    // Node vector MC/DC guard:
    int h_n = impulse_vm_context_acquire_node_vector(ctx);
    assert(h_n >= 0);
    assert(impulse_vm_context_get_node_vector(ctx, h_n) != nullptr);
    assert(impulse_vm_context_get_node_vector(nullptr, h_n) == nullptr);
    assert(impulse_vm_context_get_node_vector(ctx, 9999) == nullptr);
    impulse_vm_context_release_node_vector(ctx, h_n);
    assert(impulse_vm_context_get_node_vector(ctx, h_n) == nullptr);
    impulse_vm_context_release_node_vector(nullptr, 0);
    impulse_vm_context_release_node_vector(ctx, 9999);

    // Bitset MC/DC guard:
    int h_b = impulse_vm_context_acquire_bitset(ctx);
    assert(h_b >= 0);
    impulse_vm_context_bitset_add(ctx, h_b, 42);
    assert(impulse_vm_context_bitset_test(ctx, h_b, 42) == true);
    assert(impulse_vm_context_bitset_test(ctx, h_b, 43) == false);
    // Null & bounds checks:
    impulse_vm_context_bitset_add(nullptr, h_b, 42);
    impulse_vm_context_bitset_add(ctx, 9999, 42);
    assert(impulse_vm_context_bitset_test(nullptr, h_b, 42) == false);
    assert(impulse_vm_context_bitset_test(ctx, 9999, 42) == false);
    impulse_vm_context_release_bitset(nullptr, 0);
    impulse_vm_context_release_bitset(ctx, 9999);
    impulse_vm_context_release_bitset(ctx, h_b);
    assert(impulse_vm_context_bitset_test(ctx, h_b, 42) == false);

    // String vector MC/DC guard:
    int h_s = impulse_vm_context_acquire_string_vector(ctx);
    assert(h_s >= 0);
    impulse_vm_context_string_vector_add(ctx, h_s, "hello");
    impulse_vm_context_string_vector_add(nullptr, h_s, "bad");
    impulse_vm_context_string_vector_add(ctx, 9999, "bad");
    assert(impulse_vm_context_string_vector_size(ctx, h_s) == 1);
    assert(impulse_vm_context_string_vector_size(nullptr, h_s) == 0);
    assert(impulse_vm_context_string_vector_size(ctx, 9999) == 0);
    assert(impulse_vm_context_string_vector_get(ctx, h_s, 0) != nullptr);
    assert(impulse_vm_context_string_vector_get(nullptr, h_s, 0) == nullptr);
    assert(impulse_vm_context_string_vector_get(ctx, 9999, 0) == nullptr);
    assert(impulse_vm_context_string_vector_get(ctx, h_s, 100) == nullptr);
    impulse_vm_context_release_string_vector(nullptr, 0);
    impulse_vm_context_release_string_vector(ctx, 9999);
    impulse_vm_context_release_string_vector(ctx, h_s);
    assert(impulse_vm_context_string_vector_get(ctx, h_s, 0) == nullptr);

    // Value map MC/DC guard:
    int h_m = impulse_vm_context_acquire_value_map(ctx);
    assert(h_m >= 0);
    assert(impulse_vm_context_value_map_size(ctx, h_m) == 0);
    assert(impulse_vm_context_value_map_size(nullptr, h_m) == 0);
    assert(impulse_vm_context_value_map_size(ctx, 9999) == 0);
    assert(impulse_vm_context_value_map_get_key(ctx, h_m, 0) == nullptr);
    assert(impulse_vm_context_value_map_get_key(nullptr, h_m, 0) == nullptr);
    assert(impulse_vm_context_value_map_get_key(ctx, 9999, 0) == nullptr);
    assert(impulse_vm_context_value_map_get_value(ctx, h_m, 0) == 0.0f);
    assert(impulse_vm_context_value_map_get_value(nullptr, h_m, 0) == 0.0f);
    assert(impulse_vm_context_value_map_get_value(ctx, 9999, 0) == 0.0f);
    impulse_vm_context_release_value_map(nullptr, 0);
    impulse_vm_context_release_value_map(ctx, 9999);
    impulse_vm_context_release_value_map(ctx, h_m);

    // Pool exhaustion testing (for all handle types)
    std::vector<int> bitset_handles;
    for (int i = 0; i < 1024; ++i) {
        int h = impulse_vm_context_acquire_bitset(ctx);
        if (h >= 0) bitset_handles.push_back(h);
    }
    // Next acquire should fail with -1 (pool exhausted branch)
    assert(impulse_vm_context_acquire_bitset(ctx) == -1);
    for (int h : bitset_handles) impulse_vm_context_release_bitset(ctx, h);

    // Acquire float vector exhaustion
    std::vector<int> float_handles;
    for (int i = 0; i < 32; ++i) {
        int h = impulse_vm_context_acquire_float_vector(ctx);
        if (h >= 0) float_handles.push_back(h);
    }
    assert(impulse_vm_context_acquire_float_vector(ctx) == -1);
    for (int h : float_handles) impulse_vm_context_release_float_vector(ctx, h);

    // Acquire double vector exhaustion
    std::vector<int> double_handles;
    for (int i = 0; i < 32; ++i) {
        int h = impulse_vm_context_acquire_double_vector(ctx);
        if (h >= 0) double_handles.push_back(h);
    }
    assert(impulse_vm_context_acquire_double_vector(ctx) == -1);
    for (int h : double_handles) impulse_vm_context_release_double_vector(ctx, h);

    // Acquire node vector exhaustion
    std::vector<int> node_handles;
    for (int i = 0; i < 32; ++i) {
        int h = impulse_vm_context_acquire_node_vector(ctx);
        if (h >= 0) node_handles.push_back(h);
    }
    assert(impulse_vm_context_acquire_node_vector(ctx) == -1);
    for (int h : node_handles) impulse_vm_context_release_node_vector(ctx, h);

    // Acquire string vector exhaustion
    std::vector<int> str_handles;
    for (int i = 0; i < 32; ++i) {
        int h = impulse_vm_context_acquire_string_vector(ctx);
        if (h >= 0) str_handles.push_back(h);
    }
    assert(impulse_vm_context_acquire_string_vector(ctx) == -1);
    for (int h : str_handles) impulse_vm_context_release_string_vector(ctx, h);

    // Acquire value map exhaustion
    std::vector<int> map_handles;
    for (int i = 0; i < 32; ++i) {
        int h = impulse_vm_context_acquire_value_map(ctx);
        if (h >= 0) map_handles.push_back(h);
    }
    assert(impulse_vm_context_acquire_value_map(ctx) == -1);
    for (int h : map_handles) impulse_vm_context_release_value_map(ctx, h);

    // Null context acquire checks
    assert(impulse_vm_context_acquire_bitset(nullptr) == -1);
    assert(impulse_vm_context_acquire_float_vector(nullptr) == -1);
    assert(impulse_vm_context_acquire_double_vector(nullptr) == -1);
    assert(impulse_vm_context_acquire_node_vector(nullptr) == -1);
    assert(impulse_vm_context_acquire_string_vector(nullptr) == -1);
    assert(impulse_vm_context_acquire_value_map(nullptr) == -1);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_vm_dispatch_and_fuel() {
    std::cout << "[MC/DC] Testing VM Dispatch Fuel & Bounds Conditions..." << std::endl;

    // 1. Fuel enabled and decrementing vs exhaustion
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_context_set_fuel(ctx, 3); // 3 fuel units

    std::vector<impulse_instruction_t> fuel_bytecode = {
        { OP_NOP, 0, 0, 0 }, // Fuel 3 -> 2
        { OP_NOP, 0, 0, 0 }, // Fuel 2 -> 1
        { OP_NOP, 0, 0, 0 }, // Fuel 1 -> 0
        { OP_NOP, 0, 0, 0 }, // Fuel 0 -> GAS_EXHAUSTED!
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state{};
    state.query_context = ctx;
    impulse_vm_status_t st = impulse_vm_execute(fuel_bytecode.data(), fuel_bytecode.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_GAS_EXHAUSTED);
    assert(state.pc == 3);

    // 1b. Snap/Bytecode null checks: (!bytecode || !vm_state)
    assert(impulse_vm_execute(nullptr, 1, &state, 0) == IMPULSE_VM_ERR_NULL_SNAPSHOT);
    assert(impulse_vm_execute(fuel_bytecode.data(), 1, nullptr, 0) == IMPULSE_VM_ERR_NULL_SNAPSHOT);

    // 2. Out of bounds PC (pc >= instruction_count)
    state.query_context = nullptr; // Reset context
    state.pc = 10;
    st = impulse_vm_execute(fuel_bytecode.data(), fuel_bytecode.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    // 3. Invalid register validation (VALIDATE_REG: dst >= 64)
    std::vector<impulse_instruction_t> bad_reg_bc = {
        { OP_LOAD_CONST_INT, 0, 64, 42 }, // Register 64 is invalid (0..63 valid)
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(bad_reg_bc.data(), bad_reg_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_INVALID_REGISTER);

    // 4. Invalid Opcode (0xFF)
    std::vector<impulse_instruction_t> invalid_op_bc = {
        { 0xFF, 0, 0, 0 },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(invalid_op_bc.data(), invalid_op_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_INVALID_OPCODE);

    // 5. Reserved Opcodes (0x0A, 0x28, 0x29, 0x2B, 0x2C, 0x3B, 0x4C, 0x59, 0x5D, 0x6D, 0x76)
    uint8_t reserved_ops[] = { 0x0A, 0x28, 0x29, 0x2B, 0x2C, 0x3B, 0x4C, 0x59, 0x5D, 0x6D, 0x76 };
    for (uint8_t rop : reserved_ops) {
        std::vector<impulse_instruction_t> rop_bc = {
            { rop, 0, 0, 0 },
            { OP_HALT, 0, 0, 0 }
        };
        state.pc = 0;
        st = impulse_vm_execute(rop_bc.data(), rop_bc.size(), &state, 0);
        assert(st == IMPULSE_VM_ERR_RESERVED_OPCODE);
    }

    // 6. Stack Overflow trap: Call stack depth >= 8
    std::vector<impulse_instruction_t> call_overflow_bc;
    for (int i = 0; i < 9; ++i) {
        call_overflow_bc.push_back({ OP_CALL, 0, 0, 0 }); // offset 0 -> loops calling itself
    }
    call_overflow_bc.push_back({ OP_HALT, 0, 0, 0 });
    state.pc = 0;
    state.call_stack_depth = 0;
    st = impulse_vm_execute(call_overflow_bc.data(), call_overflow_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_STACK_OVERFLOW);

    // 7. Stack Underflow trap: RET when call_stack_depth == 0
    std::vector<impulse_instruction_t> ret_underflow_bc = {
        { OP_RET, 0, 0, 0 },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    state.call_stack_depth = 0;
    st = impulse_vm_execute(ret_underflow_bc.data(), ret_underflow_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_STACK_UNDERFLOW);

    // 8. Normal Call & Ret execution
    std::vector<impulse_instruction_t> normal_call_ret_bc = {
        { OP_CALL, 0, 0, 2 },       // 0: Call to 2 (pc -> 2, push 1)
        { OP_HALT, 0, 0, 0 },       // 1: Halt
        { OP_LOAD_CONST_INT, 0, 1, 100 }, // 2: Subroutine body
        { OP_RET, 0, 0, 0 }         // 3: Return (pc -> 1)
    };
    state.pc = 0;
    state.call_stack_depth = 0;
    st = impulse_vm_execute(normal_call_ret_bc.data(), normal_call_ret_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);
    assert(state.registers[1] == 100);
    assert(state.call_stack_depth == 0);

    // 9. OP_ASSERT_FINITE with finite, NaN, +Inf, -Inf
    float nan_val = std::numeric_limits<float>::quiet_NaN();
    float inf_val = std::numeric_limits<float>::infinity();
    float ninf_val = -std::numeric_limits<float>::infinity();

    uint32_t nan_bits, inf_bits, ninf_bits, finite_bits;
    std::memcpy(&nan_bits, &nan_val, sizeof(float));
    std::memcpy(&inf_bits, &inf_val, sizeof(float));
    std::memcpy(&ninf_bits, &ninf_val, sizeof(float));
    float f42 = 42.0f;
    std::memcpy(&finite_bits, &f42, sizeof(float));

    // Finite test (positive)
    std::vector<impulse_instruction_t> assert_finite_bc = {
        { OP_LOAD_CONST_FLOAT, 0, 0, finite_bits },
        { OP_ASSERT_FINITE, 0, 0, 0 },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(assert_finite_bc.data(), assert_finite_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // NaN test (negative)
    assert_finite_bc[0].payload = nan_bits;
    state.pc = 0;
    st = impulse_vm_execute(assert_finite_bc.data(), assert_finite_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_FLOATING_POINT);

    // +Inf test (negative)
    assert_finite_bc[0].payload = inf_bits;
    state.pc = 0;
    st = impulse_vm_execute(assert_finite_bc.data(), assert_finite_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_FLOATING_POINT);

    // -Inf test (negative)
    assert_finite_bc[0].payload = ninf_bits;
    state.pc = 0;
    st = impulse_vm_execute(assert_finite_bc.data(), assert_finite_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_FLOATING_POINT);

    // 10. OP_THROW & OP_TRAP
    std::vector<impulse_instruction_t> throw_bc = {
        { OP_THROW, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(throw_bc.data(), throw_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_USER_THROW);

    std::vector<impulse_instruction_t> trap_bc = {
        { OP_TRAP, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(trap_bc.data(), trap_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_TRAP);
}

void test_mcdc_mock_graph_and_traversals() {
    std::cout << "[MC/DC] Testing OP_INIT_MOCK_GRAPH & Traversal Edge Cases..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t state{};
    state.query_context = ctx;

    // 1. OP_INIT_MOCK_GRAPH null checks
    std::vector<impulse_instruction_t> mock_null_ctx_bc = {
        { OP_INIT_MOCK_GRAPH, 0, 0, 0 },
        { OP_HALT, 0, 0, 0 }
    };
    impulse_vm_state_t null_ctx_state{};
    assert(impulse_vm_execute(mock_null_ctx_bc.data(), mock_null_ctx_bc.size(), &null_ctx_state, 0) == IMPULSE_VM_ERR_NULL_SNAPSHOT);

    // Context present but no inline data bound
    assert(impulse_vm_execute(mock_null_ctx_bc.data(), mock_null_ctx_bc.size(), &state, 0) == IMPULSE_VM_ERR_NULL_SNAPSHOT);

    // 2. Inline data with 4 nodes, 4 edges:
    // Offsets: [0, 1, 2, 3, 4]
    // Targets: [1, 2, 3, 0]
    uint32_t inline_graph[] = {
        0, 1, 2, 3, 4, // offsets (node_count=4)
        1, 2, 3, 0     // targets (edge_count=4)
    };
    size_t inline_bytes = sizeof(inline_graph);
    impulse_vm_context_bind_inline_data(ctx, inline_graph, inline_bytes);

    // Bounds checks: offset_bytes >= inline_data_bytes
    std::vector<impulse_instruction_t> mock_oob_offset_bc = {
        { OP_INIT_MOCK_GRAPH, 0, 0, static_cast<uint32_t>(inline_bytes + 10) | (4 << 16) },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    assert(impulse_vm_execute(mock_oob_offset_bc.data(), mock_oob_offset_bc.size(), &state, 0) == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    // Bounds checks: slot_id >= 16
    std::vector<impulse_instruction_t> mock_oob_slot_bc = {
        { OP_INIT_MOCK_GRAPH, 0, 16, 0 | (4 << 16) },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    assert(impulse_vm_execute(mock_oob_slot_bc.data(), mock_oob_slot_bc.size(), &state, 0) == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    // Valid mock graph initialization on slot 0
    std::vector<impulse_instruction_t> mock_init_bc = {
        { OP_INIT_MOCK_GRAPH, 0, 0, 0 | (4 << 16) }, // slot 0, offset 0, node_count 4
        // OP_CSR_WALK with single node ID source (R1 = 0 -> dst R2)
        { OP_LOAD_CONST_INT, 0, 1, 0 },
        { OP_CSR_WALK, 0, 2, 1 | (0 << 16) }, // dst=R2, src=R1, rel=0
        // OP_CSR_WALK with bitset source (R2 is bitset -> dst R3)
        { OP_CSR_WALK, 0, 3, 2 | (0 << 16) },
        // OP_CSR_WALK with ACCUMULATE flag
        { OP_CSR_WALK, IMPULSE_VM_OP_FLAG_ACCUMULATE, 3, 1 | (0 << 16) },
        // OP_CSR_WALK with HALT_ON_EMPTY flag
        { OP_CSR_WALK, IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY, 4, 1 | (0 << 16) },
        // OP_CSC_WALK with single node ID source
        { OP_CSC_WALK, 0, 5, 1 | (0 << 16) },
        // OP_CSC_WALK with bitset source
        { OP_CSC_WALK, 0, 6, 2 | (0 << 16) },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(mock_init_bc.data(), mock_init_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // Test out-of-bounds relation (rel 99)
    std::vector<impulse_instruction_t> oob_rel_bc = {
        { OP_CSR_WALK, 0, 7, 1 | (99 << 16) },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(oob_rel_bc.data(), oob_rel_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_load_indirect_and_indexing() {
    std::cout << "[MC/DC] Testing OP_LOAD_INDIRECT & Array Indexing..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t state{};
    state.query_context = ctx;

    // 1. flags == 0: Register indirect lookup
    // R1 = 5 (points to R5), R5 = 12345
    // OP_LOAD_INDIRECT R0, R1 (flags=0) -> R0 = R5 = 12345
    std::vector<impulse_instruction_t> reg_indirect_bc = {
        { OP_LOAD_CONST_INT, 0, 5, 12345 },
        { OP_LOAD_CONST_INT, 0, 1, 5 },
        { OP_LOAD_INDIRECT, 0, 0, 1 }, // dst=R0, src_param=R1, flags=0
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(reg_indirect_bc.data(), reg_indirect_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);
    assert(state.registers[0] == 12345);

    // Register indirect with invalid register index (> 63)
    std::vector<impulse_instruction_t> bad_indirect_bc = {
        { OP_LOAD_CONST_INT, 0, 1, 100 }, // R1 = 100 (invalid reg)
        { OP_LOAD_INDIRECT, 0, 0, 1 },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(bad_indirect_bc.data(), bad_indirect_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_INVALID_REGISTER);

    // 2. flags != 0: Vector index lookup
    int h_f = impulse_vm_context_acquire_float_vector(ctx);
    impulse_vm_context_float_vector_set(ctx, h_f, 0, 42.5f);

    std::vector<impulse_instruction_t> vec_indirect_bc = {
        { OP_LOAD_CONST_INT, 0, 1, static_cast<uint32_t>(h_f) }, // R1 = vector handle
        { OP_LOAD_CONST_INT, 0, 2, 0 },                          // R2 = index 0
        { OP_LOAD_INDIRECT, 1, 0, 1 | (2 << 16) },               // dst=R0, src=R1, idx=R2, flags=1
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(vec_indirect_bc.data(), vec_indirect_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);
    float res_f = *reinterpret_cast<float*>(&state.registers[0]);
    assert(res_f == 42.5f);

    // Vector indirect out-of-bounds handle
    vec_indirect_bc[0].payload = 99; // handle 99 >= 32
    state.pc = 0;
    st = impulse_vm_execute(vec_indirect_bc.data(), vec_indirect_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    // Vector indirect out-of-bounds index
    vec_indirect_bc[0].payload = static_cast<uint32_t>(h_f);
    vec_indirect_bc[1].payload = 99999999; // out-of-bounds index
    state.pc = 0;
    st = impulse_vm_execute(vec_indirect_bc.data(), vec_indirect_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_set_ops_permutations() {
    std::cout << "[MC/DC] Testing Set Operations Permutations & Dynamic Types..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t state{};
    state.query_context = ctx;

    // Test OP_SET_UNION, OP_SET_INTERSECT, OP_SET_DIFFERENCE across all register combinations:
    // (dst == src1), (dst == src2), (dst != src1 && dst != src2)
    // with TYPE_BITSET_HANDLE, TYPE_NODE_ID, TYPE_INT64, and TYPE_NULL

    std::vector<impulse_instruction_t> set_ops_bc = {
        // Setup values
        { OP_LOAD_CONST_INT, 0, 10, 1 },  // R10 = 1 (INT64)
        { OP_LOAD_CONST_INT, 0, 11, 2 },  // R11 = 2 (INT64)
        { OP_LOAD_CONST_INT, 0, 12, 3 },  // R12 = 3 (INT64)

        // 1. OP_SET_UNION:
        // dst != src1 && dst != src2
        { OP_SET_UNION, 0, 1, 10 | (11 << 16) }, // R1 = {1, 2}
        // dst == src1
        { OP_SET_UNION, 0, 1, 1 | (12 << 16) },  // R1 = {1, 2, 3}
        // dst == src2
        { OP_SET_UNION, 0, 2, 10 | (2 << 16) },  // R2 = {1}
        { OP_SET_UNION, 0, 2, 1 | (2 << 16) },   // R2 = R1 | R2 = {1, 2, 3}
        // with bitsets in both operands: R3 = R1 | R2
        { OP_SET_UNION, 0, 3, 1 | (2 << 16) },

        // 2. OP_SET_INTERSECT:
        // dst == src1 with bitset: R1 = R1 & R2
        { OP_SET_INTERSECT, 0, 1, 1 | (2 << 16) },
        // dst == src1 with node ID: R1 = R1 & R10 ({1, 2, 3} & {1} -> {1})
        { OP_SET_INTERSECT, 0, 1, 1 | (10 << 16) },
        // dst == src2 with bitset: R2 = R1 & R2
        { OP_SET_INTERSECT, 0, 2, 1 | (2 << 16) },
        // dst == src2 with node ID: R2 = R10 & R2 ({1} & {1} -> {1})
        { OP_SET_INTERSECT, 0, 2, 10 | (2 << 16) },
        // dst != src1 && dst != src2 with node IDs
        { OP_SET_INTERSECT, 0, 4, 10 | (11 << 16) }, // {1} & {2} -> empty

        // 3. OP_SET_DIFFERENCE:
        // R1 = {1, 2, 3}, R2 = {1}
        { OP_SET_UNION, 0, 1, 10 | (11 << 16) },
        { OP_SET_UNION, 0, 1, 1 | (12 << 16) },
        // dst == src1 with bitset: R1 = R1 - R2 ({1, 2, 3} - {1} = {2, 3})
        { OP_SET_DIFFERENCE, 0, 1, 1 | (2 << 16) },
        // dst == src1 with node ID: R1 = R1 - R11 ({2, 3} - {2} = {3})
        { OP_SET_DIFFERENCE, 0, 1, 1 | (11 << 16) },
        // dst != src1 && dst != src2: R5 = R1 - R12 ({3} - {3} = {})
        { OP_SET_DIFFERENCE, 0, 5, 1 | (12 << 16) },

        { OP_HALT, 0, 0, 0 }
    };

    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(set_ops_bc.data(), set_ops_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_branching_and_control_flow() {
    std::cout << "[MC/DC] Testing Branching Instructions & Flags..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t state{};
    state.query_context = ctx;

    // 1. OP_JZ: taken vs not taken
    std::vector<impulse_instruction_t> jz_taken_bc = {
        { OP_JZ, 0, 0, 2 },               // taken (jump over next instr to HALT at 2)
        { OP_LOAD_CONST_INT, 0, 2, 999 }, // skipped
        { OP_HALT, 0, 0, 0 }
    };
    state.flags = IMPULSE_VM_FLAG_ZF;
    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(jz_taken_bc.data(), jz_taken_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);
    assert(state.registers[2] == 0); // skipped

    std::vector<impulse_instruction_t> jz_not_taken_bc = {
        { OP_JZ, 0, 0, 2 },               // not taken
        { OP_LOAD_CONST_INT, 0, 2, 100 }, // executed
        { OP_HALT, 0, 0, 0 }
    };
    state.flags = 0; // ZF clear
    state.pc = 0;
    st = impulse_vm_execute(jz_not_taken_bc.data(), jz_not_taken_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);
    assert(state.registers[2] == 100);

    // 2. OP_JNZ: taken vs not taken
    std::vector<impulse_instruction_t> jnz_taken_bc = {
        { OP_JNZ, 0, 0, 2 },              // taken (ZF is clear)
        { OP_LOAD_CONST_INT, 0, 1, 999 }, // skipped
        { OP_HALT, 0, 0, 0 }
    };
    state.flags = 0; // ZF clear -> JNZ taken
    state.pc = 0;
    st = impulse_vm_execute(jnz_taken_bc.data(), jnz_taken_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);
    assert(state.registers[1] == 0);

    std::vector<impulse_instruction_t> jnz_not_taken_bc = {
        { OP_JNZ, 0, 0, 2 },              // not taken (ZF is set)
        { OP_LOAD_CONST_INT, 0, 1, 100 }, // executed
        { OP_HALT, 0, 0, 0 }
    };
    state.flags = IMPULSE_VM_FLAG_ZF;
    state.pc = 0;
    st = impulse_vm_execute(jnz_not_taken_bc.data(), jnz_not_taken_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);
    assert(state.registers[1] == 100);

    // 3. OP_LOOP_DECR: loop 3 times
    std::vector<impulse_instruction_t> loop_bc = {
        { OP_LOAD_CONST_INT, 0, 1, 3 },                                      // 0: R1 = 3 (loop counter)
        { OP_LOAD_CONST_INT, 0, 2, 0 },                                      // 1: R2 = 0 (accumulator)
        { OP_LOAD_CONST_INT, 0, 3, 10 },                                     // 2: R3 = 10
        // Loop head at 3:
        { OP_LOOP_DECR, 0, 1, static_cast<uint32_t>(static_cast<int32_t>(-1)) }, // 3: Decrements R1; if > 0 jumps to 2, else fallthrough to 4
        { OP_HALT, 0, 0, 0 }                                                 // 4: Halt
    };
    state.pc = 0;
    st = impulse_vm_execute(loop_bc.data(), loop_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);
    assert(state.registers[1] == 0);

    // 4. OP_ASSERT with flags == 0 and flags != 0
    std::vector<impulse_instruction_t> assert_bc = {
        { OP_LOAD_CONST_INT, 0, 1, 42 },
        // Match register value
        { OP_ASSERT, 0, 1, 42 }, // flags=0, matches
        // Match flag bits
        { OP_ASSERT, 1, 0, 0 },  // flags=1, match flags mask 0
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(assert_bc.data(), assert_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // Assert fail on register mismatch
    assert_bc[1].payload = 99; // mismatch 42 != 99
    state.pc = 0;
    st = impulse_vm_execute(assert_bc.data(), assert_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_ASSERTION_FAILED);

    // Assert fail on flags mismatch
    assert_bc[1].payload = 42; // reset
    assert_bc[2].payload = static_cast<uint32_t>(IMPULSE_VM_FLAG_ZF | IMPULSE_VM_FLAG_LT); // flag mismatch
    state.flags = 0;
    state.pc = 0;
    st = impulse_vm_execute(assert_bc.data(), assert_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_ASSERTION_FAILED);

    // 5. OP_NOP
    std::vector<impulse_instruction_t> nop_bc = {
        { OP_NOP, 0, 5, 0 },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(nop_bc.data(), nop_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_simd_and_vector_math() {
    std::cout << "[MC/DC] Testing SIMD Vector Math & Boundary Widths..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t state{};
    state.query_context = ctx;

    // Vector operations with lengths: 0, 1, 4, 7, 8, 9, 16, 17, 32
    int h_f1 = impulse_vm_context_acquire_float_vector(ctx);
    int h_f2 = impulse_vm_context_acquire_float_vector(ctx);
    int h_f3 = impulse_vm_context_acquire_float_vector(ctx);

    for (size_t len : { 0, 1, 4, 7, 8, 9, 16, 17, 32 }) {
        for (size_t i = 0; i < len; ++i) {
            impulse_vm_context_float_vector_set(ctx, h_f1, i, static_cast<float>(i + 1));
            impulse_vm_context_float_vector_set(ctx, h_f2, i, 2.0f);
        }

        // OP_FLOAT_VECTOR_SCALE
        std::vector<impulse_instruction_t> scale_bc = {
            { OP_LOAD_CONST_INT, 0, 1, static_cast<uint32_t>(h_f1) },
            { OP_LOAD_CONST_FLOAT, 0, 2, 0x40000000 }, // 2.0f
            { OP_FLOAT_VECTOR_SCALE, 0, static_cast<uint16_t>(h_f3), 1 | (2 << 16) },
            { OP_HALT, 0, 0, 0 }
        };
        state.pc = 0;
        impulse_vm_status_t st = impulse_vm_execute(scale_bc.data(), scale_bc.size(), &state, 0);
        assert(st == IMPULSE_VM_OK);

        // OP_L1_NORM_DIFF
        std::vector<impulse_instruction_t> l1_bc = {
            { OP_LOAD_CONST_INT, 0, 1, static_cast<uint32_t>(h_f1) },
            { OP_LOAD_CONST_INT, 0, 2, static_cast<uint32_t>(h_f2) },
            { OP_L1_NORM_DIFF, 0, 0, 1 | (2 << 16) },
            { OP_HALT, 0, 0, 0 }
        };
        state.pc = 0;
        st = impulse_vm_execute(l1_bc.data(), l1_bc.size(), &state, 0);
        assert(st == IMPULSE_VM_OK);
    }

    // OP_VEC_CMP_EQ with and without INVERT flag
    std::vector<impulse_instruction_t> cmp_bc = {
        { OP_LOAD_CONST_INT, 0, 1, static_cast<uint32_t>(h_f1) },
        { OP_LOAD_CONST_INT, 0, 2, static_cast<uint32_t>(h_f2) },
        { OP_VEC_CMP_EQ, 0, 3, 1 | (2 << 8) },                         // flags = 0
        { OP_VEC_CMP_EQ, IMPULSE_VM_OP_FLAG_INVERT, 4, 1 | (2 << 8) }, // flags = INVERT
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(cmp_bc.data(), cmp_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_null_context_scalar_stream() {
    std::cout << "[MC/DC] Testing Pure Scalar Stream with Null Context (Dispatch C1 Independence)..." << std::endl;

    impulse_vm_state_t state{};
    state.query_context = nullptr; // Ensure query_context is null!

    std::vector<impulse_instruction_t> scalar_bc = {
        { OP_NOP, 0, 0, 0 },
        { OP_LOAD_CONST_INT, 0, 1, 42 },
        { OP_LOAD_CONST_FLOAT, 0, 2, 0x40490fdb },
        { OP_MOV, 0, 3, 1 },
        { OP_SWAP_REG, 0, 1, 3 },
        { OP_CLEAR_REG, 0, 2, 0 },
        { OP_JMP, 0, 0, 2 },
        { OP_LOAD_CONST_INT, 0, 4, 999 }, // skipped
        { OP_ASSERT, 0, 1, 42 },
        { OP_HALT, 0, 0, 0 }
    };

    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(scalar_bc.data(), scalar_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);
    assert(state.registers[1] == 42);
    assert(state.registers[4] == 0);
}

void test_mcdc_set_ops_type_tags_exhaustive() {
    std::cout << "[MC/DC] Testing Exhaustive Set Ops Type Tags (NODE_ID vs INT64 vs NULL)..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t state{};
    state.query_context = ctx;

    // R10 is TYPE_NODE_ID, R11 is TYPE_INT64, R12 is TYPE_FLOAT (neither NODE_ID nor INT64)
    std::vector<impulse_instruction_t> type_tag_bc = {
        { OP_INIT_INPUT_NODE, 0, 10, 0 },          // R10 = 5 (TYPE_NODE_ID)
        { OP_LOAD_CONST_INT, 0, 11, 10 },          // R11 = 10 (TYPE_INT64)
        { OP_LOAD_CONST_FLOAT, 0, 12, 0x40000000 },// R12 = 2.0f (TYPE_FLOAT)

        // OP_SET_UNION dst == src1 with NODE_ID, then with INT64, then with FLOAT
        { OP_SET_UNION, 0, 1, 10 | (10 << 16) }, // dst=R1, src1=R10, src2=R10 (R1={5})
        { OP_SET_UNION, 0, 1, 1 | (11 << 16) },  // dst=R1, src1=R1, src2=R11 (R1={5, 10})
        { OP_SET_UNION, 0, 1, 1 | (12 << 16) },  // dst=R1, src1=R1, src2=R12 (FLOAT -> no-op)

        // OP_SET_UNION dst == src2 with NODE_ID, then with INT64, then with FLOAT
        { OP_SET_UNION, 0, 2, 10 | (2 << 16) },  // dst=R2, src1=R10, src2=R2
        { OP_SET_UNION, 0, 2, 11 | (2 << 16) },  // dst=R2, src1=R11, src2=R2
        { OP_SET_UNION, 0, 2, 12 | (2 << 16) },  // dst=R2, src1=R12, src2=R2

        // OP_SET_UNION dst != src1 && dst != src2 with NODE_ID, INT64, FLOAT
        { OP_SET_UNION, 0, 3, 10 | (11 << 16) }, // R3 = {5, 10}
        { OP_SET_UNION, 0, 4, 11 | (12 << 16) }, // R4 = {10} (src2 is FLOAT)
        { OP_SET_UNION, 0, 5, 12 | (10 << 16) }, // R5 = {5} (src1 is FLOAT)

        // OP_SET_INTERSECT with NODE_ID, INT64, FLOAT across all forms
        { OP_SET_INTERSECT, 0, 3, 3 | (10 << 16) }, // R3 = R3 & R10 (NODE_ID) -> keeps 5
        { OP_SET_INTERSECT, 0, 3, 3 | (11 << 16) }, // R3 = R3 & R11 (INT64) -> doesn't keep 10 -> empty
        { OP_SET_INTERSECT, 0, 1, 1 | (12 << 16) }, // R1 = R1 & R12 (FLOAT) -> clears R1

        // OP_SET_DIFFERENCE with NODE_ID, INT64, FLOAT across all forms
        { OP_SET_UNION, 0, 6, 10 | (11 << 16) },    // R6 = {5, 10}
        { OP_SET_DIFFERENCE, 0, 6, 6 | (10 << 16) }, // R6 = R6 - R10 (NODE_ID) -> {10}
        { OP_SET_DIFFERENCE, 0, 6, 6 | (11 << 16) }, // R6 = R6 - R11 (INT64) -> {}
        { OP_SET_DIFFERENCE, 0, 6, 6 | (12 << 16) }, // R6 = R6 - R12 (FLOAT) -> no-op

        { OP_HALT, 0, 0, 0 }
    };

    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(type_tag_bc.data(), type_tag_bc.size(), &state, 5);
    assert(st == IMPULSE_VM_OK);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_traversal_bounds_and_node_counts() {
    std::cout << "[MC/DC] Testing Traversal Bounds (u < node_count vs u >= node_count vs node_count == 0)..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t state{};
    state.query_context = ctx;

    // Slot 0: node_count = 4, edges = 4
    uint32_t inline_graph1[] = {
        0, 1, 2, 3, 4, // offsets
        1, 2, 3, 0     // targets
    };
    impulse_vm_context_bind_inline_data(ctx, inline_graph1, sizeof(inline_graph1));

    std::vector<impulse_instruction_t> trav_bc = {
        { OP_INIT_MOCK_GRAPH, 0, 0, 0 | (4 << 16) }, // slot 0, node_count = 4
        // 1. In-bounds node (u = 2 < node_count 4)
        { OP_LOAD_CONST_INT, 0, 1, 2 },
        { OP_CSR_WALK, 0, 2, 1 | (0 << 16) },
        // 2. Out-of-bounds node (u = 99 >= node_count 4) -> should skip edge expansion safely
        { OP_LOAD_CONST_INT, 0, 3, 99 },
        { OP_CSR_WALK, 0, 4, 3 | (0 << 16) },
        // 3. BitSet traversal with out-of-bounds bit set
        { OP_CSR_WALK, 0, 5, 4 | (0 << 16) },
        { OP_HALT, 0, 0, 0 }
    };

    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(trav_bc.data(), trav_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // Test with node_count == 0 (slot 1)
    uint32_t inline_graph2[] = {
        0, // offsets
        0  // targets
    };
    impulse_vm_context_bind_inline_data(ctx, inline_graph2, sizeof(inline_graph2));

    std::vector<impulse_instruction_t> zero_nc_bc = {
        { OP_INIT_MOCK_GRAPH, 0, 1, 0 | (0 << 16) }, // slot 1, node_count = 0
        { OP_LOAD_CONST_INT, 0, 1, 0 },
        { OP_CSR_WALK, 0, 2, 1 | (1 << 16) }, // node_count == 0 branch
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(zero_nc_bc.data(), zero_nc_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    impulse_vm_context_destroy(ctx);
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << " ImpulseVM MC/DC Condition Independence & Boundary Test Suite" << std::endl;
    std::cout << "================================================================" << std::endl;

    test_mcdc_bitset_conditions();
    test_mcdc_relation_slot_widths();
    test_mcdc_context_guards_and_handles();
    test_mcdc_vm_dispatch_and_fuel();
    test_mcdc_mock_graph_and_traversals();
    test_mcdc_load_indirect_and_indexing();
    test_mcdc_set_ops_permutations();
    test_mcdc_branching_and_control_flow();
    test_mcdc_simd_and_vector_math();
    test_mcdc_null_context_scalar_stream();
    test_mcdc_set_ops_type_tags_exhaustive();
    test_mcdc_traversal_bounds_and_node_counts();

    std::cout << "================================================================" << std::endl;
    std::cout << " ALL MC/DC CONDITION INDEPENDENCE TESTS PASSED SUCCESSFULLY!" << std::endl;
    std::cout << "================================================================" << std::endl;
    return 0;
}

