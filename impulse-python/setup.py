import os
import shutil
import subprocess
import sys

from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import find_packages, setup

this_dir = os.path.dirname(os.path.abspath(__file__))
cpp_dir = os.path.abspath(os.path.join(this_dir, "..", "impulse-cpp"))
local_cpp_dir = os.path.join(this_dir, "impulse-cpp")

# Synchronize required C++ engine headers and core translation units into local_cpp_dir
# when building from monorepo repository so sdist and wheel builds are 100% self-contained.
if os.path.exists(cpp_dir) and os.path.exists(os.path.join(cpp_dir, "include")):
    os.makedirs(os.path.join(local_cpp_dir, "include"), exist_ok=True)
    os.makedirs(os.path.join(local_cpp_dir, "src"), exist_ok=True)
    shutil.copytree(os.path.join(cpp_dir, "include"), os.path.join(local_cpp_dir, "include"), dirs_exist_ok=True)
    for cpp_file in [
        "impulse_bytecode_emitter.cpp",
        "impulse_graph.cpp",
        "impulse_index.cpp",
        "impulse_simd.cpp",
        "impulse_vm.cpp",
        "impulse_vm_fluent.cpp",
    ]:
        src_file = os.path.join(cpp_dir, "src", cpp_file)
        if os.path.exists(src_file):
            shutil.copy2(src_file, os.path.join(local_cpp_dir, "src", cpp_file))

if os.path.exists(local_cpp_dir) and os.path.exists(os.path.join(local_cpp_dir, "src", "impulse_graph.cpp")):
    inc_dir = os.path.abspath(os.path.join(local_cpp_dir, "include"))
    src_dir = os.path.abspath(os.path.join(local_cpp_dir, "src"))
    rel_src_prefix = "impulse-cpp/src"
else:
    inc_dir = os.path.abspath(os.path.join(cpp_dir, "include"))
    src_dir = os.path.abspath(os.path.join(cpp_dir, "src"))
    rel_src_prefix = "../impulse-cpp/src"

hwy_dir = os.path.abspath(os.path.join(cpp_dir, "build", "_deps", "highway-src"))
if not os.path.exists(hwy_dir) and os.path.exists(os.path.join(cpp_dir, "CMakeLists.txt")):
    try:
        subprocess.run(
            [
                "cmake",
                "-B",
                "build",
                "-DCMAKE_BUILD_TYPE=Release",
                "-DHWY_ENABLE_CONTRIB=OFF",
                "-DHWY_ENABLE_TESTS=OFF",
                "-DHWY_ENABLE_EXAMPLES=OFF",
            ],
            cwd=cpp_dir,
            check=True,
        )
    except Exception:
        pass

hwy_sources = []
for hwy_file in [
    "targets.cc",
    "per_target.cc",
    "nanobenchmark.cc",
    "aligned_allocator.cc",
    "timer.cc",
    "abort.cc",
    "base.cc",
    "stats.cc",
    "print.cc",
]:
    full_hwy_file = os.path.join(hwy_dir, "hwy", hwy_file)
    if os.path.exists(full_hwy_file):
        hwy_sources.append(full_hwy_file)

hwy_lib = os.path.abspath(os.path.join(cpp_dir, "build", "_deps", "highway-build", "libhwy.a"))
extra_objects = [hwy_lib] if os.path.exists(hwy_lib) else []

extra_compile_args = (
    [
        "/std:c++20",
        "/EHsc",
        "/DNOMINMAX",
        "/DWIN32_LEAN_AND_MEAN",
        "/D_CRT_SECURE_NO_WARNINGS",
        "/utf-8",
        "/permissive-",
    ]
    if sys.platform == "win32"
    else ["-std=c++20"]
)

version = os.environ.get("IMPULSE_VERSION", "0.9.0").strip()
if version.startswith("v"):
    version = version[1:]

ext_modules = [
    Pybind11Extension(
        "impulse_graph._impulse_native",
        [
            "src/impulse_python.cpp",
            f"{rel_src_prefix}/impulse_bytecode_emitter.cpp",
            f"{rel_src_prefix}/impulse_graph.cpp",
            f"{rel_src_prefix}/impulse_index.cpp",
            f"{rel_src_prefix}/impulse_simd.cpp",
            f"{rel_src_prefix}/impulse_vm.cpp",
            f"{rel_src_prefix}/impulse_vm_fluent.cpp",
        ]
        + (hwy_sources if not extra_objects else []),
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
    version=version,
    description="Impulse Graph Engine Python SDK & C++ Bytecode VM Binding",
    author="Impulse Graph Team",
    packages=find_packages(),
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    python_requires=">=3.8",
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "Intended Audience :: Science/Research",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
        "Programming Language :: C++",
        "Operating System :: OS Independent",
        "Topic :: Scientific/Engineering",
        "Topic :: Scientific/Engineering :: Artificial Intelligence",
        "Topic :: Database",
        "Topic :: Software Development :: Libraries :: Python Modules",
    ],
)

