import os
import shutil
from setuptools import setup, find_packages
from pybind11.setup_helpers import Pybind11Extension, build_ext

this_dir = os.path.dirname(os.path.abspath(__file__))
cpp_dir = os.path.abspath(os.path.join(this_dir, "..", "impulse-cpp"))

local_cpp_dir = os.path.join(this_dir, "impulse-cpp")
if os.path.exists(cpp_dir):
    if os.path.exists(local_cpp_dir):
        shutil.rmtree(local_cpp_dir)
    inc_dir = os.path.abspath(os.path.join(cpp_dir, "include"))
    src_dir = os.path.abspath(os.path.join(cpp_dir, "src"))
    rel_src_prefix = "../impulse-cpp/src"
elif os.path.exists(local_cpp_dir):
    inc_dir = os.path.abspath(os.path.join(local_cpp_dir, "include"))
    src_dir = os.path.abspath(os.path.join(local_cpp_dir, "src"))
    rel_src_prefix = "impulse-cpp/src"
else:
    inc_dir = "../impulse-cpp/include"
    src_dir = "../impulse-cpp/src"
    rel_src_prefix = "../impulse-cpp/src"

import subprocess

hwy_dir = os.path.abspath(os.path.join(cpp_dir, "build", "_deps", "highway-src"))
if not os.path.exists(hwy_dir) and os.path.exists(os.path.join(cpp_dir, "CMakeLists.txt")):
    try:
        subprocess.run(["cmake", "-B", "build", "-DCMAKE_BUILD_TYPE=Release", "-DHWY_ENABLE_CONTRIB=OFF", "-DHWY_ENABLE_TESTS=OFF", "-DHWY_ENABLE_EXAMPLES=OFF"], cwd=cpp_dir, check=True)
    except Exception:
        pass

hwy_sources = []
for hwy_file in ["targets.cc", "per_target.cc", "nanobenchmark.cc", "aligned_allocator.cc", "timer.cc"]:
    full_hwy_file = os.path.join(hwy_dir, "hwy", hwy_file)
    if os.path.exists(full_hwy_file):
        hwy_sources.append(full_hwy_file)

hwy_lib = os.path.abspath(os.path.join(cpp_dir, "build", "_deps", "highway-build", "libhwy.a"))
extra_objects = [hwy_lib] if os.path.exists(hwy_lib) else []

import sys
extra_compile_args = ["/std:c++20", "/EHsc", "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN"] if sys.platform == "win32" else ["-std=c++20"]

ext_modules = [
    Pybind11Extension(
        "impulse_graph._impulse_native",
        [
            "src/impulse_python.cpp",
            f"{rel_src_prefix}/impulse_graph.cpp",
            f"{rel_src_prefix}/impulse_index.cpp",
            f"{rel_src_prefix}/impulse_simd.cpp",
            f"{rel_src_prefix}/impulse_vm.cpp",
            f"{rel_src_prefix}/impulse_vm_fluent.cpp",
        ] + (hwy_sources if not extra_objects else []),
        include_dirs=[
            inc_dir,
            src_dir,
            hwy_dir,
        ],
        extra_objects=extra_objects,
        extra_compile_args=extra_compile_args,
        cxx_std=20,
    ),
]

setup(
    name="impulse-graph",
    version="0.9.0",
    description="Impulse Graph Engine Python SDK & C++ Bytecode VM Binding",
    author="Impulse Graph Team",
    packages=find_packages(),
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    python_requires=">=3.8",
    classifiers=[
        "Programming Language :: Python :: 3",
        "Programming Language :: C++",
        "License :: OSI Approved :: Apache Software License",
        "Operating System :: OS Independent",
    ],
)
