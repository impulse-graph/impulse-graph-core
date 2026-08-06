#include "impulse_vm.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>

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

int main() {
    std::cout << "--- Impulse C++ VM Unit Test Suite (Phase 1) ---" << std::endl;
    test_vm_state_layout_size();
    test_basic_nop_halt();
    test_scalar_load_and_mov();
    test_init_input_node();
    test_jmp_jz_jnz();
    test_loop_decr();
    test_error_handling();
    std::cout << "All Phase 1 VM tests passed successfully!" << std::endl;
    return 0;
}
