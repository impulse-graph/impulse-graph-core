import re
with open('../impulse-cpp/tests/test_vm_vector_math.cpp', 'r') as f:
    content = f.read()

content = content.replace("uint32_t dummy_offsets[5] = {0};", "uint32_t dummy_offsets_a[5] = {0};", 1)
content = content.replace("uint32_t dummy_targets[1] = {0};", "uint32_t dummy_targets_a[1] = {0};", 1)
content = content.replace("impulse_vm_context_mock_csr(ctx1, 0, dummy_offsets, dummy_targets, 4, 0);", "impulse_vm_context_mock_csr(ctx1, 0, dummy_offsets_a, dummy_targets_a, 4, 0);")

content = content.replace("uint32_t dummy_offsets[5] = {0};", "uint32_t dummy_offsets_b[5] = {0};", 1)
content = content.replace("uint32_t dummy_targets[1] = {0};", "uint32_t dummy_targets_b[1] = {0};", 1)
content = content.replace("impulse_vm_context_mock_csr(ctx2, 0, dummy_offsets, dummy_targets, 4, 0);", "impulse_vm_context_mock_csr(ctx2, 0, dummy_offsets_b, dummy_targets_b, 4, 0);")

with open('../impulse-cpp/tests/test_vm_vector_math.cpp', 'w') as f:
    f.write(content)
