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

void test_attribute_filtering_and_math() {
    const char* filename = "__vm_test_attr_math.bin";
    std::remove(filename);

    impulse_writer_t* writer = impulse_writer_create(filename, 0);
    assert(writer != nullptr);

    impulse_status_t st = impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT32, "nodes");
    assert(st == IMPULSE_OK);

    // 5 nodes, 5 edges (triangle + self/extra edges)
    // Relation 0 (edges): Node 0->1, 1->2, 2->0, 3->4, 4->3
    const uint32_t offsets[] = { 0, 1, 2, 3, 4, 5 };
    const uint32_t targets[] = { 1, 2, 0, 4, 3 };

    st = impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, 5, 5, 0,
                                     offsets, sizeof(offsets),
                                     targets, sizeof(targets));
    assert(st == IMPULSE_OK);

    // Add age attribute (INT32, dimension 1)
    const int32_t ages[] = { 10, 20, 30, 20, 50 };
    st = impulse_writer_add_attribute(writer, 0, "age", 0x03, 1, ages, sizeof(ages), nullptr, 0);
    assert(st == IMPULSE_OK);

    // Add name attribute (fixed-width string, dimension 4)
    const char names[] = "ALIC" "BOB\0" "ALAN" "CHAR" "ALEX";
    st = impulse_writer_add_attribute(writer, 0, "name", 0x0B, 4, names, sizeof(names) - 1, nullptr, 0);
    assert(st == IMPULSE_OK);

    st = impulse_writer_finalize(writer);
    if (st != IMPULSE_OK) {
        std::cerr << "[FINALIZE ERROR] " << impulse_get_last_error() << std::endl;
    }
    assert(st == IMPULSE_OK);
    impulse_writer_destroy(writer);

    // Open snapshot and context
    impulse_snapshot_t* snap = impulse_snapshot_open(filename, &st);
    assert(snap != nullptr);
    assert(st == IMPULSE_OK);

    impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
    assert(ctx != nullptr);

    // --- Part 1: OP_NODE_FILTER (age == 20) ---
    // Register 1: Source bitset (containing {0, 1, 2, 3, 4})
    // Register 2: Comparison value (20)
    // Register 3: Destination bitset
    impulse_vm_state_t state1{};
    state1.query_context = ctx;

    // Load nodes 0..4 into bitset R1
    int h_src = impulse_vm_context_acquire_bitset(ctx);
    assert(h_src >= 0);
    for (uint64_t i = 0; i < 5; ++i) {
        impulse_vm_context_bitset_add(ctx, h_src, i);
    }
    state1.registers[1] = h_src;
    state1.register_types[1] = TYPE_BITSET_HANDLE;
    state1.registers[2] = 20;
    state1.register_types[2] = TYPE_INT64;

    // opcode: OP_NODE_FILTER, dst=3, payload: src=1, val_reg=2, attr_id=0, rel_id=0
    std::vector<impulse_instruction_t> bytecode1 = {
        { OP_NODE_FILTER, 0, 3, 1 | (2 << 8) | (0 << 16) | (0 << 24) },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_status_t status1 = impulse_vm_execute(
        bytecode1.data(), bytecode1.size(), &state1, 0
    );
    assert(status1 == IMPULSE_VM_OK);
    assert(state1.register_types[3] == TYPE_BITSET_HANDLE);

    int h_dst = static_cast<int>(state1.registers[3]);
    // Expect nodes 1 and 3 to be matched (ages[1]=20, ages[3]=20)
    assert(impulse_vm_context_bitset_test(ctx, h_dst, 1));
    assert(impulse_vm_context_bitset_test(ctx, h_dst, 3));
    assert(!impulse_vm_context_bitset_test(ctx, h_dst, 0));
    assert(!impulse_vm_context_bitset_test(ctx, h_dst, 2));
    assert(!impulse_vm_context_bitset_test(ctx, h_dst, 4));

    // Release bitsets
    impulse_vm_context_release_bitset(ctx, h_src);
    impulse_vm_context_release_bitset(ctx, h_dst);

    // --- Part 2: OP_NODE_FILTER_STR_PREFIX (name.startsWith("AL")) ---
    impulse_vm_state_t state2{};
    state2.query_context = ctx;

    int h_src2 = impulse_vm_context_acquire_bitset(ctx);
    for (uint64_t i = 0; i < 5; ++i) {
        impulse_vm_context_bitset_add(ctx, h_src2, i);
    }
    state2.registers[1] = h_src2;
    state2.register_types[1] = TYPE_BITSET_HANDLE;
    state2.registers[2] = reinterpret_cast<uint64_t>("AL");
    state2.register_types[2] = TYPE_INT64;

    // opcode: OP_NODE_FILTER_STR_PREFIX, dst=3, payload: src=1, val_reg=2, attr_id=1, rel_id=0
    std::vector<impulse_instruction_t> bytecode2 = {
        { OP_NODE_FILTER_STR_PREFIX, 0, 3, 1 | (2 << 8) | (1 << 16) | (0 << 24) },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_status_t status2 = impulse_vm_execute(
        bytecode2.data(), bytecode2.size(), &state2, 0
    );
    assert(status2 == IMPULSE_VM_OK);
    int h_dst2 = static_cast<int>(state2.registers[3]);
    // Expect nodes 0 (ALIC), 2 (ALAN), 4 (ALEX) to match
    assert(impulse_vm_context_bitset_test(ctx, h_dst2, 0));
    assert(impulse_vm_context_bitset_test(ctx, h_dst2, 2));
    assert(impulse_vm_context_bitset_test(ctx, h_dst2, 4));
    assert(!impulse_vm_context_bitset_test(ctx, h_dst2, 1));
    assert(!impulse_vm_context_bitset_test(ctx, h_dst2, 3));

    impulse_vm_context_release_bitset(ctx, h_src2);
    impulse_vm_context_release_bitset(ctx, h_dst2);

    // --- Part 3: Vector Math & CSR Walk PageRank ---
    impulse_vm_state_t state3{};
    state3.query_context = ctx;

    // Allocate input vector R5 (all 1.0f)
    int h_v5 = impulse_vm_context_acquire_float_vector(ctx);
    assert(h_v5 >= 0);
    for (size_t i = 0; i < 5; ++i) {
        impulse_vm_context_float_vector_set(ctx, h_v5, i, 1.0f);
    }
    state3.registers[5] = h_v5;
    state3.register_types[5] = TYPE_FLOAT_VECTOR;

    // Allocate denom vector R1 (all 1.0f)
    int h_v1 = impulse_vm_context_acquire_float_vector(ctx);
    assert(h_v1 >= 0);
    for (size_t i = 0; i < 5; ++i) {
        impulse_vm_context_float_vector_set(ctx, h_v1, i, 1.0f);
    }
    state3.registers[1] = h_v1;
    state3.register_types[1] = TYPE_FLOAT_VECTOR;

    float damping = 0.85f;
    uint32_t damping_payload = reinterpret_cast<uint32_t&>(damping);

    std::vector<impulse_instruction_t> bytecode3 = {
        { OP_VECTOR_DIV, 0, 5, 5 | (1 << 16) },
        { OP_CSR_WALK_REDUCE_SUM, 0, 6, 5 | (0xFF << 8) | (0 << 16) }, // attr_id=0xFF (no edge attribute weight)
        { OP_VECTOR_MUL_ATTR, 0, 6, damping_payload },
        { OP_VECTOR_REDUCE_SUM, 0, 7, 6 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_status_t status3 = impulse_vm_execute(
        bytecode3.data(), bytecode3.size(), &state3, 0
    );
    assert(status3 == IMPULSE_VM_OK);

    // Verify R6 values (nodes 0, 1, 2 should be 0.85f; nodes 3, 4 should be 0.85f)
    int h_v6 = static_cast<int>(state3.registers[6]);
    const float* res_v6 = impulse_vm_context_get_float_vector(ctx, h_v6);
    assert(res_v6 != nullptr);
    assert(std::abs(res_v6[0] - 0.85f) < 1e-5f);
    assert(std::abs(res_v6[1] - 0.85f) < 1e-5f);
    assert(std::abs(res_v6[2] - 0.85f) < 1e-5f);
    assert(std::abs(res_v6[3] - 0.85f) < 1e-5f);
    assert(std::abs(res_v6[4] - 0.85f) < 1e-5f);

    // Verify R7 sum reduction (0.85 * 5 = 4.25f)
    float sum_val = reinterpret_cast<float&>(state3.registers[7]);
    assert(std::abs(sum_val - 4.25f) < 1e-5f);

    // Clean up
    impulse_vm_context_release_float_vector(ctx, h_v5);
    impulse_vm_context_release_float_vector(ctx, h_v1);
    impulse_vm_context_release_float_vector(ctx, h_v6);
    impulse_vm_context_destroy(ctx);
    impulse_snapshot_close(snap);
    std::remove(filename);

    std::cout << "[VM Test] Attribute Filtering & Math Opcodes (Phase 4): PASSED" << std::endl;
}

void test_subroutines_and_key_resolutions() {
    const char* filename = "__vm_test_phase5.bin";
    std::remove(filename);

    impulse_writer_t* writer = impulse_writer_create(filename, 0);
    assert(writer != nullptr);

    impulse_status_t st = impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_STRING, "nodes");
    assert(st == IMPULSE_OK);

    // 5 nodes, 0 edges
    const uint32_t offsets[] = { 0, 0, 0, 0, 0, 0 };
    const uint32_t targets[] = { 0 };

    st = impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, 5, 0, 0,
                                     offsets, sizeof(offsets),
                                     targets, 0);
    assert(st == IMPULSE_OK);

    // Add name attribute (fixed-width string, dimension 4)
    const char names[] = "ALIC" "BOB\0" "ALAN" "CHAR" "ALEX";
    st = impulse_writer_add_attribute(writer, 0, "name", 0x0B, 4, names, sizeof(names) - 1, nullptr, 0);
    assert(st == IMPULSE_OK);

    st = impulse_writer_finalize(writer);
    assert(st == IMPULSE_OK);
    impulse_writer_destroy(writer);

    // Open snapshot and context
    impulse_snapshot_t* snap = impulse_snapshot_open(filename, &st);
    assert(snap != nullptr);
    assert(st == IMPULSE_OK);

    impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
    assert(ctx != nullptr);

    // --- Part 1: Subroutine CALL & RET ---
    std::vector<impulse_instruction_t> bytecode1 = {
        { OP_LOAD_CONST_INT, 0, 10, 5 },
        { OP_CALL, 0, 0, 3 },            // Call PC 3
        { OP_HALT, 0, 0, 0 },
        { OP_LOAD_CONST_INT, 0, 10, 99 },
        { OP_RET, 0, 0, 0 }
    };

    impulse_vm_state_t state1{};
    state1.query_context = ctx;

    impulse_vm_status_t status1 = impulse_vm_execute(
        bytecode1.data(), bytecode1.size(), &state1, 0
    );
    assert(status1 == IMPULSE_VM_OK);
    assert(state1.registers[10] == 99);
    assert(state1.call_stack_depth == 0); // Correctly popped

    // --- Part 2: Key Mappings (OP_MAP_KEYS_TO_DENSE & OP_MAP_DENSE_TO_KEYS) ---
    // Load input keys "BOB" and "ALAN"
    const char* keys[] = { "BOB", "ALAN" };
    impulse_vm_input_keys input_keys{};
    input_keys.keys = keys;
    input_keys.count = 2;

    // opcode: OP_MAP_KEYS_TO_DENSE R3, DOMAIN_0 (0)
    // opcode: OP_MAP_DENSE_TO_KEYS R4, R3, DOMAIN_0 (0)
    // opcode: OP_HALT
    std::vector<impulse_instruction_t> bytecode2 = {
        { OP_MAP_KEYS_TO_DENSE, 0, 3, 0 },
        { OP_MAP_DENSE_TO_KEYS, 0, 4, 3 | (0 << 8) },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state2{};
    state2.query_context = ctx;

    impulse_vm_status_t status2 = impulse_vm_execute(
        bytecode2.data(), bytecode2.size(), &state2, reinterpret_cast<uint64_t>(&input_keys)
    );
    assert(status2 == IMPULSE_VM_OK);

    // Verify R3 is a bitset containing nodes 1 (BOB) and 2 (ALAN)
    int h_bs = static_cast<int>(state2.registers[3]);
    assert(state2.register_types[3] == TYPE_BITSET_HANDLE);
    assert(impulse_vm_context_bitset_test(ctx, h_bs, 1));
    assert(impulse_vm_context_bitset_test(ctx, h_bs, 2));
    assert(!impulse_vm_context_bitset_test(ctx, h_bs, 0));

    // Verify R4 is a string vector containing "BOB\0" and "ALAN"
    int h_svec = static_cast<int>(state2.registers[4]);
    assert(state2.register_types[4] == TYPE_STRING_VECTOR);
    assert(impulse_vm_context_string_vector_size(ctx, h_svec) == 2);
    const char* key0 = impulse_vm_context_string_vector_get(ctx, h_svec, 0);
    const char* key1 = impulse_vm_context_string_vector_get(ctx, h_svec, 1);
    assert(key0 != nullptr && std::strncmp(key0, "BOB", 3) == 0);
    assert(key1 != nullptr && std::strncmp(key1, "ALAN", 4) == 0);

    // Release pools
    impulse_vm_context_release_bitset(ctx, h_bs);
    impulse_vm_context_release_string_vector(ctx, h_svec);

    // --- Part 3: Value Map Collection (OP_COLLECT_VALUE_MAP) ---
    // R1: Node bitset containing { 1, 3 } ("BOB", "CHAR")
    // R5: Float vector with values: Node 1 = 12.34f, Node 3 = 56.78f
    int h_nodes = impulse_vm_context_acquire_bitset(ctx);
    impulse_vm_context_bitset_add(ctx, h_nodes, 1);
    impulse_vm_context_bitset_add(ctx, h_nodes, 3);

    int h_vals = impulse_vm_context_acquire_float_vector(ctx);
    impulse_vm_context_float_vector_set(ctx, h_vals, 1, 12.34f);
    impulse_vm_context_float_vector_set(ctx, h_vals, 3, 56.78f);

    impulse_vm_state_t state3{};
    state3.query_context = ctx;
    state3.registers[1] = h_nodes;
    state3.register_types[1] = TYPE_BITSET_HANDLE;
    state3.registers[5] = h_vals;
    state3.register_types[5] = TYPE_FLOAT_VECTOR;

    // opcode: OP_COLLECT_VALUE_MAP R6, nodes_reg=1, vals_reg=5, domain_id=0
    std::vector<impulse_instruction_t> bytecode3 = {
        { OP_COLLECT_VALUE_MAP, 0, 6, 1 | (5 << 8) | (0 << 16) },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_status_t status3 = impulse_vm_execute(
        bytecode3.data(), bytecode3.size(), &state3, 0
    );
    assert(status3 == IMPULSE_VM_OK);

    int h_vmap = static_cast<int>(state3.registers[6]);
    assert(state3.register_types[6] == TYPE_VALUE_MAP);
    assert(impulse_vm_context_value_map_size(ctx, h_vmap) == 2);

    const char* vkey0 = impulse_vm_context_value_map_get_key(ctx, h_vmap, 0);
    float vval0 = impulse_vm_context_value_map_get_value(ctx, h_vmap, 0);
    const char* vkey1 = impulse_vm_context_value_map_get_key(ctx, h_vmap, 1);
    float vval1 = impulse_vm_context_value_map_get_value(ctx, h_vmap, 1);

    assert(vkey0 != nullptr && std::strncmp(vkey0, "BOB", 3) == 0);
    assert(std::abs(vval0 - 12.34f) < 1e-4f);
    assert(vkey1 != nullptr && std::strncmp(vkey1, "CHAR", 4) == 0);
    assert(std::abs(vval1 - 56.78f) < 1e-4f);

    // Clean up
    impulse_vm_context_release_bitset(ctx, h_nodes);
    impulse_vm_context_release_float_vector(ctx, h_vals);
    impulse_vm_context_release_value_map(ctx, h_vmap);
    impulse_vm_context_destroy(ctx);
    impulse_snapshot_close(snap);
    std::remove(filename);

    std::cout << "[VM Test] Subroutines & Key Resolutions (Phase 5): PASSED" << std::endl;
}

void test_csc_walk() {
    const char* filename = "__vm_test_csc.bin";
    std::remove(filename);

    impulse_writer_t* writer = impulse_writer_create(filename, 0);
    assert(writer != nullptr);

    impulse_status_t st = impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_STRING, "nodes");
    assert(st == IMPULSE_OK);

    // 5 nodes, 0 edges (dummy graph)
    const uint32_t offsets[] = { 0, 0, 0, 0, 0, 0 };
    const uint32_t targets[] = { 0 };
    st = impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, 5, 0, 0,
                                     offsets, sizeof(offsets),
                                     targets, 0);
    assert(st == IMPULSE_OK);

    st = impulse_writer_finalize(writer);
    assert(st == IMPULSE_OK);
    impulse_writer_destroy(writer);

    impulse_snapshot_t* snap = impulse_snapshot_open(filename, &st);
    if (!snap) {
        std::cerr << "test_csc_walk failed to open snapshot: status=" << st << " (" << impulse_get_last_error() << ")" << std::endl;
    }
    assert(snap != nullptr);

    impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
    assert(ctx != nullptr);

    // Mock relation slot 0 CSC buffers
    // Target 0 has incoming edges from 1 and 2
    uint32_t csc_offsets[] = { 0, 2, 2, 2, 2, 2 };
    uint32_t csc_targets[] = { 1, 2 };
    impulse_vm_context_mock_csc(ctx, 0, csc_offsets, csc_targets);

    // Bytecode:
    // R1 = current frontier (bitset containing node 1 and node 2)
    // R2 = unvisited set (bitset containing node 0)
    // R3 = next frontier (OP_CSC_WALK R3, R1 | (R2 << 16) | (0 << 24))
    std::vector<impulse_instruction_t> bytecode = {
        { OP_LOAD_CONST_INT, 0, 4, 1 }, // R4 = 1
        { OP_LOAD_CONST_INT, 0, 5, 2 }, // R5 = 2
        { OP_LOAD_CONST_INT, 0, 6, 0 }, // R6 = 0
        { OP_SET_UNION, 0, 1, 4 },       // R1 = {1}
        { OP_SET_UNION, 0, 1, 5 },       // R1 = {1, 2} (frontier)
        { OP_SET_UNION, 0, 2, 6 },       // R2 = {0} (unvisited)
        { OP_CSC_WALK, 0, 3, 1 | (2 << 16) | (0 << 24) }, // R3 = CSC walk from R1 on R2 (relation 0)
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state{};
    state.query_context = ctx;

    impulse_vm_status_t status = impulse_vm_execute(
        bytecode.data(), bytecode.size(), &state, 0
    );
    assert(status == IMPULSE_VM_OK);

    // Assert that R3 is indeed a bitset containing node 0!
    int h_dst = static_cast<int>(state.registers[3]);
    assert(state.register_types[3] == TYPE_BITSET_HANDLE);
    assert(impulse_vm_context_bitset_test(ctx, h_dst, 0) == true);
    assert(impulse_vm_context_bitset_test(ctx, h_dst, 1) == false);

    impulse_vm_context_release_bitset(ctx, state.registers[1]);
    impulse_vm_context_release_bitset(ctx, state.registers[2]);
    impulse_vm_context_release_bitset(ctx, state.registers[3]);
    impulse_vm_context_destroy(ctx);
    impulse_snapshot_close(snap);
    std::remove(filename);

    std::cout << "[VM Test] CSC Bottom-Up Walk (OP_CSC_WALK): PASSED" << std::endl;
}

void test_graphblas_opcodes() {
    const char* filename = "__vm_test_graphblas_snapshot.bin";
    std::remove(filename);

    impulse_writer_t* writer = impulse_writer_create(filename, 0);
    assert(writer != nullptr);

    impulse_status_t st = impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT32, "nodes");
    assert(st == IMPULSE_OK);

    // 3 nodes, 2 edges: 0->1, 1->2
    const uint32_t offsets[] = { 0, 1, 2, 2 };
    const uint32_t targets[] = { 1, 2 };

    st = impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, 3, 2, 0,
                                     offsets, sizeof(offsets),
                                     targets, sizeof(targets));
    assert(st == IMPULSE_OK);

    st = impulse_writer_finalize(writer);
    assert(st == IMPULSE_OK);
    impulse_writer_destroy(writer);

    // Open snapshot and context
    impulse_snapshot_t* snap = impulse_snapshot_open(filename, &st);
    assert(snap != nullptr);
    assert(st == IMPULSE_OK);

    impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
    assert(ctx != nullptr);

    // Mock CSC: 0<-none, 1<-0, 2<-1
    const uint32_t csc_offsets[] = { 0, 0, 1, 2 };
    const uint32_t csc_targets[] = { 0, 1 };
    impulse_vm_context_mock_csc(ctx, 0, csc_offsets, csc_targets);

    // --- PROGRAM 1: OP_MXV (Plus-Times) and OP_REDUCE ---
    // Initialize input float vector R1 = [1.0f, 2.0f, 3.0f]
    int h_v1 = impulse_vm_context_acquire_float_vector(ctx);
    impulse_vm_context_float_vector_set(ctx, h_v1, 0, 1.0f);
    impulse_vm_context_float_vector_set(ctx, h_v1, 1, 2.0f);
    impulse_vm_context_float_vector_set(ctx, h_v1, 2, 3.0f);

    std::vector<impulse_instruction_t> bytecode1 = {
        { OP_MXV, 0, 2, 1 | (0 << 8) | (SEMIRING_PLUS_TIMES << 16) },
        { OP_REDUCE, 0, 3, 2 | (BINARY_OP_ADD << 16) },
        { OP_REDUCE, 0, 4, 2 | (BINARY_OP_MAX << 16) },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state1{};
    state1.query_context = ctx;
    state1.registers[1] = h_v1;
    state1.register_types[1] = TYPE_FLOAT_VECTOR;

    impulse_vm_status_t status1 = impulse_vm_execute(
        bytecode1.data(), bytecode1.size(), &state1, 0
    );
    assert(status1 == IMPULSE_VM_OK);

    // Assert that dst vector R2 is a float vector with [2.0f, 3.0f, 0.0f]
    int h_v2 = static_cast<int>(state1.registers[2]);
    assert(state1.register_types[2] == TYPE_FLOAT_VECTOR);
    const float* v2_data = impulse_vm_context_get_float_vector(ctx, h_v2);
    assert(v2_data[0] == 2.0f);
    assert(v2_data[1] == 3.0f);
    assert(v2_data[2] == 0.0f);

    // Assert reduction sum (R3) is 5.0f
    assert(state1.register_types[3] == TYPE_FLOAT);
    float sum_val = reinterpret_cast<float&>(state1.registers[3]);
    assert(sum_val == 5.0f);

    // Assert reduction max (R4) is 3.0f
    assert(state1.register_types[4] == TYPE_FLOAT);
    float max_val = reinterpret_cast<float&>(state1.registers[4]);
    assert(max_val == 3.0f);

    impulse_vm_context_release_float_vector(ctx, h_v2);

    // --- PROGRAM 2: OP_VXM (Plus-Times) ---
    // Multiply transpose of adjacency matrix by v1.
    // Expecting: [0.0f, 1.0f, 2.0f]
    std::vector<impulse_instruction_t> bytecode2 = {
        { OP_VXM, 0, 2, 1 | (0 << 8) | (SEMIRING_PLUS_TIMES << 16) },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state2{};
    state2.query_context = ctx;
    state2.registers[1] = h_v1;
    state2.register_types[1] = TYPE_FLOAT_VECTOR;

    impulse_vm_status_t status2 = impulse_vm_execute(
        bytecode2.data(), bytecode2.size(), &state2, 0
    );
    assert(status2 == IMPULSE_VM_OK);

    int h_v2_vxm = static_cast<int>(state2.registers[2]);
    const float* vxm_data = impulse_vm_context_get_float_vector(ctx, h_v2_vxm);
    assert(vxm_data[0] == 0.0f);
    assert(vxm_data[1] == 1.0f);
    assert(vxm_data[2] == 2.0f);

    impulse_vm_context_release_float_vector(ctx, h_v1);

    // --- PROGRAM 3: OP_EWISE_ADD and OP_EWISE_MULT ---
    // Initialize input float vector R5 = [2.0f, 2.0f, 2.0f]
    int h_v5 = impulse_vm_context_acquire_float_vector(ctx);
    impulse_vm_context_float_vector_set(ctx, h_v5, 0, 2.0f);
    impulse_vm_context_float_vector_set(ctx, h_v5, 1, 2.0f);
    impulse_vm_context_float_vector_set(ctx, h_v5, 2, 2.0f);

    std::vector<impulse_instruction_t> bytecode3 = {
        { OP_EWISE_ADD, 0, 6, 2 | (5 << 8) | (BINARY_OP_ADD << 16) },
        { OP_EWISE_MULT, 0, 7, 2 | (5 << 8) | (BINARY_OP_MUL << 16) },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state3{};
    state3.query_context = ctx;
    state3.registers[2] = h_v2_vxm;
    state3.register_types[2] = TYPE_FLOAT_VECTOR;
    state3.registers[5] = h_v5;
    state3.register_types[5] = TYPE_FLOAT_VECTOR;

    impulse_vm_status_t status3 = impulse_vm_execute(
        bytecode3.data(), bytecode3.size(), &state3, 0
    );
    assert(status3 == IMPULSE_VM_OK);

    int h_v6 = static_cast<int>(state3.registers[6]);
    const float* v6_data = impulse_vm_context_get_float_vector(ctx, h_v6);
    assert(v6_data[0] == 2.0f);
    assert(v6_data[1] == 3.0f);
    assert(v6_data[2] == 4.0f);

    int h_v7 = static_cast<int>(state3.registers[7]);
    const float* v7_data = impulse_vm_context_get_float_vector(ctx, h_v7);
    assert(v7_data[0] == 0.0f);
    assert(v7_data[1] == 2.0f);
    assert(v7_data[2] == 4.0f);

    // Clean up
    impulse_vm_context_release_float_vector(ctx, h_v2_vxm);
    impulse_vm_context_release_float_vector(ctx, h_v5);
    impulse_vm_context_release_float_vector(ctx, h_v6);
    impulse_vm_context_release_float_vector(ctx, h_v7);
    impulse_vm_context_destroy(ctx);
    impulse_snapshot_close(snap);
    std::remove(filename);

    std::cout << "[VM Test] GraphBLAS Opcodes (OP_MXV, OP_VXM, OP_EWISE, OP_REDUCE): PASSED" << std::endl;
}

void test_extra_opcodes() {
    const char* filename = "__vm_test_extra_snapshot.bin";
    std::remove(filename);

    impulse_writer_t* writer = impulse_writer_create(filename, 0);
    assert(writer != nullptr);

    impulse_status_t st = impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT32, "nodes");
    assert(st == IMPULSE_OK);

    // 3 nodes, 2 edges: 0->1, 1->2
    const uint32_t offsets[] = { 0, 1, 2, 2 };
    const uint32_t targets[] = { 1, 2 };

    st = impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, 3, 2, 0,
                                     offsets, sizeof(offsets),
                                     targets, sizeof(targets));
    assert(st == IMPULSE_OK);

    st = impulse_writer_finalize(writer);
    assert(st == IMPULSE_OK);
    impulse_writer_destroy(writer);

    impulse_snapshot_t* snap = impulse_snapshot_open(filename, &st);
    assert(snap != nullptr);
    assert(st == IMPULSE_OK);

    impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
    assert(ctx != nullptr);

    // --- PROGRAM 1: OP_CC_AFFOREST ---
    std::vector<impulse_instruction_t> bytecode1 = {
        { OP_CC_AFFOREST, 0, 1, 0 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state1{};
    state1.query_context = ctx;

    impulse_vm_status_t status1 = impulse_vm_execute(
        bytecode1.data(), bytecode1.size(), &state1, 0
    );
    assert(status1 == IMPULSE_VM_OK);
    assert(state1.register_types[1] == TYPE_UINT64_VECTOR);
    int h_comp = static_cast<int>(state1.registers[1]);
    const uint64_t* comp_data = impulse_vm_context_get_node_vector(ctx, h_comp);
    assert(comp_data[0] == comp_data[1]);
    assert(comp_data[1] == comp_data[2]);

    // --- PROGRAM 2: OP_COLLECT_BITSET ---
    int h_bs1 = impulse_vm_context_acquire_bitset(ctx);
    impulse_vm_context_bitset_add(ctx, h_bs1, 0);

    std::vector<impulse_instruction_t> bytecode2 = {
        { OP_COLLECT_BITSET, 0, 2, 1 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state2{};
    state2.query_context = ctx;
    state2.registers[1] = h_bs1;
    state2.register_types[1] = TYPE_BITSET_HANDLE;

    impulse_vm_status_t status2 = impulse_vm_execute(
        bytecode2.data(), bytecode2.size(), &state2, 0
    );
    assert(status2 == IMPULSE_VM_OK);
    assert(state2.register_types[2] == TYPE_BITSET_HANDLE);
    assert(state2.registers[2] == static_cast<uint64_t>(h_bs1));

    // --- PROGRAM 3: OP_CSR_WALK_REDUCE ---
    int h_f3 = impulse_vm_context_acquire_float_vector(ctx);
    impulse_vm_context_float_vector_set(ctx, h_f3, 0, 10.0f);
    impulse_vm_context_float_vector_set(ctx, h_f3, 1, 20.0f);
    impulse_vm_context_float_vector_set(ctx, h_f3, 2, 30.0f);

    std::vector<impulse_instruction_t> bytecode3 = {
        { OP_CSR_WALK_REDUCE, 0, 4, 3 | (0 << 8) | (0 << 16) },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state3{};
    state3.query_context = ctx;
    state3.registers[3] = h_f3;
    state3.register_types[3] = TYPE_FLOAT_VECTOR;

    impulse_vm_status_t status3 = impulse_vm_execute(
        bytecode3.data(), bytecode3.size(), &state3, 0
    );
    assert(status3 == IMPULSE_VM_OK);
    int h_f4 = static_cast<int>(state3.registers[4]);
    assert(state3.register_types[4] == TYPE_FLOAT_VECTOR);
    const float* f4_data = impulse_vm_context_get_float_vector(ctx, h_f4);
    assert(f4_data[0] == 0.0f);
    assert(f4_data[1] == 10.0f);
    assert(f4_data[2] == 20.0f);

    // Clean up
    impulse_vm_context_release_node_vector(ctx, h_comp);
    impulse_vm_context_release_bitset(ctx, h_bs1);
    impulse_vm_context_release_float_vector(ctx, h_f3);
    impulse_vm_context_release_float_vector(ctx, h_f4);
    impulse_vm_context_destroy(ctx);
    impulse_snapshot_close(snap);
    std::remove(filename);

    std::cout << "[VM Test] Extra Opcodes (OP_CC_AFFOREST, OP_COLLECT_BITSET, OP_CSR_WALK_REDUCE): PASSED" << std::endl;
}

void test_pagerank_bytecode() {
    const char* filename = "__vm_test_pagerank_snapshot.bin";
    std::remove(filename);

    impulse_writer_t* writer = impulse_writer_create(filename, 0);
    assert(writer != nullptr);

    impulse_status_t st = impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT32, "nodes");
    assert(st == IMPULSE_OK);

    // 3 nodes, 3 edges: 0->1, 1->2, 2->0 (directed cycle)
    const uint32_t offsets[] = { 0, 1, 2, 3 };
    const uint32_t targets[] = { 1, 2, 0 };

    st = impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, 3, 3, 0,
                                     offsets, sizeof(offsets),
                                     targets, sizeof(targets));
    assert(st == IMPULSE_OK);

    st = impulse_writer_finalize(writer);
    assert(st == IMPULSE_OK);
    impulse_writer_destroy(writer);

    impulse_snapshot_t* snap = impulse_snapshot_open(filename, &st);
    assert(snap != nullptr);
    assert(st == IMPULSE_OK);

    impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
    assert(ctx != nullptr);

    // Mock CSC: 0<-2, 1<-0, 2<-1 (directed cycle)
    const uint32_t csc_offsets[] = { 0, 1, 2, 3 };
    const uint32_t csc_targets[] = { 2, 0, 1 };
    impulse_vm_context_mock_csc(ctx, 0, csc_offsets, csc_targets);

    // R1: p = [1/3, 1/3, 1/3]
    int h_p = impulse_vm_context_acquire_float_vector(ctx);
    impulse_vm_context_float_vector_set(ctx, h_p, 0, 1.0f / 3.0f);
    impulse_vm_context_float_vector_set(ctx, h_p, 1, 1.0f / 3.0f);
    impulse_vm_context_float_vector_set(ctx, h_p, 2, 1.0f / 3.0f);

    // R2: d = [1.0f, 1.0f, 1.0f]
    int h_d = impulse_vm_context_acquire_float_vector(ctx);
    impulse_vm_context_float_vector_set(ctx, h_d, 0, 1.0f);
    impulse_vm_context_float_vector_set(ctx, h_d, 1, 1.0f);
    impulse_vm_context_float_vector_set(ctx, h_d, 2, 1.0f);

    // R3: t = [0.05f, 0.05f, 0.05f] ( (1 - beta) / N = 0.15 / 3 = 0.05 )
    int h_t = impulse_vm_context_acquire_float_vector(ctx);
    impulse_vm_context_float_vector_set(ctx, h_t, 0, 0.05f);
    impulse_vm_context_float_vector_set(ctx, h_t, 1, 0.05f);
    impulse_vm_context_float_vector_set(ctx, h_t, 2, 0.05f);

    float damping = 0.85f;
    uint32_t damping_payload = reinterpret_cast<uint32_t&>(damping);

    std::vector<impulse_instruction_t> bytecode = {
        { OP_LOAD_CONST_INT, 0, 6, 4 }, // iterations = 4
        // .loop: (PC = 1)
        { OP_VECTOR_DIV, 0, 4, 1 | (2 << 16) }, // R4 = R1 / R2
        { OP_VXM, 0, 5, 4 | (0 << 8) | (SEMIRING_PLUS_TIMES << 16) }, // R5 = A^T * R4
        { OP_VECTOR_MUL_ATTR, 0, 5, damping_payload }, // R5 = R5 * 0.85f
        { OP_EWISE_ADD, 0, 1, 5 | (3 << 8) | (BINARY_OP_ADD << 16) }, // R1 = R5 + R3
        { OP_LOOP_DECR, 0, 6, static_cast<uint32_t>(-4) }, // loop PC=1
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state{};
    state.query_context = ctx;
    state.registers[1] = h_p;
    state.register_types[1] = TYPE_FLOAT_VECTOR;
    state.registers[2] = h_d;
    state.register_types[2] = TYPE_FLOAT_VECTOR;
    state.registers[3] = h_t;
    state.register_types[3] = TYPE_FLOAT_VECTOR;

    impulse_vm_status_t status = impulse_vm_execute(
        bytecode.data(), bytecode.size(), &state, 0
    );
    assert(status == IMPULSE_VM_OK);

    // Verify PageRank converges to exactly 1/3 = 0.33333f for all 3 nodes
    const float* p_data = impulse_vm_context_get_float_vector(ctx, h_p);
    assert(std::abs(p_data[0] - 1.0f / 3.0f) < 1e-4f);
    assert(std::abs(p_data[1] - 1.0f / 3.0f) < 1e-4f);
    assert(std::abs(p_data[2] - 1.0f / 3.0f) < 1e-4f);

    // Clean up
    impulse_vm_context_release_float_vector(ctx, h_p);
    impulse_vm_context_release_float_vector(ctx, h_d);
    impulse_vm_context_release_float_vector(ctx, h_t);
    if (state.register_types[4] == TYPE_FLOAT_VECTOR) {
        impulse_vm_context_release_float_vector(ctx, state.registers[4]);
    }
    if (state.register_types[5] == TYPE_FLOAT_VECTOR) {
        impulse_vm_context_release_float_vector(ctx, state.registers[5]);
    }
    impulse_vm_context_destroy(ctx);
    impulse_snapshot_close(snap);
    std::remove(filename);

    std::cout << "[VM Test] PageRank Bytecode: PASSED" << std::endl;
}

void test_new_opcodes() {
    impulse_vm_status_t st{};
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr); // Snapshot-less context
    assert(ctx != nullptr);

    // Prepare inline binary payload (mock CSR: node 0 -> [1, 2], node 1 -> [2], node 2 -> [3])
    // Offsets: [0, 2, 3, 4]
    // Targets: [1, 2, 2, 3]
    uint32_t inline_graph[] = { 0, 2, 3, 4, 1, 2, 2, 3 };
    float inline_weights[] = { 10.5f, 20.5f, 30.5f, 40.5f };

    std::vector<uint8_t> payload_buf(sizeof(inline_graph) + sizeof(inline_weights));
    std::memcpy(payload_buf.data(), inline_graph, sizeof(inline_graph));
    std::memcpy(payload_buf.data() + sizeof(inline_graph), inline_weights, sizeof(inline_weights));

    impulse_vm_context_bind_inline_data(ctx, payload_buf.data(), payload_buf.size());

    // 1. Test OP_INIT_MOCK_GRAPH & OP_LOAD_INLINE_ARRAY
    // Payload layout:
    // Slot 0 at offset 0, node_count = 3
    // Array R1 at offset sizeof(inline_graph), count = 4
    std::vector<impulse_instruction_t> prog_inline = {
        { OP_INIT_MOCK_GRAPH, 0, 0, 0 | (3 << 16) },                                   // Slot 0
        { OP_LOAD_INLINE_ARRAY, 0, 1, static_cast<uint32_t>(sizeof(inline_graph)) | (4 << 16) }, // R1
        { OP_LOAD_CONST_INT, 0, 10, 2 },                                               // R10 = 2 (index)
        { OP_LOAD_INDIRECT, 1, 2, 1 | (10 << 16) },                                    // R2 = Vector[R1][R10] (flags=1)
        { OP_ASSERT, 0, 2, 0x41F40000 },                                               // Assert R2 == 30.5f (bits 0x41F40000)
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state1{};
    state1.query_context = ctx;
    impulse_vm_status_t status1 = impulse_vm_execute(prog_inline.data(), prog_inline.size(), &state1, 0);
    assert(status1 == IMPULSE_VM_OK);

    // 2. Test OP_LOAD_INDIRECT (Register-Indirect Mode: R3 = R[R4] where R4 = 5, R5 = 77)
    std::vector<impulse_instruction_t> prog_reg_indirect = {
        { OP_LOAD_CONST_INT, 0, 5, 77 },
        { OP_LOAD_CONST_INT, 0, 4, 5 },
        { OP_LOAD_INDIRECT, 0, 3, 4 }, // R3 = R[R4] = R5 = 77 (flags=0)
        { OP_ASSERT, 0, 3, 77 },       // Assert R3 == 77
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state2{};
    state2.query_context = ctx;
    impulse_vm_status_t status2 = impulse_vm_execute(prog_reg_indirect.data(), prog_reg_indirect.size(), &state2, 0);
    assert(status2 == IMPULSE_VM_OK);
    assert(state2.registers[3] == 77);

    // 3. Test OP_THROW
    std::vector<impulse_instruction_t> prog_throw = {
        { OP_THROW, 0, 0, 404 }
    };
    impulse_vm_state_t state3{};
    state3.query_context = ctx;
    impulse_vm_status_t status3 = impulse_vm_execute(prog_throw.data(), prog_throw.size(), &state3, 0);
    assert(status3 == IMPULSE_VM_ERR_USER_THROW);
    assert(state3.registers[0] == 404);

    // 4. Test OP_ASSERT Failure
    std::vector<impulse_instruction_t> prog_assert_fail = {
        { OP_LOAD_CONST_INT, 0, 0, 10 },
        { OP_ASSERT, 0, 0, 99 } // Expected 99, actual 10
    };
    impulse_vm_state_t state4{};
    state4.query_context = ctx;
    impulse_vm_status_t status4 = impulse_vm_execute(prog_assert_fail.data(), prog_assert_fail.size(), &state4, 0);
    assert(status4 == IMPULSE_VM_ERR_ASSERTION_FAILED);

    // 5. Test OP_TRAP
    std::vector<impulse_instruction_t> prog_trap = {
        { OP_TRAP, 0, 0, 1 }
    };
    impulse_vm_state_t state5{};
    state5.query_context = ctx;
    impulse_vm_status_t status5 = impulse_vm_execute(prog_trap.data(), prog_trap.size(), &state5, 0);
    assert(status5 == IMPULSE_VM_ERR_TRAP);

    impulse_vm_context_destroy(ctx);
    std::cout << "[VM Test] 6 New Opcodes (INLINE_ARRAY, MOCK_GRAPH, INDIRECT, THROW, ASSERT, TRAP): PASSED" << std::endl;
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
    test_attribute_filtering_and_math();
    test_subroutines_and_key_resolutions();
    test_csc_walk();
    test_graphblas_opcodes();
    test_extra_opcodes();
    test_pagerank_bytecode();
    test_new_opcodes();
    test_error_handling();
    std::cout << "All VM tests passed successfully!" << std::endl;
    return 0;
}
