import re

with open('impulse-cpp/src/impulse_vm.cpp', 'r') as f:
    content = f.read()

pattern = re.compile(r'^op_([A-Z0-9_]+):\s*\{(.*?)\n\}\n', re.MULTILINE | re.DOTALL)
for match in pattern.finditer(content):
    if match.group(1) == "VEC_MATH_UNARY":
        print(match.group(2)[-100:])
