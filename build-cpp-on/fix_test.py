import re

with open('../impulse-cpp/tests/test_vm_vector_math.cpp', 'r') as f:
    content = f.read()

content = content.replace(
'''    float raw_floats[8] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f };
    impulse_vm_context_bind_inline_data(ctx, raw_floats, sizeof(raw_floats));

    impulse_vm_status_t status = impulse_vm_execute(program.data(), program.size(), &state, 0);''',
'''    float raw_floats[8] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f };
    impulse_vm_context_bind_inline_data(ctx, raw_floats, sizeof(raw_floats));

    uint32_t dummy_offsets[9] = {0};
    uint32_t dummy_targets[1] = {0};
    impulse_vm_context_mock_csr(ctx, 0, dummy_offsets, dummy_targets, 8, 0);

    impulse_vm_status_t status = impulse_vm_execute(program.data(), program.size(), &state, 0);'''
)

content = content.replace(
'''    float clean_data[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    impulse_vm_context_bind_inline_data(ctx1, clean_data, sizeof(clean_data));
    impulse_vm_status_t st1 = impulse_vm_execute(clean_prog.data(), clean_prog.size(), &state1, 0);''',
'''    float clean_data[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    impulse_vm_context_bind_inline_data(ctx1, clean_data, sizeof(clean_data));
    uint32_t dummy_offsets[5] = {0};
    uint32_t dummy_targets[1] = {0};
    impulse_vm_context_mock_csr(ctx1, 0, dummy_offsets, dummy_targets, 4, 0);
    impulse_vm_status_t st1 = impulse_vm_execute(clean_prog.data(), clean_prog.size(), &state1, 0);'''
)

content = content.replace(
'''    float nan_data[4] = { 1.0f, nan_val, 3.0f, 4.0f };
    impulse_vm_context_bind_inline_data(ctx2, nan_data, sizeof(nan_data));
    impulse_vm_status_t st2 = impulse_vm_execute(nan_prog.data(), nan_prog.size(), &state2, 0);''',
'''    float nan_data[4] = { 1.0f, nan_val, 3.0f, 4.0f };
    impulse_vm_context_bind_inline_data(ctx2, nan_data, sizeof(nan_data));
    uint32_t dummy_offsets[5] = {0};
    uint32_t dummy_targets[1] = {0};
    impulse_vm_context_mock_csr(ctx2, 0, dummy_offsets, dummy_targets, 4, 0);
    impulse_vm_status_t st2 = impulse_vm_execute(nan_prog.data(), nan_prog.size(), &state2, 0);'''
)

with open('../impulse-cpp/tests/test_vm_vector_math.cpp', 'w') as f:
    f.write(content)
