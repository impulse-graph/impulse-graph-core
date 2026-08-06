#include "impulse_vm.h"
#include "impulse_graph.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include <algorithm>

void test_basic_nop_halt() {
    // 0: NOP
    // 1: HALT
    std::vector<impulse_instruction_t> bytecode = {
        { OP_NOP, 0, 0, 0 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state{};
    state.pc = 0;
    state.flags = 0;
    
    impulse_vm_status_t status = impulse_vm_execute(
        bytecode.data(), bytecode.size(), &state, 0
    );

    assert(status == IMPULSE_VM_OK);
    assert(state.pc == 1); // Pointing to HALT (which was executed)
    std::cout << "[VM Test] NOP & HALT: PASSED" << std::endl;
}

void test_scalar_load_and_mov() {
    // 0: LOAD_CONST_INT R4, 42
    // 1: LOAD_CONST_FLOAT R5, 0x40490fdb (3.14159265f bits)
    // 2: MOV R6, R4
    // 3: CLEAR_REG R4
    // 4: HALT
    std::vector<impulse_instruction_t> bytecode = {
        { OP_LOAD_CONST_INT, 0, 4, 42 },
        { OP_LOAD_CONST_FLOAT, 0, 5, 0x40490fdb },
        { OP_MOV, 0, 6, 4 }, // DST=R6, SRC=R4 (in payload)
        { OP_CLEAR_REG, 0, 4, 0 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state{};
    
    impulse_vm_status_t status = impulse_vm_execute(
        bytecode.data(), bytecode.size(), &state, 0
    );

    assert(status == IMPULSE_VM_OK);
    assert(state.registers[4] == 0);
    assert(state.register_types[4] == TYPE_NULL);
    
    assert(state.registers[5] == 0x40490fdb);
    assert(state.register_types[5] == TYPE_FLOAT);
    
    assert(state.registers[6] == 42);
    assert(state.register_types[6] == TYPE_INT64);
    
    std::cout << "[VM Test] Scalar Load & MOV: PASSED" << std::endl;
}

void test_init_input_node() {
    // 0: INIT_INPUT_NODE R0, 0
    // 1: HALT
    std::vector<impulse_instruction_t> bytecode = {
        { OP_INIT_INPUT_NODE, 0, 0, 0 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state{};
    state.flags = IMPULSE_VM_FLAG_ZF; // Set ZF
    
    impulse_vm_status_t status = impulse_vm_execute(
        bytecode.data(), bytecode.size(), &state, 12345ULL
    );

    assert(status == IMPULSE_VM_OK);
    assert(state.registers[0] == 12345ULL);
    assert(state.register_types[0] == TYPE_NODE_ID);
    assert(!(state.flags & IMPULSE_VM_FLAG_ZF)); // ZF should be cleared by OP_INIT_INPUT_NODE
    
    std::cout << "[VM Test] INIT_INPUT_NODE: PASSED" << std::endl;
}

void test_jmp_jz_jnz() {
    // 0: LOAD_CONST_INT R4, 1
    // 1: JMP +2 (jump to HALT at 3)
    // 2: LOAD_CONST_INT R4, 99
    // 3: HALT
    std::vector<impulse_instruction_t> bytecode1 = {
        { OP_LOAD_CONST_INT, 0, 4, 1 },
        { OP_JMP, 0, 0, 2 }, // payload offset = 2
        { OP_LOAD_CONST_INT, 0, 4, 99 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state1{};
    impulse_vm_status_t status1 = impulse_vm_execute(
        bytecode1.data(), bytecode1.size(), &state1, 0
    );
    assert(status1 == IMPULSE_VM_OK);
    assert(state1.registers[4] == 1); // R4 should NOT be 99

    // 0: JZ +2 (jump if ZF is set)
    // 1: LOAD_CONST_INT R4, 88
    // 2: HALT
    std::vector<impulse_instruction_t> bytecode2 = {
        { OP_JZ, 0, 0, 2 },
        { OP_LOAD_CONST_INT, 0, 4, 88 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state2{};
    state2.flags = IMPULSE_VM_FLAG_ZF; // set ZF
    impulse_vm_status_t status2 = impulse_vm_execute(
        bytecode2.data(), bytecode2.size(), &state2, 0
    );
    assert(status2 == IMPULSE_VM_OK);
    assert(state2.registers[4] == 0); // JZ taken, R4 not modified

    // Same program, but ZF is NOT set (JZ should not jump)
    impulse_vm_state_t state3{};
    state3.flags = 0; // clear ZF
    impulse_vm_status_t status3 = impulse_vm_execute(
        bytecode2.data(), bytecode2.size(), &state3, 0
    );
    assert(status3 == IMPULSE_VM_OK);
    assert(state3.registers[4] == 88); // JZ not taken, load executed

    // JNZ test
    // 0: JNZ +2 (jump if ZF is NOT set)
    // 1: LOAD_CONST_INT R4, 77
    // 2: HALT
    std::vector<impulse_instruction_t> bytecode3 = {
        { OP_JNZ, 0, 0, 2 },
        { OP_LOAD_CONST_INT, 0, 4, 77 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state4{};
    state4.flags = 0; // ZF not set -> should jump
    impulse_vm_status_t status4 = impulse_vm_execute(
        bytecode3.data(), bytecode3.size(), &state4, 0
    );
    assert(status4 == IMPULSE_VM_OK);
    assert(state4.registers[4] == 0);

    impulse_vm_state_t state5{};
    state5.flags = IMPULSE_VM_FLAG_ZF; // ZF set -> should not jump
    impulse_vm_status_t status5 = impulse_vm_execute(
        bytecode3.data(), bytecode3.size(), &state5, 0
    );
    assert(status5 == IMPULSE_VM_OK);
    assert(state5.registers[4] == 77);

    std::cout << "[VM Test] Branching (JMP, JZ, JNZ): PASSED" << std::endl;
}

void test_loop_decr() {
    // Implement a loop to sum numbers:
    // 0: LOAD_CONST_INT R4, 5  (Loop Counter = 5)
    // 1: LOAD_CONST_INT R5, 0  (Accumulator = 0)
    // --- Loop Start ---
    // 2: LOAD_CONST_INT R6, 10
    // 3: LOOP_DECR R4, -1      (decr R4, if > 0 jump back to 2)
    // 4: HALT
    // Wait, let's trace this loop:
    // iter 1: R4=5. R5=0. inst 2: R6=10. inst 3: R4 becomes 4. Since 4 > 0, pc += -1 (points to inst 2).
    // iter 2: R6=10. inst 3: R4 becomes 3. Since 3 > 0, pc += -1 (points to inst 2).
    // iter 3: R6=10. inst 3: R4 becomes 2. Since 2 > 0, pc += -1 (points to inst 2).
    // iter 4: R6=10. inst 3: R4 becomes 1. Since 1 > 0, pc += -1 (points to inst 2).
    // iter 5: R6=10. inst 3: R4 becomes 0. Since 0 is not > 0, pc++ (points to HALT).
    // This executes inst 2 exactly 5 times.
    std::vector<impulse_instruction_t> bytecode = {
        { OP_LOAD_CONST_INT, 0, 4, 5 },
        { OP_LOAD_CONST_INT, 0, 5, 0 },
        { OP_LOAD_CONST_INT, 0, 6, 10 },
        { OP_LOOP_DECR, 0, 4, static_cast<uint32_t>(-1) }, // Offset is -1 (jump back to offset 2)
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state{};
    impulse_vm_status_t status = impulse_vm_execute(
        bytecode.data(), bytecode.size(), &state, 0
    );

    assert(status == IMPULSE_VM_OK);
    assert(state.registers[4] == 0); // Counter reached 0
    assert(state.flags & IMPULSE_VM_FLAG_ZF); // ZF is set when result is 0
    
    std::cout << "[VM Test] LOOP_DECR: PASSED" << std::endl;
}

void test_error_handling() {
    // Test invalid register index (>= 64)
    std::vector<impulse_instruction_t> bytecode1 = {
        { OP_LOAD_CONST_INT, 0, 64, 5 }, // R64 is invalid
        { OP_HALT, 0, 0, 0 }
    };
    impulse_vm_state_t state1{};
    impulse_vm_status_t status1 = impulse_vm_execute(
        bytecode1.data(), bytecode1.size(), &state1, 0
    );
    assert(status1 == IMPULSE_VM_ERR_INVALID_REGISTER);

    // Test invalid opcode
    std::vector<impulse_instruction_t> bytecode2 = {
        { 0xEE, 0, 4, 0 }, // 0xEE is invalid opcode
        { OP_HALT, 0, 0, 0 }
    };
    impulse_vm_state_t state2{};
    impulse_vm_status_t status2 = impulse_vm_execute(
        bytecode2.data(), bytecode2.size(), &state2, 0
    );
    assert(status2 == IMPULSE_VM_ERR_INVALID_OPCODE);

    // Test PC out of bounds (no HALT)
    std::vector<impulse_instruction_t> bytecode3 = {
        { OP_NOP, 0, 0, 0 }
    };
    impulse_vm_state_t state3{};
    impulse_vm_status_t status3 = impulse_vm_execute(
        bytecode3.data(), bytecode3.size(), &state3, 0
    );
    assert(status3 == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    std::cout << "[VM Test] Error Handling & Bounds checks: PASSED" << std::endl;
}

void test_vm_state_layout_size() {
    // Assert alignment and size to guarantee Java conceptual alignment
    assert(sizeof(impulse_vm_state_t) == 640);
    assert(alignof(impulse_vm_state_t) == 64);
    
    impulse_vm_state_t state{};
    // Verify relative offsets
    uint8_t* base = reinterpret_cast<uint8_t*>(&state);
    uint8_t* p_pc = reinterpret_cast<uint8_t*>(&state.pc);
    uint8_t* p_flags = reinterpret_cast<uint8_t*>(&state.flags);
    uint8_t* p_regs = reinterpret_cast<uint8_t*>(state.registers);
    uint8_t* p_types = reinterpret_cast<uint8_t*>(state.register_types);
    uint8_t* p_ctx = reinterpret_cast<uint8_t*>(&state.query_context);

    assert((p_pc - base) == 0);
    assert((p_flags - base) == 8);
    assert((p_regs - base) == 16);
    assert((p_types - base) == 528);
    assert((p_ctx - base) == 592);
    
    std::cout << "[VM Test] VmState Memory Layout Alignment: PASSED" << std::endl;
}

void test_bitset_operations() {
    // Create query context (defaults to 1024 * 1024 max nodes -> 16384 words of 64-bit)
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    
    // Initialize a mock bitset with nodes 5, 10, 100 set
    std::vector<uint64_t> input_set(16384, 0);
    input_set[5 / 64] |= (1ULL << (5 % 64));
    input_set[10 / 64] |= (1ULL << (10 % 64));
    input_set[100 / 64] |= (1ULL << (100 % 64));

    // Program:
    // 0: INIT_INPUT_SET R4, input_param (R4 = {5, 10, 100})
    // 1: CLEAR_REG R5
    // 2: LOAD_CONST_INT R10, 500
    // 3: SET_UNION R5, R10              (R5 = {500})
    // 4: LOAD_CONST_INT R11, 10
    // 5: SET_UNION R5, R11              (R5 = {10, 500})
    // 6: SET_INTERSECT R4, R5           (R4 = R4 & R5 -> {10})
    // 7: SET_CARDINALITY R7, R4         (R7 = 1)
    // 8: COLLECT_ARRAY R63, R4          (R63 = array data, type = TYPE_NODE_VECTOR)
    // 9: HALT
    std::vector<impulse_instruction_t> bytecode = {
        { OP_INIT_INPUT_SET, 0, 4, 0 },
        { OP_CLEAR_REG, 0, 5, 0 },
        { OP_LOAD_CONST_INT, 0, 10, 500 },
        { OP_SET_UNION, 0, 5, 10 },
        { OP_LOAD_CONST_INT, 0, 11, 10 },
        { OP_SET_UNION, 0, 5, 11 },
        { OP_SET_INTERSECT, 0, 4, 5 },
        { OP_SET_CARDINALITY, 0, 7, 4 },
        { OP_COLLECT_ARRAY, 0, 63, 4 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state{};
    state.query_context = ctx;

    impulse_vm_status_t status = impulse_vm_execute(
        bytecode.data(), bytecode.size(), &state, reinterpret_cast<uint64_t>(input_set.data())
    );

    assert(status == IMPULSE_VM_OK);
    assert(state.registers[7] == 1); // Cardinality is 1
    assert(state.register_types[7] == TYPE_INT64);
    assert(state.register_types[63] == TYPE_NODE_VECTOR);

    // Retrieve contiguous array
    const uint64_t* results = reinterpret_cast<const uint64_t*>(state.registers[63]);
    assert(results != nullptr);
    // There should be exactly 1 element in node_buffer: 10
    assert(impulse_vm_context_get_vector_size(ctx) == 1);
    assert(results[0] == 10);

    // Clean up
    impulse_vm_context_destroy(ctx);
    std::cout << "[VM Test] BitSet & Set Operations (Phase 2): PASSED" << std::endl;
}

void test_rbac_traversal() {
    const char* filename = "__vm_test_rbac_snapshot.bin";
    std::remove(filename); // Clean up any stale files

    // Build a small graph: 8 nodes, 7 edges
    // Rel 0 (userToGroup):
    //   0 -> 3 (Alice is User 0, assigned to Employee Role 3)
    //   1 -> 2 (Bob is User 1, assigned to Guest Role 2)
    const uint32_t r0_offsets[] = { 0, 1, 2, 2, 2, 2, 2, 2, 2 };
    const uint32_t r0_targets[] = { 3, 2 };

    // Rel 1 (roleInheritance):
    //   Role 4 (Admin) -> Role 3 (Employee)
    //   Role 3 (Employee) -> Role 2 (Guest)
    const uint32_t r1_offsets[] = { 0, 0, 0, 0, 1, 2, 2, 2, 2 };
    const uint32_t r1_targets[] = { 2, 3 }; // Note: 3 -> 2, 4 -> 3

    // Rel 2 (rolePermission):
    //   Role 2 (Guest) -> Permission 5 (read)
    //   Role 3 (Employee) -> Permission 6 (write)
    //   Role 4 (Admin) -> Permission 7 (delete)
    const uint32_t r2_offsets[] = { 0, 0, 0, 1, 2, 3, 3, 3, 3 };
    const uint32_t r2_targets[] = { 5, 6, 7 };

    impulse_writer_t* writer = impulse_writer_create(filename, 0);
    assert(writer != nullptr);

    impulse_status_t st;
    st = impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT32, "entities");
    assert(st == IMPULSE_OK);

    // Add Rel 0 (userToGroup)
    st = impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, 8, 2, 0,
                                     r0_offsets, sizeof(r0_offsets),
                                     r0_targets, sizeof(r0_targets));
    assert(st == IMPULSE_OK);

    // Add Rel 1 (roleInheritance)
    st = impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, 8, 2, 0,
                                     r1_offsets, sizeof(r1_offsets),
                                     r1_targets, sizeof(r1_targets));
    assert(st == IMPULSE_OK);

    // Add Rel 2 (rolePermission)
    st = impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, 8, 3, 0,
                                     r2_offsets, sizeof(r2_offsets),
                                     r2_targets, sizeof(r2_targets));
    assert(st == IMPULSE_OK);

    st = impulse_writer_finalize(writer);
    assert(st == IMPULSE_OK);
    impulse_writer_destroy(writer);

    // Open snapshot
    impulse_snapshot_t* snap = impulse_snapshot_open(filename, &st);
    assert(snap != nullptr);
    assert(st == IMPULSE_OK);

    // Create VM query context using snapshot
    impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
    assert(ctx != nullptr);

    // --- PROGRAM 1: Linear 3-Tier walk starting at User 0 (Alice) ---
    // 0: INIT_INPUT_NODE R0, 0           ; R0 = 0 (Alice)
    // 1: CSR_WALK R1, R0, REL_0          ; R1 = Walk userToGroup from Alice -> {3} (Employee)
    // 2: CSR_WALK R2, R1, REL_1          ; R2 = Walk roleInheritance from Employee -> {2} (Guest)
    // 3: CSR_WALK R3, R2, REL_2          ; R3 = Walk rolePermission from Guest -> {5} (Read permission)
    // 4: COLLECT_ARRAY R63, R3           ; Collect node IDs of R3 into R63 vector
    // 5: HALT
    std::vector<impulse_instruction_t> bytecode1 = {
        { OP_INIT_INPUT_NODE, 0, 0, 0 },
        { OP_CSR_WALK, 0, 1, 0 | (0 << 16) }, // dst=1, src=0, rel=0 (userToGroup)
        { OP_CSR_WALK, 0, 2, 1 | (1 << 16) }, // dst=2, src=1, rel=1 (roleInheritance)
        { OP_CSR_WALK, 0, 3, 2 | (2 << 16) }, // dst=3, src=2, rel=2 (rolePermission)
        { OP_COLLECT_ARRAY, 0, 63, 3 },       // dst=63, src=3
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state1{};
    state1.query_context = ctx;

    impulse_vm_status_t status1 = impulse_vm_execute(
        bytecode1.data(), bytecode1.size(), &state1, 0ULL
    );

    assert(status1 == IMPULSE_VM_OK);
    assert(state1.register_types[63] == TYPE_NODE_VECTOR);
    assert(impulse_vm_context_get_vector_size(ctx) == 1);
    const uint64_t* res1 = reinterpret_cast<const uint64_t*>(state1.registers[63]);
    assert(res1 != nullptr && res1[0] == 5); // Read Permission (node ID 5)

    // --- PROGRAM 2: Transitive closure loop using OP_STABLE_CHECK ---
    // We start with Admin (Role 4) in R4 and want to find all direct and inherited roles: {4, 3, 2}.
    // 0: INIT_INPUT_NODE R4, 0           ; R4 = 4 (Admin)
    // 1: MOV R5, R4                      ; R5 = R4 = {4} (All accumulated roles set)
    // ; Loop start at PC 2:
    // 2: MOV R6, R5                      ; R6 = R5 (Save old set to R6)
    // 3: CSR_WALK R5, R6, REL_1 (accum)  ; R5 = R5 | walk(R6, REL_1)
    // 4: STABLE_CHECK R6, R5             ; ST = ZF = (R5 subset of R6) -> converged?
    // 5: JNZ 0, 0, -3                    ; If ZF == 0 (not stable yet), jump back to PC 2 (-3 offset)
    // 6: CSR_WALK R7, R5, REL_2          ; R7 = Walk rolePermission from accumulated roles {4, 3, 2}
    // 7: COLLECT_ARRAY R63, R7           ; Return permissions array
    // 8: HALT
    std::vector<impulse_instruction_t> bytecode2 = {
        { OP_INIT_INPUT_NODE, 0, 4, 0 },
        { OP_MOV, 0, 5, 4 },
        { OP_MOV, 0, 6, 5 },
        { OP_CSR_WALK, IMPULSE_VM_OP_FLAG_ACCUMULATE, 5, 6 | (1 << 16) },
        { OP_STABLE_CHECK, 0, 6, 5 },
        { OP_JNZ, 0, 0, static_cast<uint32_t>(-3) },
        { OP_CSR_WALK, 0, 7, 5 | (2 << 16) },
        { OP_COLLECT_ARRAY, 0, 63, 7 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state2{};
    state2.query_context = ctx;

    impulse_vm_status_t status2 = impulse_vm_execute(
        bytecode2.data(), bytecode2.size(), &state2, 4ULL
    );

    assert(status2 == IMPULSE_VM_OK);
    assert(state2.register_types[63] == TYPE_NODE_VECTOR);
    size_t size2 = impulse_vm_context_get_vector_size(ctx);
    const uint64_t* res2 = reinterpret_cast<const uint64_t*>(state2.registers[63]);
    assert(size2 == 3); // Read (5), Write (6), Delete (7)
    assert(res2 != nullptr);
    std::vector<uint64_t> actual_perms(res2, res2 + size2);
    std::sort(actual_perms.begin(), actual_perms.end());
    assert(actual_perms[0] == 5);
    assert(actual_perms[1] == 6);
    assert(actual_perms[2] == 7);

    // --- PROGRAM 3: OP_CSR_DEGREE check ---
    // 0: INIT_INPUT_NODE R0, 0           ; R0 = 3 (Employee Role)
    // 1: CSR_DEGREE R1, R0, REL_1        ; R1 = degree of Node 3 in relation 1 (roleInheritance) -> should be 1
    // 2: HALT
    std::vector<impulse_instruction_t> bytecode3 = {
        { OP_INIT_INPUT_NODE, 0, 0, 0 },
        { OP_CSR_DEGREE, 0, 1, 0 | (1 << 16) },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state3{};
    state3.query_context = ctx;

    impulse_vm_status_t status3 = impulse_vm_execute(
        bytecode3.data(), bytecode3.size(), &state3, 3ULL
    );
    assert(status3 == IMPULSE_VM_OK);
    assert(state3.registers[1] == 1); // Employee inherits 1 role (Guest)
    assert(state3.register_types[1] == TYPE_INT64);

    // Clean up
    impulse_vm_context_destroy(ctx);
    impulse_snapshot_close(snap);
    std::remove(filename);

    std::cout << "[VM Test] 3-Tier RBAC Traversal & Stable Check (Phase 3): PASSED" << std::endl;
}

int main() {
    std::cout << "--- Impulse C++ VM Unit Test Suite ---" << std::endl;
    test_vm_state_layout_size();
    test_basic_nop_halt();
    test_scalar_load_and_mov();
    test_init_input_node();
    test_jmp_jz_jnz();
    test_loop_decr();
    test_bitset_operations();
    test_rbac_traversal();
    test_error_handling();
    std::cout << "All VM tests passed successfully!" << std::endl;
    return 0;
}
