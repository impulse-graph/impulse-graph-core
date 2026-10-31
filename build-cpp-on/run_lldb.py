import subprocess
p = subprocess.Popen(["lldb", "./test_vm_vector_math"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
stdout, stderr = p.communicate("run\nbt\nquit\n")
print(stdout)
