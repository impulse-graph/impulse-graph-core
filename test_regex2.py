import re

with open('impulse-cpp/src/impulse_vm.cpp', 'r') as f:
    content = f.read()

pattern = re.compile(r'^op_([A-Z0-9_]+):\s*\{(.*?)\n\}\n', re.MULTILINE | re.DOTALL)
for match in pattern.finditer(content):
    impl = match.group(2)
    if not impl.strip().endswith('DISPATCH();'):
        print(f"FAILED on {match.group(1)}: ends with\n{impl[-50:]}")
