#include <iostream>
#include <vector>
#include <cassert>
#include "../impulse-cpp/include/impulse_vm.h"

int main() {
    impulse_vm_state_t state{};
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    state.query_context = ctx;
    float raw_floats[8] = { -2.0f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 2.0f, 3.0f };
    impulse_vm_context_bind_inline_data(ctx, raw_floats, sizeof(raw_floats));

    std::vector<impulse_instruction_t> p1 = { { OP_INIT_MOCK_GRAPH, 0, 0, 8 | (8 << 16) }, { OP_HALT, 0, 0, 0 } };
    std::cout << "1..." << std::endl;
    impulse_vm_execute(p1.data(), p1.size(), &state, 0);

    std::vector<impulse_instruction_t> p2 = { { OP_LOAD_INLINE_ARRAY, 0, 1, 0 | (8 << 16) }, { OP_HALT, 0, 0, 0 } };
    std::cout << "2..." << std::endl;
    impulse_vm_execute(p2.data(), p2.size(), &state, 0);

    std::vector<impulse_instruction_t> p3 = { { OP_VEC_MATH_UNARY, 0, 2, (uint32_t)(MATH_FUNC_EXP | (1 << 8)) }, { OP_HALT, 0, 0, 0 } };
    std::cout << "3..." << std::endl;
    impulse_vm_execute(p3.data(), p3.size(), &state, 0);

    return 0;
}
