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
    try:
        shutil.copytree(cpp_dir, local_cpp_dir, ignore=shutil.ignore_patterns("build", "cmake-build-debug", ".git"))
    except Exception:
        pass


if os.path.exists(local_cpp_dir):
    inc_dir = "impulse-cpp/include"
    src_prefix = "impulse-cpp/src"
else:
    inc_dir = "../impulse-cpp/include"
    src_prefix = "../impulse-cpp/src"

ext_modules = [
    Pybind11Extension(
        "_impulse_native",
        [
            "src/impulse_python.cpp",
            f"{src_prefix}/impulse_graph.cpp",
            f"{src_prefix}/impulse_simd.cpp",
            f"{src_prefix}/impulse_vm.cpp",
            f"{src_prefix}/impulse_vm_fluent.cpp",
        ],
        include_dirs=[
            inc_dir,
        ],
        cxx_std=20,
    ),
]

setup(
    name="impulse-graph",
    version="2.4.0",
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
