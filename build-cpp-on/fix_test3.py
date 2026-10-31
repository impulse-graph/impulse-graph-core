import re
with open('../impulse-cpp/tests/test_vm_vector_math.cpp', 'r') as f:
    content = f.read()

# find all `uint32_t dummy_targets...` and rename them uniquely
counter = 0
def repl(m):
    global counter
    counter += 1
    return f"uint32_t dummy_targets_{counter}[1] = {{0}};"

content = re.sub(r'uint32_t dummy_targets.*?\[1\] = \{0\};', repl, content)

c2 = 0
def repl2(m):
    global c2
    c2 += 1
    return f"dummy_targets_{c2}"

content = re.sub(r'dummy_targets_[a-d]', repl2, content)
content = re.sub(r'dummy_targets(?!_)', repl2, content)

with open('../impulse-cpp/tests/test_vm_vector_math.cpp', 'w') as f:
    f.write(content)
