import re
with open('../impulse-cpp/tests/test_vm_vector_math.cpp', 'r') as f:
    content = f.read()

# Replace the OP_INIT_MOCK_GRAPH opcode payload with 0 (since we will just NOP it)
content = content.replace('{ OP_INIT_MOCK_GRAPH, 0, 0, 8 | (8 << 16) }', '{ OP_NOP, 0, 0, 0 }')
content = content.replace('{ OP_INIT_MOCK_GRAPH, 0, 0, 4 | (4 << 16) }', '{ OP_NOP, 0, 0, 0 }')

# Now add a proper mock CSR call before impulse_vm_execute
# For the first case:
content = content.replace(
'''    float raw_floats[12] = { -2.0f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 2.0f, 3.0f, 0, 0, 0, 0 };
    impulse_vm_context_bind_inline_data(ctx, raw_floats, sizeof(raw_floats));

    impulse_vm_status_t status = impulse_vm_execute(program.data(), program.size(), &state, 0);''',
'''    float raw_floats[8] = { -2.0f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 2.0f, 3.0f };
    impulse_vm_context_bind_inline_data(ctx, raw_floats, sizeof(raw_floats));
    uint32_t doff1[9] = {0}; uint32_t dtar1[1] = {0};
    impulse_vm_context_mock_csr(ctx, 0, doff1, dtar1, 8, 0);
    impulse_vm_status_t status = impulse_vm_execute(program.data(), program.size(), &state, 0);'''
)

# For the second case:
content = content.replace(
'''    float raw_floats[12] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 0, 0, 0, 0 };
    impulse_vm_context_bind_inline_data(ctx, raw_floats, sizeof(raw_floats));

    impulse_vm_status_t status = impulse_vm_execute(program.data(), program.size(), &state, 0);''',
'''    float raw_floats[8] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f };
    impulse_vm_context_bind_inline_data(ctx, raw_floats, sizeof(raw_floats));
    uint32_t doff2[9] = {0}; uint32_t dtar2[1] = {0};
    impulse_vm_context_mock_csr(ctx, 0, doff2, dtar2, 8, 0);
    impulse_vm_status_t status = impulse_vm_execute(program.data(), program.size(), &state, 0);'''
)

# For the third case:
content = content.replace(
'''    float clean_data[12] = { 1.0f, 2.0f, 3.0f, 4.0f, 0, 0, 0, 0, 0, 0, 0, 0 };
    impulse_vm_context_bind_inline_data(ctx1, clean_data, sizeof(clean_data));
    impulse_vm_status_t st1 = impulse_vm_execute(clean_prog.data(), clean_prog.size(), &state1, 0);''',
'''    float clean_data[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    impulse_vm_context_bind_inline_data(ctx1, clean_data, sizeof(clean_data));
    uint32_t doff3[5] = {0}; uint32_t dtar3[1] = {0};
    impulse_vm_context_mock_csr(ctx1, 0, doff3, dtar3, 4, 0);
    impulse_vm_status_t st1 = impulse_vm_execute(clean_prog.data(), clean_prog.size(), &state1, 0);'''
)

# For the fourth case:
content = content.replace(
'''    float nan_data[12] = { 1.0f, nan_val, 3.0f, 4.0f, 0, 0, 0, 0, 0, 0, 0, 0 };
    impulse_vm_context_bind_inline_data(ctx2, nan_data, sizeof(nan_data));
    impulse_vm_status_t st2 = impulse_vm_execute(nan_prog.data(), nan_prog.size(), &state2, 0);''',
'''    float nan_data[4] = { 1.0f, nan_val, 3.0f, 4.0f };
    impulse_vm_context_bind_inline_data(ctx2, nan_data, sizeof(nan_data));
    uint32_t doff4[5] = {0}; uint32_t dtar4[1] = {0};
    impulse_vm_context_mock_csr(ctx2, 0, doff4, dtar4, 4, 0);
    impulse_vm_status_t st2 = impulse_vm_execute(nan_prog.data(), nan_prog.size(), &state2, 0);'''
)

with open('../impulse-cpp/tests/test_vm_vector_math.cpp', 'w') as f:
    f.write(content)
