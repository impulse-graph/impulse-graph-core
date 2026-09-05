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
    if (st != IMPULSE_VM_OK) { printf("DEBUG STATUS: %d\n", st); } assert(st == IMPULSE_VM_OK);
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

    // 1. Purely Scalar Stream with Null Context (No bitsets or vector buffers needed)
    std::vector<impulse_instruction_t> scalar_bc = {
        { OP_NOP, 0, 0, 0 },
        { OP_INIT_INPUT_NODE, 0, 0, 0 }, // R0 = input_param (7)
        { OP_LOAD_CONST_INT, 0, 1, 42 },
        { OP_LOAD_CONST_INT, 0, 2, 10 },
        { OP_LOAD_CONST_FLOAT, 0, 3, 0x40490fdb },
        { OP_LOAD_CONST_STR_PREFIX, 0, 4, 0x41424344 },
        { OP_MOV, 0, 5, 1 },
        { OP_SWAP_REG, 0, 1, 5 },
        { OP_CLEAR_REG, 0, 5, 0 },

        // Subroutine and frames (R13 -> R1 in callee window)
        { OP_LOAD_CONST_INT, 0, 13, 42 },// 9
        { OP_ENTER_FRAME, 0, 0, 0 },     // 10
        { OP_CALL, 0, 0, 14 },            // 11: call subroutine at 14 (return to 12)
        { OP_LEAVE_FRAME, 0, 0, 0 },     // 12: executes on return
        { OP_JMP, 0, 0, 2 },              // 13: relative jump +2 (to 15)
        // Subroutine target
        { OP_RET, 0, 0, 0 },             // 14: ret
        // Continuation
        { OP_SET_MAX_DOP, 0, 14, 4 },    // 15
        { OP_ALLOC_SCRATCH, 0, 0, 1024 }, // 16
        { OP_ASSERT_SCRATCH_BYTES, 0, 0, 512 }, // 17
        { OP_ASSERT, 0, 1, 42 },         // 18: R1 was passed R13 = 42
        { OP_HALT, 0, 0, 0 }             // 19
    };

    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(scalar_bc.data(), scalar_bc.size(), &state, 7);
    if (st != IMPULSE_VM_OK) {
        printf("DEBUG STATUS: %d, pc=%u\n", st, state.pc);
    }
    assert(st == IMPULSE_VM_OK);
    assert(state.registers[1] == 42);

    // Test graceful null-context trap on bitset/vector opcodes
    std::vector<impulse_instruction_t> vec_null_bc = {
        { OP_VEC_CMP_EQ, 0, 6, 1 | (2 << 16) },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(vec_null_bc.data(), vec_null_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    // 2. Transcendental Math Suite with Null Context
    std::vector<impulse_instruction_t> math_null_bc = {
        { OP_LOAD_CONST_FLOAT, 0, 1, 0x40800000 }, // 4.0f
        { OP_LOAD_CONST_FLOAT, 0, 2, 0x40000000 }, // 2.0f
        { OP_LOAD_CONST_FLOAT, 0, 3, 0x3f000000 }, // 0.5f

        { OP_VEC_MATH_UNARY, 0, 4, 1 | (MATH_FUNC_ABS << 16) },
        { OP_VEC_MATH_UNARY, 0, 5, 1 | (MATH_FUNC_SQRT << 16) },
        { OP_VEC_MATH_UNARY, 0, 6, 1 | (MATH_FUNC_RSQRT << 16) },
        { OP_VEC_MATH_UNARY, 0, 7, 1 | (MATH_FUNC_CBRT << 16) },
        { OP_VEC_MATH_BINARY, 0, 8, 1 | (2 << 8) | (MATH_FUNC_POW << 16) },
        { OP_VEC_MATH_BINARY, 0, 9, 1 | (2 << 8) | (MATH_FUNC_HYPOT << 16) },
        { OP_VEC_MATH_TERNARY, 0, 10, 1 | (2 << 8) | (3 << 16) | (MATH_FUNC_LERP << 24) },

        { OP_VEC_MATH_UNARY, 0, 11, 2 | (MATH_FUNC_EXP << 16) },
        { OP_VEC_MATH_UNARY, 0, 12, 2 | (MATH_FUNC_EXP2 << 16) },
        { OP_VEC_MATH_UNARY, 0, 13, 2 | (MATH_FUNC_EXP10 << 16) },
        { OP_VEC_MATH_UNARY, 0, 14, 2 | (MATH_FUNC_EXPM1 << 16) },
        { OP_VEC_MATH_UNARY, 0, 15, 2 | (MATH_FUNC_LOG << 16) },
        { OP_VEC_MATH_UNARY, 0, 16, 2 | (MATH_FUNC_LOG2 << 16) },
        { OP_VEC_MATH_UNARY, 0, 17, 2 | (MATH_FUNC_LOG10 << 16) },
        { OP_VEC_MATH_UNARY, 0, 18, 2 | (MATH_FUNC_LOG1P << 16) },

        { OP_VEC_MATH_UNARY, 0, 19, 3 | (MATH_FUNC_SIN << 16) },
        { OP_VEC_MATH_UNARY, 0, 20, 3 | (MATH_FUNC_COS << 16) },
        { OP_VEC_MATH_UNARY, 0, 21, 3 | (MATH_FUNC_TAN << 16) },
        { OP_VEC_MATH_UNARY, 0, 22, 3 | (MATH_FUNC_ASIN << 16) },
        { OP_VEC_MATH_UNARY, 0, 23, 3 | (MATH_FUNC_ACOS << 16) },
        { OP_VEC_MATH_UNARY, 0, 24, 3 | (MATH_FUNC_ATAN << 16) },
        { OP_VEC_MATH_BINARY, 0, 25, 1 | (2 << 8) | (MATH_FUNC_ATAN2 << 16) },
        { OP_VEC_MATH_UNARY, 0, 26, 3 | (MATH_FUNC_SINC << 16) },

        { OP_VEC_MATH_UNARY, 0, 27, 3 | (MATH_FUNC_SINH << 16) },
        { OP_VEC_MATH_UNARY, 0, 28, 3 | (MATH_FUNC_COSH << 16) },
        { OP_VEC_MATH_UNARY, 0, 29, 3 | (MATH_FUNC_TANH << 16) },
        { OP_VEC_MATH_UNARY, 0, 30, 3 | (MATH_FUNC_ASINH << 16) },
        { OP_VEC_MATH_UNARY, 0, 31, 1 | (MATH_FUNC_ACOSH << 16) },
        { OP_VEC_MATH_UNARY, 0, 32, 3 | (MATH_FUNC_ATANH << 16) },

        { OP_VEC_MATH_UNARY, 0, 33, 3 | (MATH_FUNC_SIGMOID << 16) },
        { OP_VEC_MATH_UNARY, 0, 34, 3 | (MATH_FUNC_SILU << 16) },
        { OP_VEC_MATH_UNARY, 0, 35, 3 | (MATH_FUNC_GELU << 16) },
        { OP_VEC_MATH_UNARY, 0, 36, 3 | (MATH_FUNC_RELU << 16) },
        { OP_VEC_MATH_UNARY, 0, 37, 3 | (MATH_FUNC_LEAKY_RELU << 16) },
        { OP_VEC_MATH_UNARY, 0, 38, 3 | (MATH_FUNC_ERF << 16) },
        { OP_VEC_MATH_UNARY, 0, 39, 3 | (MATH_FUNC_ERFC << 16) },
        { OP_VEC_MATH_UNARY, 0, 40, 3 | (MATH_FUNC_SOFTPLUS << 16) },

        { OP_VEC_MATH_UNARY, 0, 41, 1 | (MATH_FUNC_FLOOR << 16) },
        { OP_VEC_MATH_UNARY, 0, 42, 1 | (MATH_FUNC_CEIL << 16) },
        { OP_VEC_MATH_UNARY, 0, 43, 1 | (MATH_FUNC_ROUND << 16) },
        { OP_VEC_MATH_UNARY, 0, 44, 1 | (MATH_FUNC_TRUNC << 16) },

        { OP_HALT, 0, 0, 0 }
    };

    state = impulse_vm_state_t{};
    state.query_context = nullptr;
    state.pc = 0;
    st = impulse_vm_execute(math_null_bc.data(), math_null_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_NULL_SNAPSHOT);
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

void test_mcdc_opcodes_roaring_and_boolean() {
    std::cout << "[MC/DC] Testing Roaring Bitmaps & Boolean SIMD Masks..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t state{};
    state.query_context = ctx;

    int h1 = impulse_vm_context_acquire_bitset(ctx);
    int h2 = impulse_vm_context_acquire_bitset(ctx);
    impulse_vm_context_bitset_add(ctx, h1, 10);
    impulse_vm_context_bitset_add(ctx, h1, 20);
    impulse_vm_context_bitset_add(ctx, h2, 20);
    impulse_vm_context_bitset_add(ctx, h2, 30);

    // 1. OP_ROARING_BITMAP_AND across {T,T}, {T,F}, {F,T}
    std::vector<impulse_instruction_t> roaring_bc = {
        // Setup registers: R1 = h1 (bitset), R2 = h2 (bitset), R3 = 99 (INT64)
        { OP_LOAD_CONST_INT, 0, 3, 99 },
        // {T, T}: both are bitsets -> R10 = R1 & R2 ({20})
        { OP_ROARING_BITMAP_AND, 0, 10, static_cast<uint32_t>(h1 | (h2 << 16)) },
        // {T, F}: src1 is bitset, src2 is INT64
        { OP_ROARING_BITMAP_AND, 0, 11, static_cast<uint32_t>(h1 | (3 << 16)) },
        // {F, T}: src1 is INT64, src2 is bitset
        { OP_ROARING_BITMAP_AND, 0, 12, static_cast<uint32_t>(3 | (h2 << 16)) },

        // 2. OP_ROARING_BITMAP_OR across {T,T}, {T,F}, {F,T}
        { OP_ROARING_BITMAP_OR, 0, 13, static_cast<uint32_t>(h1 | (h2 << 16)) },
        { OP_ROARING_BITMAP_OR, 0, 14, static_cast<uint32_t>(h1 | (3 << 16)) },
        { OP_ROARING_BITMAP_OR, 0, 15, static_cast<uint32_t>(3 | (h2 << 16)) },

        // 3. OP_ROARING_BITMAP_AND_NOT across {T,T}, {T,F}, {F,T}
        { OP_ROARING_BITMAP_AND_NOT, 0, 16, static_cast<uint32_t>(h1 | (h2 << 16)) },
        { OP_ROARING_BITMAP_AND_NOT, 0, 17, static_cast<uint32_t>(h1 | (3 << 16)) },
        { OP_ROARING_BITMAP_AND_NOT, 0, 18, static_cast<uint32_t>(3 | (h2 << 16)) },

        // 4. OP_MASK_AND, OP_MASK_OR, OP_MASK_NOT
        { OP_MASK_AND, 0, 20, static_cast<uint32_t>(h1 | (h2 << 8)) },
        { OP_MASK_OR, 0, 21, static_cast<uint32_t>(h1 | (h2 << 8)) },
        { OP_MASK_NOT, 0, 22, static_cast<uint32_t>(h1) },

        { OP_HALT, 0, 0, 0 }
    };

    state.registers[1] = static_cast<uint64_t>(h1);
    state.register_types[1] = TYPE_BITSET_HANDLE;
    state.registers[2] = static_cast<uint64_t>(h2);
    state.register_types[2] = TYPE_BITSET_HANDLE;

    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(roaring_bc.data(), roaring_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // 5. OP_VEC_BLEND with float and double vectors
    int h_f1 = impulse_vm_context_acquire_float_vector(ctx);
    int h_f2 = impulse_vm_context_acquire_float_vector(ctx);
    int h_d1 = impulse_vm_context_acquire_double_vector(ctx);
    int h_d2 = impulse_vm_context_acquire_double_vector(ctx);

    impulse_vm_context_float_vector_set(ctx, h_f1, 0, 10.0f);
    impulse_vm_context_float_vector_set(ctx, h_f2, 0, 20.0f);
    impulse_vm_context_double_vector_set(ctx, h_d1, 0, 100.0);
    impulse_vm_context_double_vector_set(ctx, h_d2, 0, 200.0);

    std::vector<impulse_instruction_t> blend_bc = {
        // Float blend: R10 = mask ? R1 : R2
        { OP_VEC_BLEND, 0, 10, static_cast<uint32_t>(1 | (h_f1 << 8) | (h_f2 << 16)) },
        // Double blend: R11 = mask ? R3 : R4
        { OP_VEC_BLEND, 0, 11, static_cast<uint32_t>(1 | (3 << 8) | (4 << 16)) },
        { OP_HALT, 0, 0, 0 }
    };
    state.registers[1] = static_cast<uint64_t>(h1); // mask
    state.register_types[1] = TYPE_BITSET_HANDLE;
    state.registers[h_f1] = static_cast<uint64_t>(h_f1);
    state.register_types[h_f1] = TYPE_FLOAT_VECTOR;
    state.registers[h_f2] = static_cast<uint64_t>(h_f2);
    state.register_types[h_f2] = TYPE_FLOAT_VECTOR;
    state.registers[3] = static_cast<uint64_t>(h_d1);
    state.register_types[3] = TYPE_DOUBLE_VECTOR;
    state.registers[4] = static_cast<uint64_t>(h_d2);
    state.register_types[4] = TYPE_DOUBLE_VECTOR;

    state.pc = 0;
    st = impulse_vm_execute(blend_bc.data(), blend_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_opcodes_vector_math_unary_binary_ternary() {
    std::cout << "[MC/DC] Testing Vector Math Unary, Binary, Ternary & Between..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t state{};
    state.query_context = ctx;

    int h_f1 = impulse_vm_context_acquire_float_vector(ctx);
    int h_f2 = impulse_vm_context_acquire_float_vector(ctx);
    int h_f3 = impulse_vm_context_acquire_float_vector(ctx);
    int h_d1 = impulse_vm_context_acquire_double_vector(ctx);
    int h_d2 = impulse_vm_context_acquire_double_vector(ctx);
    int h_d3 = impulse_vm_context_acquire_double_vector(ctx);

    impulse_vm_context_float_vector_set(ctx, h_f1, 0, 4.0f);
    impulse_vm_context_float_vector_set(ctx, h_f2, 0, 2.0f);
    impulse_vm_context_float_vector_set(ctx, h_f3, 0, 1.0f);

    impulse_vm_context_double_vector_set(ctx, h_d1, 0, 9.0);
    impulse_vm_context_double_vector_set(ctx, h_d2, 0, 3.0);
    impulse_vm_context_double_vector_set(ctx, h_d3, 0, 1.5);

    // 1. OP_VEC_MATH_UNARY: float and double, type_tag == 0 vs 1, dst allocated vs unallocated
    std::vector<impulse_instruction_t> unary_bc = {
        // Float unary (type_tag=0, dst unallocated)
        { OP_VEC_MATH_UNARY, 0, 10, static_cast<uint32_t>(0 | (1 << 8)) }, // sqrt(R1)
        // Float unary (type_tag=0, dst already allocated as FLOAT_VECTOR)
        { OP_VEC_MATH_UNARY, 0, 10, static_cast<uint32_t>(0 | (1 << 8)) },
        // Double unary (type_tag=1, dst unallocated)
        { OP_VEC_MATH_UNARY, 1, 11, static_cast<uint32_t>(0 | (3 << 8)) }, // sqrt(R3)
        // Double unary (type_tag=0, src is TYPE_DOUBLE_VECTOR)
        { OP_VEC_MATH_UNARY, 0, 12, static_cast<uint32_t>(0 | (3 << 8)) },
        { OP_HALT, 0, 0, 0 }
    };
    state.registers[1] = static_cast<uint64_t>(h_f1);
    state.register_types[1] = TYPE_FLOAT_VECTOR;
    state.registers[3] = static_cast<uint64_t>(h_d1);
    state.register_types[3] = TYPE_DOUBLE_VECTOR;

    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(unary_bc.data(), unary_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // 2. OP_VEC_MATH_BINARY: float and double
    std::vector<impulse_instruction_t> binary_bc = {
        // Float binary (func=0 add, type_tag=0)
        { OP_VEC_MATH_BINARY, 0, 10, static_cast<uint32_t>(0 | (1 << 8) | (2 << 16)) },
        // Float binary (dst already allocated)
        { OP_VEC_MATH_BINARY, 0, 10, static_cast<uint32_t>(0 | (1 << 8) | (2 << 16)) },
        // Double binary (func=0 add, type_tag=1)
        { OP_VEC_MATH_BINARY, 1, 11, static_cast<uint32_t>(0 | (3 << 8) | (4 << 16)) },
        // Double binary (func=0 add, type_tag=0, src is TYPE_DOUBLE_VECTOR)
        { OP_VEC_MATH_BINARY, 0, 12, static_cast<uint32_t>(0 | (3 << 8) | (4 << 16)) },
        { OP_HALT, 0, 0, 0 }
    };
    state.registers[2] = static_cast<uint64_t>(h_f2);
    state.register_types[2] = TYPE_FLOAT_VECTOR;
    state.registers[4] = static_cast<uint64_t>(h_d2);
    state.register_types[4] = TYPE_DOUBLE_VECTOR;

    state.pc = 0;
    st = impulse_vm_execute(binary_bc.data(), binary_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // 3. OP_VEC_MATH_TERNARY: float and double
    std::vector<impulse_instruction_t> ternary_bc = {
        // Float ternary (fma: R1 * R2 + R5)
        { OP_VEC_MATH_TERNARY, 0, 10, static_cast<uint32_t>(0 | (1 << 8) | (2 << 16) | (5 << 24)) },
        // Double ternary (fma: R3 * R4 + R6)
        { OP_VEC_MATH_TERNARY, 1, 11, static_cast<uint32_t>(0 | (3 << 8) | (4 << 16) | (6 << 24)) },
        { OP_HALT, 0, 0, 0 }
    };
    state.registers[5] = static_cast<uint64_t>(h_f3);
    state.register_types[5] = TYPE_FLOAT_VECTOR;
    state.registers[6] = static_cast<uint64_t>(h_d3);
    state.register_types[6] = TYPE_DOUBLE_VECTOR;

    state.pc = 0;
    st = impulse_vm_execute(ternary_bc.data(), ternary_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // 4. OP_VEC_CMP_BETWEEN: float and double
    // Elements: 0: 4.0 (in range [1.0, 5.0]), 1: 0.5 (< min), 2: 10.0 (> max)
    impulse_vm_context_float_vector_set(ctx, h_f1, 0, 4.0f);
    impulse_vm_context_float_vector_set(ctx, h_f1, 1, 0.5f);
    impulse_vm_context_float_vector_set(ctx, h_f1, 2, 10.0f);

    impulse_vm_context_double_vector_set(ctx, h_d1, 0, 4.0);
    impulse_vm_context_double_vector_set(ctx, h_d1, 1, 0.5);
    impulse_vm_context_double_vector_set(ctx, h_d1, 2, 10.0);

    std::vector<impulse_instruction_t> between_bc = {
        // Float between [R7, R8] where R7 = 1.0f, R8 = 5.0f
        { OP_LOAD_CONST_FLOAT, 0, 7, 0x3f800000 }, // 1.0f
        { OP_LOAD_CONST_FLOAT, 0, 8, 0x40a00000 }, // 5.0f
        { OP_VEC_CMP_BETWEEN, 0, 20, static_cast<uint32_t>(1 | (7 << 8) | (8 << 16)) },

        // Double between [R7, R8]
        { OP_VEC_CMP_BETWEEN, 0, 21, static_cast<uint32_t>(3 | (7 << 8) | (8 << 16)) },
        { OP_HALT, 0, 0, 0 }
    };
    state.registers[1] = static_cast<uint64_t>(h_f1);
    state.register_types[1] = TYPE_FLOAT_VECTOR;
    state.registers[3] = static_cast<uint64_t>(h_d1);
    state.register_types[3] = TYPE_DOUBLE_VECTOR;

    state.pc = 0;
    st = impulse_vm_execute(between_bc.data(), between_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_opcodes_assert_finite_exhaustive() {
    std::cout << "[MC/DC] Testing Exhaustive OP_ASSERT_FINITE (Floats, Doubles, Vectors & NaNs)..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t state{};
    state.query_context = ctx;

    // 1. TYPE_FLOAT: Finite vs NaN vs +Inf vs -Inf
    float f_finite = 42.0f;
    float f_nan = std::numeric_limits<float>::quiet_NaN();
    float f_inf = std::numeric_limits<float>::infinity();

    std::vector<impulse_instruction_t> assert_f_bc = {
        { OP_ASSERT_FINITE, 0, 1, 0 },
        { OP_HALT, 0, 0, 0 }
    };

    // Finite float -> OK
    state.registers[1] = *reinterpret_cast<uint32_t*>(&f_finite);
    state.register_types[1] = TYPE_FLOAT;
    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(assert_f_bc.data(), assert_f_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // NaN float -> ERR_FLOATING_POINT
    state.registers[1] = *reinterpret_cast<uint32_t*>(&f_nan);
    state.register_types[1] = TYPE_FLOAT;
    state.pc = 0;
    st = impulse_vm_execute(assert_f_bc.data(), assert_f_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_FLOATING_POINT);

    // Inf float -> ERR_FLOATING_POINT
    state.registers[1] = *reinterpret_cast<uint32_t*>(&f_inf);
    state.register_types[1] = TYPE_FLOAT;
    state.pc = 0;
    st = impulse_vm_execute(assert_f_bc.data(), assert_f_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_FLOATING_POINT);

    // 2. TYPE_DOUBLE: Finite vs NaN vs Inf
    double d_finite = 42.0;
    double d_nan = std::numeric_limits<double>::quiet_NaN();
    double d_inf = std::numeric_limits<double>::infinity();

    // Finite double -> OK
    state.registers[1] = *reinterpret_cast<uint64_t*>(&d_finite);
    state.register_types[1] = TYPE_DOUBLE;
    state.pc = 0;
    st = impulse_vm_execute(assert_f_bc.data(), assert_f_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // NaN double -> ERR_FLOATING_POINT
    state.registers[1] = *reinterpret_cast<uint64_t*>(&d_nan);
    state.register_types[1] = TYPE_DOUBLE;
    state.pc = 0;
    st = impulse_vm_execute(assert_f_bc.data(), assert_f_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_FLOATING_POINT);

    // Inf double -> ERR_FLOATING_POINT
    state.registers[1] = *reinterpret_cast<uint64_t*>(&d_inf);
    state.register_types[1] = TYPE_DOUBLE;
    state.pc = 0;
    st = impulse_vm_execute(assert_f_bc.data(), assert_f_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_FLOATING_POINT);

    // 3. TYPE_FLOAT_VECTOR: Finite vs NaN vs Inf vs Bad Handle
    int h_f = impulse_vm_context_acquire_float_vector(ctx);
    impulse_vm_context_float_vector_set(ctx, h_f, 0, 1.0f);
    impulse_vm_context_float_vector_set(ctx, h_f, 1, 2.0f);

    state.registers[1] = static_cast<uint64_t>(h_f);
    state.register_types[1] = TYPE_FLOAT_VECTOR;
    state.pc = 0;
    st = impulse_vm_execute(assert_f_bc.data(), assert_f_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // Float vector with NaN at index 1
    impulse_vm_context_float_vector_set(ctx, h_f, 1, f_nan);
    state.pc = 0;
    st = impulse_vm_execute(assert_f_bc.data(), assert_f_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_FLOATING_POINT);
    assert(state.registers[0] == 1); // error index

    // Float vector with Inf at index 0
    impulse_vm_context_float_vector_set(ctx, h_f, 0, f_inf);
    state.pc = 0;
    st = impulse_vm_execute(assert_f_bc.data(), assert_f_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_FLOATING_POINT);

    // Float vector bad handle (>= 8)
    state.registers[1] = 99;
    state.pc = 0;
    st = impulse_vm_execute(assert_f_bc.data(), assert_f_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    // 4. TYPE_DOUBLE_VECTOR: Finite vs NaN vs Inf vs Bad Handle
    int h_d = impulse_vm_context_acquire_double_vector(ctx);
    impulse_vm_context_double_vector_set(ctx, h_d, 0, 1.0);
    impulse_vm_context_double_vector_set(ctx, h_d, 1, 2.0);

    state.registers[1] = static_cast<uint64_t>(h_d);
    state.register_types[1] = TYPE_DOUBLE_VECTOR;
    state.pc = 0;
    st = impulse_vm_execute(assert_f_bc.data(), assert_f_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // Double vector with NaN at index 1
    impulse_vm_context_double_vector_set(ctx, h_d, 1, d_nan);
    state.pc = 0;
    st = impulse_vm_execute(assert_f_bc.data(), assert_f_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_FLOATING_POINT);

    // Double vector bad handle (>= 8)
    state.registers[1] = 99;
    state.pc = 0;
    st = impulse_vm_execute(assert_f_bc.data(), assert_f_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_opcodes_graph_traversal_specializations() {
    std::cout << "[MC/DC] Testing Traversal Specializations (CSR_DEGREE, HAS_*, STABLE, ADAPTIVE)..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t state{};
    state.query_context = ctx;

    // Slot 0: valid CSR and CSC (node_count = 4)
    uint32_t inline_graph[] = {
        0, 1, 2, 3, 4, // offsets
        1, 2, 3, 0     // targets
    };
    impulse_vm_context_bind_inline_data(ctx, inline_graph, sizeof(inline_graph));

    // Setup slot 0 with CSR and CSC
    std::vector<impulse_instruction_t> setup_bc = {
        { OP_INIT_MOCK_GRAPH, 0, 0, 0 | (4 << 16) },
        // 1. OP_HAS_CSR, OP_HAS_CSC, OP_HAS_COO, OP_HAS_KEY_CATALOG
        { OP_HAS_CSR, 0, 10, 0 },  // R10 = 1 (CSR present on rel 0)
        { OP_HAS_CSC, 0, 11, 0 },  // R11 = 1 (CSC present on rel 0)
        { OP_HAS_COO, 0, 12, 0 },  // R12 = 1 (COO present on rel 0)
        { OP_HAS_KEY_CATALOG, 0, 13, 0 }, // R13 = 1
        // On invalid relation 99
        { OP_HAS_CSR, 0, 14, 99 }, // R14 = 0 (ZF set)
        { OP_HAS_CSC, 0, 15, 99 }, // R15 = 0 (ZF set)
        { OP_HAS_COO, 0, 16, 99 }, // R16 = 0 (ZF set)

        // 2. OP_CSR_DEGREE
        { OP_LOAD_CONST_INT, 0, 1, 0 },  // R1 = 0 (in bounds)
        { OP_CSR_DEGREE, 0, 20, 1 | (0 << 16) }, // degree of node 0 -> R20 = 1
        { OP_LOAD_CONST_INT, 0, 2, 99 }, // R2 = 99 (out of bounds)
        { OP_CSR_DEGREE, 0, 21, 2 | (0 << 16) }, // degree of node 99 -> R21 = 0 (ZF set)

        // 3. OP_CSR_WALK_2HOP (src is implicitly R0, payload is rel1 | (rel2 << 16))
        { OP_LOAD_CONST_INT, 0, 0, 0 },  // R0 = 0 (scalar source)
        { OP_CSR_WALK_2HOP, 0, 25, 0 | (0 << 16) }, // dst=R25, rel1=0, rel2=0
        // Now R25 is a bitset handle. Set R0 = R25 for bitset source 2-hop:
        { OP_MOV, 0, 0, 25 },
        { OP_CSR_WALK_2HOP, 0, 26, 0 | (0 << 16) }, // dst=R26, bitset source

        // 4. OP_STABLE_CHECK
        // Equal bitsets -> ST flag set
        { OP_STABLE_CHECK, 0, 25, 25 },
        // Equal scalars
        { OP_STABLE_CHECK, 0, 1, 1 },
        // Unequal scalars
        { OP_STABLE_CHECK, 0, 1, 2 },

        // 5. OP_ADAPTIVE_WALK
        { OP_ADAPTIVE_WALK, 0, 27, 1 | (0 << 16) },  // scalar source R1
        { OP_ADAPTIVE_WALK, 0, 28, 25 | (0 << 16) }, // bitset source R25

        // 6. OP_ISLAND_DETECT
        { OP_ISLAND_DETECT, 0, 30, static_cast<uint32_t>(1 | (1 << 8) | (0 << 16)) }, // same_set
        { OP_ISLAND_DETECT, 0, 31, static_cast<uint32_t>(1 | (2 << 8) | (0 << 16)) }, // different sets

        { OP_HALT, 0, 0, 0 }
    };

    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(setup_bc.data(), setup_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);
    assert(state.registers[10] == 1);
    assert(state.registers[14] == 0);
    assert(state.registers[20] == 1);
    assert(state.registers[21] == 0);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_opcodes_key_mapping_and_attributes() {
    std::cout << "[MC/DC] Testing Key Mapping & Attribute Catalogs (MAP_KEYS, DENSE_TO_KEYS, VALUE_MAP)..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t state{};
    state.query_context = ctx;

    // 1. OP_LOAD_INLINE_ARRAY
    uint32_t inline_arr[] = { 111, 222, 333, 444 };
    impulse_vm_context_bind_inline_data(ctx, inline_arr, sizeof(inline_arr));

    std::vector<impulse_instruction_t> inline_bc = {
        { OP_LOAD_INLINE_ARRAY, 0, 1, 0 | (4 << 16) }, // load inline array to R1
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(inline_bc.data(), inline_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // Null inline data -> ERR_NULL_SNAPSHOT
    impulse_vm_context_bind_inline_data(ctx, nullptr, 0);
    state.pc = 0;
    st = impulse_vm_execute(inline_bc.data(), inline_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_NULL_SNAPSHOT);

    // Restore inline data
    impulse_vm_context_bind_inline_data(ctx, inline_arr, sizeof(inline_arr));

    // 2. OP_MAP_KEYS_TO_DENSE without attributes (fallback to single node 0)
    std::vector<impulse_instruction_t> map_keys_bc = {
        { OP_MAP_KEYS_TO_DENSE, 0, 5, 0 },
        { OP_MAP_DENSE_TO_KEYS, 0, 6, 5 | (0 << 8) },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(map_keys_bc.data(), map_keys_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // 3. OP_COLLECT_VALUE_MAP
    int h_val_f = impulse_vm_context_acquire_float_vector(ctx);
    int h_val_d = impulse_vm_context_acquire_double_vector(ctx);
    impulse_vm_context_float_vector_set(ctx, h_val_f, 0, 3.14f);
    impulse_vm_context_double_vector_set(ctx, h_val_d, 0, 2.718);

    std::vector<impulse_instruction_t> val_map_bc = {
        // Collect with float vector values
        { OP_COLLECT_VALUE_MAP, 0, 10, static_cast<uint32_t>(5 | (1 << 8) | (0 << 16)) },
        // Collect with double vector values
        { OP_COLLECT_VALUE_MAP, 0, 11, static_cast<uint32_t>(5 | (2 << 8) | (0 << 16)) },
        { OP_HALT, 0, 0, 0 }
    };
    state.registers[1] = static_cast<uint64_t>(h_val_f);
    state.register_types[1] = TYPE_FLOAT_VECTOR;
    state.registers[2] = static_cast<uint64_t>(h_val_d);
    state.register_types[2] = TYPE_DOUBLE_VECTOR;

    state.pc = 0;
    st = impulse_vm_execute(val_map_bc.data(), val_map_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_opcodes_filtered_and_advanced_traversals() {
    std::cout << "[MC/DC] Testing Filtered CSR, CSC Bottom-Up, Direct Store & Reductions..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t state{};
    state.query_context = ctx;

    uint32_t inline_graph[] = {
        0, 1, 2, 3, 4, // offsets
        1, 2, 3, 0     // targets
    };
    impulse_vm_context_bind_inline_data(ctx, inline_graph, sizeof(inline_graph));

    int h_f = impulse_vm_context_acquire_float_vector(ctx);
    int h_d = impulse_vm_context_acquire_double_vector(ctx);
    impulse_vm_context_float_vector_set(ctx, h_f, 0, 10.0f);
    impulse_vm_context_float_vector_set(ctx, h_f, 1, 5.0f);
    impulse_vm_context_double_vector_set(ctx, h_d, 0, 100.0);
    impulse_vm_context_double_vector_set(ctx, h_d, 1, 50.0);

    int h_src = impulse_vm_context_acquire_bitset(ctx);
    int h_filter = impulse_vm_context_acquire_bitset(ctx);
    int h_unv = impulse_vm_context_acquire_bitset(ctx);

    impulse_vm_context_bitset_add(ctx, h_src, 0);
    impulse_vm_context_bitset_add(ctx, h_filter, 1);
    impulse_vm_context_bitset_add(ctx, h_filter, 2);
    impulse_vm_context_bitset_add(ctx, h_unv, 1);
    impulse_vm_context_bitset_add(ctx, h_unv, 2);

    std::vector<impulse_instruction_t> adv_bc = {
        { OP_INIT_MOCK_GRAPH, 0, 0, 0 | (4 << 16) },

        // 1. OP_CSR_WALK_FILTERED: bitset source & scalar source
        { OP_CSR_WALK_FILTERED, 0, 10, static_cast<uint32_t>(1 | (2 << 8) | (0 << 16)) }, // bitset source R1, filter R2
        { OP_CSR_WALK_FILTERED, 0, 11, static_cast<uint32_t>(3 | (2 << 8) | (0 << 16)) }, // scalar source R3, filter R2

        // 2. OP_CSR_WALK_PREDICATE: bitset source & scalar source
        { OP_CSR_WALK_PREDICATE, 0, 12, static_cast<uint32_t>(1 | (0 << 8) | (0 << 16)) }, // bitset source
        { OP_CSR_WALK_PREDICATE, 0, 13, static_cast<uint32_t>(3 | (0 << 8) | (0 << 16)) }, // scalar source

        // 3. OP_NODE_FILTER: bitset source & scalar source
        { OP_NODE_FILTER, 0, 14, static_cast<uint32_t>(1 | (4 << 8) | (0 << 16)) }, // bitset source
        { OP_NODE_FILTER, 0, 15, static_cast<uint32_t>(3 | (4 << 8) | (0 << 16)) }, // scalar source

        // 4. OP_NODE_FILTER_STR_PREFIX: bitset source & scalar source & null prefix
        { OP_NODE_FILTER_STR_PREFIX, 0, 16, static_cast<uint32_t>(1 | (5 << 8) | (0 << 16)) }, // non-null prefix
        { OP_NODE_FILTER_STR_PREFIX, 0, 17, static_cast<uint32_t>(1 | (6 << 8) | (0 << 16)) }, // null prefix

        // 5. OP_CSC_WALK:
        // Top-down CSC with bitset source (unv == 0)
        { OP_CSC_WALK, 0, 18, static_cast<uint32_t>(1 | (0 << 16)) },
        // Top-down CSC with scalar source
        { OP_CSC_WALK, 0, 19, static_cast<uint32_t>(3 | (0 << 16)) },
        // Bottom-up CSC with candidate unv bitset (unv = R7)
        { OP_CSC_WALK, 0, 20, static_cast<uint32_t>(1 | (7 << 16) | (0 << 24)) },
        // CSC with dst == src
        { OP_CSC_WALK, 0, 1, static_cast<uint32_t>(1 | (0 << 16)) },

        // 6. Direct Store Opcodes
        { OP_CSR_WALK_DIRECT_STORE, 0, 21, static_cast<uint32_t>(3 | (0 << 16)) },
        { OP_CSR_WALK_DENSE_STREAM, 0, 22, static_cast<uint32_t>(3 | (0 << 16)) },
        { OP_CSC_WALK_DIRECT_STORE, 0, 23, static_cast<uint32_t>(3 | (0 << 16)) },
        { OP_COO_WALK_DIRECT_STORE, 0, 24, static_cast<uint32_t>(3 | (0 << 16)) },
        { OP_DENSE_WALK_DIRECT_STORE, 0, 25, static_cast<uint32_t>(3 | (0 << 16)) },

        // 7. COO Filtered and Reduced
        { OP_COO_WALK_FILTERED, 0, 26, static_cast<uint32_t>(3 | (2 << 8) | (0 << 16)) },
        { OP_COO_WALK_REDUCE, 0, 27, static_cast<uint32_t>(3 | (8 << 8) | (0 << 16)) }, // float vec R8

        // 8. OP_CSR_WALK_REDUCE: min (0) and sum (1) across float and double vectors
        { OP_CSR_WALK_REDUCE, 0, 28, static_cast<uint32_t>(1 | (8 << 8) | (0 << 16)) }, // float vec R8, min
        { OP_CSR_WALK_REDUCE, 0, 29, static_cast<uint32_t>(1 | (9 << 8) | (0 << 16)) }, // double vec R9, min
        { OP_CSR_WALK_REDUCE_SUM, 0, 30, static_cast<uint32_t>(1 | (8 << 8) | (0 << 16)) },

        // 9. OP_COLLECT_ARRAY across all 4 register types
        { OP_COLLECT_ARRAY, 0, 31, 1 },  // src = R1 (TYPE_BITSET_HANDLE)
        { OP_COLLECT_ARRAY, 0, 32, 3 },  // src = R3 (TYPE_NODE_ID)
        { OP_COLLECT_ARRAY, 0, 33, 4 },  // src = R4 (TYPE_INT64)
        { OP_COLLECT_ARRAY, 0, 34, 35 }, // src = R35 (TYPE_NULL -> empty)

        // 10. GraphBLAS Algorithms: OP_CC_AFFOREST, OP_MXV, OP_VXM, OP_REDUCE
        { OP_CC_AFFOREST, 0, 36, 0 },
        { OP_MXV, 0, 37, static_cast<uint32_t>(0 | (8 << 16)) }, // rel 0, vec R8
        { OP_VXM, 0, 38, static_cast<uint32_t>(0 | (8 << 16)) },
        { OP_REDUCE, 0, 39, 8 }, // reduce vector R8

        { OP_HALT, 0, 0, 0 }
    };

    const char* sample_prefix = "mock";
    state.registers[1] = static_cast<uint64_t>(h_src);
    state.register_types[1] = TYPE_BITSET_HANDLE;
    state.registers[2] = static_cast<uint64_t>(h_filter);
    state.register_types[2] = TYPE_BITSET_HANDLE;
    state.registers[3] = 0; // node ID 0
    state.register_types[3] = TYPE_NODE_ID;
    state.registers[4] = 42;
    state.register_types[4] = TYPE_INT64;
    state.registers[5] = reinterpret_cast<uint64_t>(sample_prefix);
    state.register_types[5] = TYPE_INT64;
    state.registers[6] = 0; // null prefix
    state.register_types[6] = TYPE_INT64;
    state.registers[7] = static_cast<uint64_t>(h_unv);
    state.register_types[7] = TYPE_BITSET_HANDLE;
    state.registers[8] = static_cast<uint64_t>(h_f);
    state.register_types[8] = TYPE_FLOAT_VECTOR;
    state.registers[9] = static_cast<uint64_t>(h_d);
    state.register_types[9] = TYPE_DOUBLE_VECTOR;
    state.register_types[35] = TYPE_NULL;

    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(adv_bc.data(), adv_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_vm_pass4_deep_decisions() {
    std::cout << "[MC/DC] Testing Pass 4 Deep Decisions (Vector Div, Set Ops, Adaptive Walk, 2Hop)..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t state{};
    state.query_context = ctx;

    // 1. Vector Div permutations: {T,F}, {F,T}, {T,T}, {F,F}
    int h_f1 = impulse_vm_context_acquire_float_vector(ctx);
    int h_f2 = impulse_vm_context_acquire_float_vector(ctx);
    int h_d1 = impulse_vm_context_acquire_double_vector(ctx);
    int h_d2 = impulse_vm_context_acquire_double_vector(ctx);

    impulse_vm_context_float_vector_set(ctx, h_f1, 0, 10.0f);
    impulse_vm_context_float_vector_set(ctx, h_f2, 0, 2.0f);
    impulse_vm_context_double_vector_set(ctx, h_d1, 0, 100.0);
    impulse_vm_context_double_vector_set(ctx, h_d2, 0, 5.0);

    std::vector<impulse_instruction_t> div_bc = {
        // Double / Float {T, F}
        { OP_VECTOR_DIV, 0, 10, static_cast<uint32_t>(1 | (2 << 16)) }, // dst=R10, num=R1(d), denom=R2(f)
        // Float / Double {F, T}
        { OP_VECTOR_DIV, 0, 11, static_cast<uint32_t>(2 | (1 << 16)) }, // dst=R11, num=R2(f), denom=R1(d)
        // Double / Double {T, T}
        { OP_VECTOR_DIV, 0, 12, static_cast<uint32_t>(1 | (3 << 16)) }, // dst=R12, num=R1(d), denom=R3(d)
        // Float / Float {F, F}
        { OP_VECTOR_DIV, 0, 13, static_cast<uint32_t>(2 | (4 << 16)) }, // dst=R13, num=R2(f), denom=R4(f)
        { OP_HALT, 0, 0, 0 }
    };

    state.registers[1] = static_cast<uint64_t>(h_d1);
    state.register_types[1] = TYPE_DOUBLE_VECTOR;
    state.registers[2] = static_cast<uint64_t>(h_f1);
    state.register_types[2] = TYPE_FLOAT_VECTOR;
    state.registers[3] = static_cast<uint64_t>(h_d2);
    state.register_types[3] = TYPE_DOUBLE_VECTOR;
    state.registers[4] = static_cast<uint64_t>(h_f2);
    state.register_types[4] = TYPE_FLOAT_VECTOR;

    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(div_bc.data(), div_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // 2. ASSERT_FINITE Condition Independence (C1: null context, C2: h < 0, C3: h >= 8, C4: unallocated)
    std::vector<impulse_instruction_t> assert_bc = {
        { OP_ASSERT_FINITE, 0, 1, 0 },
        { OP_HALT, 0, 0, 0 }
    };

    // Float vector tests
    state.registers[1] = static_cast<uint64_t>(-1); // h < 0
    state.register_types[1] = TYPE_FLOAT_VECTOR;
    state.pc = 0;
    st = impulse_vm_execute(assert_bc.data(), assert_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    state.registers[1] = static_cast<uint64_t>(10); // h >= 8
    state.register_types[1] = TYPE_FLOAT_VECTOR;
    state.pc = 0;
    st = impulse_vm_execute(assert_bc.data(), assert_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    // Double vector tests
    state.registers[1] = static_cast<uint64_t>(-1); // h < 0
    state.register_types[1] = TYPE_DOUBLE_VECTOR;
    state.pc = 0;
    st = impulse_vm_execute(assert_bc.data(), assert_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    state.registers[1] = static_cast<uint64_t>(10); // h >= 8
    state.register_types[1] = TYPE_DOUBLE_VECTOR;
    state.pc = 0;
    st = impulse_vm_execute(assert_bc.data(), assert_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    // Null query context
    state.query_context = nullptr;
    state.registers[1] = static_cast<uint64_t>(0);
    state.register_types[1] = TYPE_FLOAT_VECTOR;
    state.pc = 0;
    st = impulse_vm_execute(assert_bc.data(), assert_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    state.registers[1] = static_cast<uint64_t>(0);
    state.register_types[1] = TYPE_DOUBLE_VECTOR;
    state.pc = 0;
    st = impulse_vm_execute(assert_bc.data(), assert_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    state.query_context = ctx;

    // 3. OP_CSR_WALK_2HOP Bounds Checks (rel1 >= size vs rel2 >= size)
    std::vector<impulse_instruction_t> hop_bc = {
        { OP_CSR_WALK_2HOP, 0, 5, static_cast<uint32_t>(99 | (0 << 16)) }, // rel1 >= size
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(hop_bc.data(), hop_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    std::vector<impulse_instruction_t> hop_bc2 = {
        { OP_CSR_WALK_2HOP, 0, 5, static_cast<uint32_t>(0 | (99 << 16)) }, // rel2 >= size
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(hop_bc2.data(), hop_bc2.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    // 4. OP_ADAPTIVE_WALK Condition Independence
    // Test est_edges > total_edges / 20 and frontier_size > total_nodes / 20
    uint32_t inline_graph[] = {
        0, 1, 2, 3, 4,
        1, 2, 3, 0
    };
    impulse_vm_context_bind_inline_data(ctx, inline_graph, sizeof(inline_graph));

    int h_frontier = impulse_vm_context_acquire_bitset(ctx);
    impulse_vm_context_bitset_add(ctx, h_frontier, 0);
    impulse_vm_context_bitset_add(ctx, h_frontier, 1);

    std::vector<impulse_instruction_t> adapt_bc = {
        { OP_INIT_MOCK_GRAPH, 0, 0, 0 | (4 << 16) },
        // Adaptive walk with bitset frontier
        { OP_ADAPTIVE_WALK, 0, 10, static_cast<uint32_t>(1 | (0 << 16)) },
        // Adaptive walk with scalar source
        { OP_ADAPTIVE_WALK, 0, 11, static_cast<uint32_t>(2 | (0 << 16)) },
        { OP_HALT, 0, 0, 0 }
    };
    state.registers[1] = static_cast<uint64_t>(h_frontier);
    state.register_types[1] = TYPE_BITSET_HANDLE;
    state.registers[2] = 0;
    state.register_types[2] = TYPE_NODE_ID;

    state.pc = 0;
    st = impulse_vm_execute(adapt_bc.data(), adapt_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // 5. OP_FIXPOINT_KLEENE_STAR with Node ID and BitSet source
    std::vector<impulse_instruction_t> fixpoint_bc = {
        { OP_INIT_MOCK_GRAPH, 0, 0, 0 | (4 << 16) },
        // Fixpoint starting from node ID 0
        { OP_FIXPOINT_KLEENE_STAR, 0, 12, static_cast<uint32_t>(2 | (0 << 16)) },
        // Fixpoint starting from bitset frontier
        { OP_FIXPOINT_KLEENE_STAR, 0, 13, static_cast<uint32_t>(1 | (0 << 16)) },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(fixpoint_bc.data(), fixpoint_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // 6. Set Operations (dst == src2, keeps = true vs keeps = false)
    int h_bs1 = impulse_vm_context_acquire_bitset(ctx);
    int h_bs2 = impulse_vm_context_acquire_bitset(ctx);
    impulse_vm_context_bitset_add(ctx, h_bs1, 1);
    impulse_vm_context_bitset_add(ctx, h_bs2, 1);
    impulse_vm_context_bitset_add(ctx, h_bs2, 2);

    std::vector<impulse_instruction_t> set_deep_bc = {
        // OP_SET_INTERSECT: dst = src2 (in-place intersect)
        { OP_SET_INTERSECT, 0, 2, static_cast<uint32_t>(1 | (2 << 16)) }, // dst=R2, src1=R1, src2=R2
        // OP_SET_DIFFERENCE: dst = src2 (in-place difference)
        { OP_SET_DIFFERENCE, 0, 2, static_cast<uint32_t>(1 | (2 << 16)) }, // dst=R2, src1=R1, src2=R2
        // Distinct dst with scalar node ID
        { OP_SET_INTERSECT, 0, 14, static_cast<uint32_t>(1 | (3 << 16)) }, // dst=R14, src1=R1, src2=R3(node)
        { OP_SET_DIFFERENCE, 0, 15, static_cast<uint32_t>(1 | (3 << 16)) }, // dst=R15, src1=R1, src2=R3(node)
        { OP_HALT, 0, 0, 0 }
    };
    state.registers[1] = static_cast<uint64_t>(h_bs1);
    state.register_types[1] = TYPE_BITSET_HANDLE;
    state.registers[2] = static_cast<uint64_t>(h_bs2);
    state.register_types[2] = TYPE_BITSET_HANDLE;
    state.registers[3] = 1;
    state.register_types[3] = TYPE_NODE_ID;

    state.pc = 0;
    st = impulse_vm_execute(set_deep_bc.data(), set_deep_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_pass10_deep_decisions() {
    std::cout << "[MC/DC] Testing Pass 10 Deep Decisions (Context Helpers, Analytics, GraphBLAS)..." << std::endl;

    // 1. Context Helper Boundaries
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    assert(ctx != nullptr);

    // Mock CSC typed permutations
    impulse_vm_context_mock_csc_typed(nullptr, 0, nullptr, nullptr, 4, 4);
    impulse_vm_context_mock_csc_typed(ctx, 9999, nullptr, nullptr, 4, 4);
    impulse_vm_context_mock_csc_typed(ctx, 0, nullptr, nullptr, 0, 0); // test default 4/4 fallback
    impulse_vm_context_mock_csc_typed(ctx, 0, nullptr, nullptr, 8, 8);

    // Bitset fill permutations (rem == 0 vs rem > 0)
    impulse_vm_context_bitset_fill(nullptr, 0, 128);
    impulse_vm_context_bitset_fill(ctx, 9999, 128);
    impulse_vm_context_bitset_fill(ctx, 0, 128); // rem == 0
    impulse_vm_context_bitset_fill(ctx, 0, 130); // rem == 2 > 0

    // Bitset get word permutations
    assert(impulse_vm_context_bitset_get_word(nullptr, 0, 0) == 0);
    assert(impulse_vm_context_bitset_get_word(ctx, 9999, 0) == 0);
    assert(impulse_vm_context_bitset_get_word(ctx, 0, 99999) == 0);
    assert(impulse_vm_context_bitset_get_word(ctx, 0, 0) != 0);

    // Bind inline data
    impulse_vm_context_bind_inline_data(nullptr, nullptr, 0);
    uint8_t dummy_inline[16] = { 1, 2, 3, 4 };
    impulse_vm_context_bind_inline_data(ctx, dummy_inline, 16);

    // Bytecode validator MC/DC
    assert(impulse_vm_validate(nullptr, 10) == IMPULSE_VM_ERR_OUT_OF_BOUNDS);
    assert(impulse_vm_validate(nullptr, 0) == IMPULSE_VM_OK);
    impulse_instruction_t valid_code[] = { { OP_NOP, 0, 0, 0 }, { OP_HALT, 0, 0, 0 } };
    assert(impulse_vm_validate(valid_code, 2) == IMPULSE_VM_OK);

    // 2. Mock Graph with CSR/CSC Topology for GraphBLAS & Analytics
    uint32_t offsets[5] = { 0, 2, 3, 4, 4 }; // 4 nodes: 0->(1,2), 1->(2), 2->(3), 3->()
    uint32_t targets[4] = { 1, 2, 2, 3 };
    uint32_t csc_offsets[5] = { 0, 0, 1, 3, 4 };
    uint32_t csc_targets[4] = { 0, 0, 1, 2 };

    impulse_vm_context_mock_csr(ctx, 0, offsets, targets, 4, 4);
    impulse_vm_context_mock_csc(ctx, 0, csc_offsets, csc_targets);

    // Set up Float and Double Vectors
    int h_f1 = impulse_vm_context_acquire_float_vector(ctx);
    int h_f2 = impulse_vm_context_acquire_float_vector(ctx);
    int h_f3 = impulse_vm_context_acquire_float_vector(ctx);
    for (size_t i = 0; i < 4; ++i) {
        impulse_vm_context_float_vector_set(ctx, h_f1, i, 1.0f);
        impulse_vm_context_float_vector_set(ctx, h_f2, i, 2.0f);
    }

    int h_d1 = impulse_vm_context_acquire_double_vector(ctx);
    int h_d2 = impulse_vm_context_acquire_double_vector(ctx);
    int h_d3 = impulse_vm_context_acquire_double_vector(ctx);
    for (size_t i = 0; i < 4; ++i) {
        impulse_vm_context_double_vector_set(ctx, h_d1, i, 1.0);
        impulse_vm_context_double_vector_set(ctx, h_d2, i, 2.0);
    }

    impulse_vm_state_t state{};
    state.query_context = ctx;

    // Registers setup
    state.registers[1] = static_cast<uint64_t>(h_f1);
    state.register_types[1] = TYPE_FLOAT_VECTOR;
    state.registers[2] = static_cast<uint64_t>(h_f2);
    state.register_types[2] = TYPE_FLOAT_VECTOR;
    state.registers[3] = static_cast<uint64_t>(h_f3);
    state.register_types[3] = TYPE_FLOAT_VECTOR;

    state.registers[11] = static_cast<uint64_t>(h_d1);
    state.register_types[11] = TYPE_DOUBLE_VECTOR;
    state.registers[12] = static_cast<uint64_t>(h_d2);
    state.register_types[12] = TYPE_DOUBLE_VECTOR;
    state.registers[13] = static_cast<uint64_t>(h_d3);
    state.register_types[13] = TYPE_DOUBLE_VECTOR;

    // 3. GraphBLAS Matrix-Vector & Element-wise Ops (Float & Double)
    std::vector<impulse_instruction_t> gblas_bc = {
        // MXV & VXM Float (flags = 0)
        { OP_MXV, 0, 3, 1 | (0 << 16) },  // R3 = M * R1 (rel 0)
        { OP_VXM, 0, 3, 1 | (0 << 16) },  // R3 = R1 * M (rel 0)
        // MXV & VXM Double (flags = 1)
        { OP_MXV, 1, 13, 11 | (0 << 16) }, // R13 = M * R11
        { OP_VXM, 1, 13, 11 | (0 << 16) }, // R13 = R11 * M

        // Element-wise Add & Mult Float
        { OP_EWISE_ADD, 0, 3, 1 | (2 << 16) },
        { OP_EWISE_MULT, 0, 3, 1 | (2 << 16) },
        // Element-wise Add & Mult Double
        { OP_EWISE_ADD, 1, 13, 11 | (12 << 16) },
        { OP_EWISE_MULT, 1, 13, 11 | (12 << 16) },

        // Reduce Float & Double
        { OP_REDUCE, 0, 4, 1 },  // R4 = sum(R1) (Float)
        { OP_REDUCE, 1, 14, 11 },// R14 = sum(R11) (Double)

        // Analytics opcodes
        { OP_BRANDES_FORWARD, 0, 5, 0 },  // Root node 0
        { OP_BRANDES_BACKWARD, 0, 6, 0 },
        { OP_DELTA_STEP_RELAX, 0, 7, 0 | (1 << 16) },
        { OP_LOUVAIN_MODULARITY, 0, 8, 0 },
        { OP_KCORE_DECOMPOSITION, 0, 9, 0 | (1 << 16) },
        { OP_ISLAND_DETECT, 0, 10, 0 },
        { OP_SPARSE_MATVEC, 0, 15, 0 | (1 << 16) },

        { OP_HALT, 0, 0, 0 }
    };

    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(gblas_bc.data(), gblas_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_pass11_exhaustive_vm_matrix() {
    std::cout << "[MC/DC] Testing Pass 11 Exhaustive VM Matrix (Adaptive CSR/CSC, Parallelism, Attributes)..." << std::endl;

    // 1. Exhaustive Context Vector / String / Value Map Handle & Index Combinations
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    assert(ctx != nullptr);

    // Float vector get/set combinations
    assert(impulse_vm_context_get_float_vector(nullptr, 0) == nullptr);
    assert(impulse_vm_context_get_float_vector(ctx, 9999) == nullptr);
    assert(impulse_vm_context_get_float_vector(ctx, 0) == nullptr); // unallocated handle 0
    int h_f = impulse_vm_context_acquire_float_vector(ctx);
    assert(h_f >= 0);
    assert(impulse_vm_context_get_float_vector(ctx, h_f) != nullptr);
    impulse_vm_context_float_vector_set(nullptr, h_f, 0, 1.0f);
    impulse_vm_context_float_vector_set(ctx, 9999, 0, 1.0f);
    impulse_vm_context_float_vector_set(ctx, 0, 0, 1.0f); // unallocated handle 0 (if h_f != 0)
    impulse_vm_context_float_vector_set(ctx, h_f, 99999999, 1.0f); // index >= max_nodes
    impulse_vm_context_float_vector_set(ctx, h_f, 0, 1.0f); // valid

    // Double vector get/set combinations
    assert(impulse_vm_context_get_double_vector(nullptr, 0) == nullptr);
    assert(impulse_vm_context_get_double_vector(ctx, 9999) == nullptr);
    assert(impulse_vm_context_get_double_vector(ctx, 0) == nullptr);
    int h_d = impulse_vm_context_acquire_double_vector(ctx);
    assert(h_d >= 0);
    assert(impulse_vm_context_get_double_vector(ctx, h_d) != nullptr);
    impulse_vm_context_double_vector_set(nullptr, h_d, 0, 1.0);
    impulse_vm_context_double_vector_set(ctx, 9999, 0, 1.0);
    impulse_vm_context_double_vector_set(ctx, 0, 0, 1.0);
    impulse_vm_context_double_vector_set(ctx, h_d, 99999999, 1.0);
    impulse_vm_context_double_vector_set(ctx, h_d, 0, 1.0);

    // String vector get/set combinations
    assert(impulse_vm_context_string_vector_get(nullptr, 0, 0) == nullptr);
    assert(impulse_vm_context_string_vector_get(ctx, 9999, 0) == nullptr);
    assert(impulse_vm_context_string_vector_get(ctx, 0, 0) == nullptr);
    int h_s = impulse_vm_context_acquire_string_vector(ctx);
    assert(h_s >= 0);
    impulse_vm_context_string_vector_add(nullptr, h_s, "abc");
    impulse_vm_context_string_vector_add(ctx, 9999, "abc");
    impulse_vm_context_string_vector_add(ctx, h_s, "hello");
    assert(impulse_vm_context_string_vector_size(nullptr, h_s) == 0);
    assert(impulse_vm_context_string_vector_size(ctx, 9999) == 0);
    assert(impulse_vm_context_string_vector_size(ctx, h_s) == 1);
    assert(impulse_vm_context_string_vector_get(ctx, h_s, 9999) == nullptr);
    assert(impulse_vm_context_string_vector_get(ctx, h_s, 0) != nullptr);

    // Value map get key / value combinations
    assert(impulse_vm_context_value_map_get_key(nullptr, 0, 0) == nullptr);
    assert(impulse_vm_context_value_map_get_key(ctx, 9999, 0) == nullptr);
    assert(impulse_vm_context_value_map_get_value(nullptr, 0, 0) == 0.0f);
    assert(impulse_vm_context_value_map_get_value(ctx, 9999, 0) == 0.0f);

    // 2. High-Frontier Parallel CSR Walk (use_parallel = true)
    const size_t HIGH_N = 20000;
    std::vector<uint32_t> big_offsets(HIGH_N + 1);
    std::vector<uint32_t> big_targets(HIGH_N);
    for (size_t i = 0; i < HIGH_N; ++i) {
        big_offsets[i] = static_cast<uint32_t>(i);
        big_targets[i] = static_cast<uint32_t>((i + 1) % HIGH_N);
    }
    big_offsets[HIGH_N] = static_cast<uint32_t>(HIGH_N);

    impulse_vm_context_mock_csr(ctx, 0, big_offsets.data(), big_targets.data(), HIGH_N, HIGH_N);
    impulse_vm_context_mock_csc(ctx, 0, big_offsets.data(), big_targets.data());

    int h_frontier = impulse_vm_context_acquire_bitset(ctx);
    int h_out = impulse_vm_context_acquire_bitset(ctx);
    impulse_vm_context_bitset_fill(ctx, h_frontier, HIGH_N); // all 20,000 active to trigger parallel threshold

    impulse_vm_state_t state{};
    state.query_context = ctx;
    state.registers[1] = static_cast<uint64_t>(h_frontier);
    state.register_types[1] = TYPE_BITSET_HANDLE;
    state.registers[2] = static_cast<uint64_t>(h_out);
    state.register_types[2] = TYPE_BITSET_HANDLE;

    // Parallel CSR Walk with non-accumulating in-place dst == src
    std::vector<impulse_instruction_t> parallel_bc = {
        { OP_SET_MAX_DOP, 0, 0, 4 },          // max threads = 4
        // Parallel CSR walk
        { OP_CSR_WALK, 0, 2, 1 | (0 << 16) }, // dst=R2, src=R1
        // In-place non-accumulating CSR walk (dst == src)
        { OP_CSR_WALK, 0, 1, 1 | (0 << 16) }, // dst=R1, src=R1
        // CSC walk where dst == src
        { OP_CSC_WALK, 0, 1, 1 | (0 << 16) }, // dst=R1, src=R1
        // Stable check with matching bitsets
        { OP_STABLE_CHECK, 0, 1, 2 },
        // Frontier diff
        { OP_FRONTIER_DIFF, 0, 3, 1 | (2 << 16) },
        // Fixpoint kleene star on relation slot 0
        { OP_FIXPOINT_KLEENE_STAR, 0, 1, 2 | (0 << 16) },

        { OP_HALT, 0, 0, 0 }
    };

    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(parallel_bc.data(), parallel_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // 3. Single-Target Layout with Tombstones (0xFFFFFFFF, 0xFFFF, ~0ULL)
    uint32_t single_targets[4] = { 1, 0xFFFFFFFF, 0xFFFF, static_cast<uint32_t>(~0ULL) };
    impulse_vm_context_mock_csr(ctx, 1, nullptr, single_targets, 4, 4); // slot 1: single target layout (!offsets_ptr && targets_ptr)
    impulse_vm_context_mock_csc(ctx, 1, nullptr, single_targets);

    int h_small = impulse_vm_context_acquire_bitset(ctx);
    impulse_vm_context_bitset_fill(ctx, h_small, 4);
    state.registers[1] = static_cast<uint64_t>(h_small);
    state.registers[2] = static_cast<uint64_t>(h_out);

    std::vector<impulse_instruction_t> single_target_bc = {
        { OP_CSR_WALK, 0, 2, 1 | (1 << 16) }, // slot 1 single target
        { OP_CSC_WALK, 0, 2, 1 | (1 << 16) },
        { OP_CSR_WALK_DENSE_STREAM, 0, 2, 0 | (0 << 16) }, // slot 0 dense stream
        { OP_CSR_WALK_DIRECT_STORE, 0, 2, 1 | (0 << 16) }, // slot 0 direct store
        { OP_CSC_WALK_DIRECT_STORE, 0, 2, 1 | (0 << 16) }, // slot 0 direct store
        { OP_CSR_DEGREE, 0, 4, 0 | (1 << 16) }, // degree of node 0
        { OP_HAS_CSR, 0, 5, 1 },
        { OP_HAS_CSC, 0, 6, 1 },
        { OP_HAS_COO, 0, 7, 1 },
        { OP_HAS_KEY_CATALOG, 0, 8, 1 },
        { OP_HALT, 0, 0, 0 }
    };

    state.pc = 0;
    st = impulse_vm_execute(single_target_bc.data(), single_target_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // Direct store null checks on slot 1
    std::vector<impulse_instruction_t> direct_null_bc = {
        { OP_CSR_WALK_DIRECT_STORE, 0, 2, 1 | (1 << 16) }, // slot 1 (!offsets) -> null snapshot
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    assert(impulse_vm_execute(direct_null_bc.data(), direct_null_bc.size(), &state, 0) == IMPULSE_VM_ERR_NULL_SNAPSHOT);

    // 4. Roaring Bitmaps Logic & Predicate Range Check
    std::vector<impulse_instruction_t> roaring_and_not_bc = {
        { OP_ROARING_BITMAP_AND_NOT, 0, 3, 1 | (2 << 16) },
        { OP_ROARING_BITMAP_OR, 0, 3, 1 | (2 << 16) },
        { OP_VEC_CMP_BETWEEN, 0, 4, 0 | (10 << 16) }, // between min/max
        { OP_HALT, 0, 0, 0 }
    };

    state.pc = 0;
    st = impulse_vm_execute(roaring_and_not_bc.data(), roaring_and_not_bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    impulse_vm_context_destroy(ctx);
}

void test_mcdc_pass17_deep_vm_accessors() {
    std::cout << "[MC/DC] Testing Pass 17 Exhaustive VM Context Accessors & Slot Width Truth Tables..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    assert(ctx != nullptr);

    // 1. BitSet add/test truth tables
    impulse_vm_context_bitset_add(nullptr, 0, 1);
    impulse_vm_context_bitset_add(ctx, 999, 1);
    impulse_vm_context_bitset_add(ctx, 0, 1); // not allocated

    assert(!impulse_vm_context_bitset_test(nullptr, 0, 1));
    assert(!impulse_vm_context_bitset_test(ctx, 999, 1));
    assert(!impulse_vm_context_bitset_test(ctx, 0, 1)); // not allocated

    int b_h = impulse_vm_context_acquire_bitset(ctx);
    assert(b_h >= 0);
    impulse_vm_context_bitset_add(ctx, b_h, 1);
    assert(impulse_vm_context_bitset_test(ctx, b_h, 1));
    assert(!impulse_vm_context_bitset_test(ctx, b_h, 2));

    // 2. Float vector set/get truth tables
    impulse_vm_context_float_vector_set(nullptr, 0, 0, 1.0f);
    impulse_vm_context_float_vector_set(ctx, 999, 0, 1.0f);
    impulse_vm_context_float_vector_set(ctx, 0, 0, 1.0f); // not allocated

    int f_h = impulse_vm_context_acquire_float_vector(ctx);
    assert(f_h >= 0);
    impulse_vm_context_float_vector_set(ctx, f_h, 99999, 1.0f); // index >= max_nodes
    impulse_vm_context_float_vector_set(ctx, f_h, 5, 3.14f); // valid

    assert(impulse_vm_context_get_float_vector(nullptr, 0) == nullptr);
    assert(impulse_vm_context_get_float_vector(ctx, 999) == nullptr);
    assert(impulse_vm_context_get_float_vector(ctx, f_h) != nullptr);

    // 3. Double vector set/get truth tables
    impulse_vm_context_double_vector_set(nullptr, 0, 0, 1.0);
    impulse_vm_context_double_vector_set(ctx, 999, 0, 1.0);
    impulse_vm_context_double_vector_set(ctx, 0, 0, 1.0); // not allocated

    int d_h = impulse_vm_context_acquire_double_vector(ctx);
    assert(d_h >= 0);
    impulse_vm_context_double_vector_set(ctx, d_h, 99999, 1.0); // index >= max_nodes
    impulse_vm_context_double_vector_set(ctx, d_h, 5, 2.718); // valid

    assert(impulse_vm_context_get_double_vector(nullptr, 0) == nullptr);
    assert(impulse_vm_context_get_double_vector(ctx, 999) == nullptr);
    assert(impulse_vm_context_get_double_vector(ctx, d_h) != nullptr);

    // 4. Node vector get truth tables
    assert(impulse_vm_context_get_node_vector(nullptr, 0) == nullptr);
    assert(impulse_vm_context_get_node_vector(ctx, 999) == nullptr);
    assert(impulse_vm_context_get_node_vector(ctx, 0) == nullptr); // not allocated
    int n_h = impulse_vm_context_acquire_node_vector(ctx);
    assert(n_h >= 0);
    assert(impulse_vm_context_get_node_vector(ctx, n_h) != nullptr);

    // 5. String vector add/size/get truth tables
    impulse_vm_context_string_vector_add(nullptr, 0, "str");
    impulse_vm_context_string_vector_add(ctx, 999, "str");
    impulse_vm_context_string_vector_add(ctx, 0, "str"); // not allocated

    assert(impulse_vm_context_string_vector_size(nullptr, 0) == 0);
    assert(impulse_vm_context_string_vector_size(ctx, 999) == 0);
    assert(impulse_vm_context_string_vector_size(ctx, 0) == 0);

    assert(impulse_vm_context_string_vector_get(nullptr, 0, 0) == nullptr);
    assert(impulse_vm_context_string_vector_get(ctx, 999, 0) == nullptr);
    assert(impulse_vm_context_string_vector_get(ctx, 0, 0) == nullptr);

    int s_h = impulse_vm_context_acquire_string_vector(ctx);
    assert(s_h >= 0);
    assert(impulse_vm_context_string_vector_get(ctx, s_h, 0) == nullptr); // index >= size
    impulse_vm_context_string_vector_add(ctx, s_h, "hello");
    assert(impulse_vm_context_string_vector_size(ctx, s_h) == 1);
    assert(std::string(impulse_vm_context_string_vector_get(ctx, s_h, 0)) == "hello");

    // 6. Value map size/get_key/get_value truth tables
    assert(impulse_vm_context_value_map_size(nullptr, 0) == 0);
    assert(impulse_vm_context_value_map_size(ctx, 999) == 0);
    assert(impulse_vm_context_value_map_size(ctx, 0) == 0);

    assert(impulse_vm_context_value_map_get_key(nullptr, 0, 0) == nullptr);
    assert(impulse_vm_context_value_map_get_key(ctx, 999, 0) == nullptr);
    assert(impulse_vm_context_value_map_get_key(ctx, 0, 0) == nullptr);

    assert(impulse_vm_context_value_map_get_value(nullptr, 0, 0) == 0.0f);
    assert(impulse_vm_context_value_map_get_value(ctx, 999, 0) == 0.0f);
    assert(impulse_vm_context_value_map_get_value(ctx, 0, 0) == 0.0f);

    int m_h = impulse_vm_context_acquire_value_map(ctx);
    assert(m_h >= 0);
    assert(impulse_vm_context_value_map_get_key(ctx, m_h, 0) == nullptr); // index >= size
    assert(impulse_vm_context_value_map_get_value(ctx, m_h, 0) == 0.0f); // index >= size

    // Release handles
    impulse_vm_context_release_bitset(ctx, b_h);
    impulse_vm_context_release_float_vector(ctx, f_h);
    impulse_vm_context_release_double_vector(ctx, d_h);
    impulse_vm_context_release_node_vector(ctx, n_h);
    impulse_vm_context_release_string_vector(ctx, s_h);
    impulse_vm_context_release_value_map(ctx, m_h);

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
    test_mcdc_opcodes_roaring_and_boolean();
    test_mcdc_opcodes_vector_math_unary_binary_ternary();
    test_mcdc_opcodes_assert_finite_exhaustive();
    test_mcdc_opcodes_graph_traversal_specializations();
    test_mcdc_opcodes_key_mapping_and_attributes();
    test_mcdc_opcodes_filtered_and_advanced_traversals();
    test_mcdc_vm_pass4_deep_decisions();
    test_mcdc_pass10_deep_decisions();
    test_mcdc_pass11_exhaustive_vm_matrix();
    test_mcdc_pass17_deep_vm_accessors();

    std::cout << "================================================================" << std::endl;
    std::cout << " ALL MC/DC CONDITION INDEPENDENCE TESTS PASSED SUCCESSFULLY!" << std::endl;
    std::cout << "================================================================" << std::endl;
    return 0;
}



