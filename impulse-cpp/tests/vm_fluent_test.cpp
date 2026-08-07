#include "impulse_vm_fluent.hpp"
#include "impulse_graph.h"

#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>

void test_fluent_basic_input_and_const() {
    using namespace impulse::vm;

    QueryBuilder builder;
    builder.inputNode(0)
           .loadConstInt(42, 1)
           .loadConstFloat(3.14159f, 2)
           .mov(3, 1);

    CompiledQuery query = builder.compile();

    assert(query.instructionCount() == 5); // 4 insts + HALT
    assert(query.bytecode()[0].opcode == OP_INIT_INPUT_NODE);
    assert(query.bytecode()[1].opcode == OP_LOAD_CONST_INT);
    assert(query.bytecode()[1].payload == 42);
    assert(query.bytecode()[2].opcode == OP_LOAD_CONST_FLOAT);
    assert(query.bytecode()[3].opcode == OP_MOV);
    assert(query.bytecode()[4].opcode == OP_HALT);

    impulse_vm_state_t state{};
    QueryResult res = query.executeWithContext(nullptr, &state, 100);

    assert(res.isOk());
    assert(state.registers[0] == 100);
    assert(state.registers[1] == 42);
    assert(state.registers[3] == 42);

    std::cout << "[VM Fluent Test] Basic Input and Const: PASSED\n";
}

void test_fluent_graph_walk_and_sets() {
    using namespace impulse::vm;

    QueryBuilder builder;
    builder.inputNode(0)
           .walkEdge(0)
           .filterNode(10)
           .unionWith(1)
           .collectBitset();

    CompiledQuery query = builder.compile();

    assert(query.instructionCount() == 6);
    assert(query.bytecode()[0].opcode == OP_INIT_INPUT_NODE);
    assert(query.bytecode()[1].opcode == OP_CSR_WALK);
    assert(query.bytecode()[2].opcode == OP_NODE_FILTER);
    assert(query.bytecode()[3].opcode == OP_SET_UNION);
    assert(query.bytecode()[4].opcode == OP_COLLECT_BITSET);
    assert(query.bytecode()[5].opcode == OP_HALT);

    std::cout << "[VM Fluent Test] Graph Walk & Sets Compilation: PASSED\n";
}

void test_fluent_repeat_loops() {
    using namespace impulse::vm;

    QueryBuilder builder;
    builder.inputNode(0)
           .repeat(3, [](QueryBuilder& q) {
               q.walkEdge(0);
           })
           .collectBitset();

    CompiledQuery query = builder.compile();

    // insts:
    // 0: INIT_INPUT_NODE
    // 1: LOAD_CONST_INT (count = 3 into counter_reg R1)
    // 2: CSR_WALK
    // 3: LOOP_DECR (R1, offset = -1) -> jump back to inst 2
    // 4: COLLECT_BITSET
    // 5: HALT
    assert(query.instructionCount() == 6);
    assert(query.bytecode()[1].opcode == OP_LOAD_CONST_INT);
    assert(query.bytecode()[1].payload == 3);
    assert(query.bytecode()[2].opcode == OP_CSR_WALK);
    assert(query.bytecode()[3].opcode == OP_LOOP_DECR);
    assert(static_cast<int32_t>(query.bytecode()[3].payload) == -1);

    std::cout << "[VM Fluent Test] Repeat Loop Jump Offsets: PASSED\n";
}

void test_fluent_repeat_until_stable() {
    using namespace impulse::vm;

    QueryBuilder builder;
    builder.inputNode(0)
           .repeatUntilStable([](QueryBuilder& q) {
               q.walkEdge(0);
           });

    CompiledQuery query = builder.compile();

    assert(query.instructionCount() == 4);
    assert(query.bytecode()[0].opcode == OP_INIT_INPUT_NODE);
    assert(query.bytecode()[1].opcode == OP_CSR_WALK);
    assert(query.bytecode()[2].opcode == OP_STABLE_CHECK);
    assert(static_cast<int32_t>(query.bytecode()[2].payload) == -1);
    assert(query.bytecode()[3].opcode == OP_HALT);

    std::cout << "[VM Fluent Test] Repeat Until Stable Jump Offsets: PASSED\n";
}

void test_fluent_extended_domain_and_graphblas() {
    using namespace impulse::vm;

    QueryBuilder builder;
    builder.inputNode(0)
           .matrixVectorMul(2, SEMIRING_PLUS_TIMES)
           .ewiseAdd(3, BINARY_OP_ADD)
           .afforest()
           .rebacCheck(101)
           .sampleNeighbors(0, 10, 42);

    CompiledQuery query = builder.compile();

    assert(query.instructionCount() == 7);
    assert(query.bytecode()[1].opcode == OP_MXV);
    assert(query.bytecode()[2].opcode == OP_EWISE_ADD);
    assert(query.bytecode()[3].opcode == OP_CC_AFFOREST);
    assert(query.bytecode()[4].opcode == OP_REBAC_CHECK);
    assert(query.bytecode()[5].opcode == OP_SAMPLE_NEIGHBORS);

    std::cout << "[VM Fluent Test] Extended Domain & GraphBLAS Compilation: PASSED\n";
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << "       Impulse VM C++ Fluent Style API Test Suite         \n";
    std::cout << "=========================================================\n";

    test_fluent_basic_input_and_const();
    test_fluent_graph_walk_and_sets();
    test_fluent_repeat_loops();
    test_fluent_repeat_until_stable();
    test_fluent_extended_domain_and_graphblas();

    std::cout << "=========================================================\n";
    std::cout << " ALL VM FLUENT API TESTS PASSED SUCCESSFULLY!            \n";
    std::cout << "=========================================================\n";
    return 0;
}
