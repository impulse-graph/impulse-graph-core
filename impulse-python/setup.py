from setuptools import setup, find_packages
from pybind11.setup_helpers import Pybind11Extension, build_ext

ext_modules = [
    Pybind11Extension(
        "_impulse_native",
        [
            "src/impulse_python.cpp",
            "../impulse-cpp/src/impulse_graph.cpp",
        ],
        include_dirs=[
            "../impulse-cpp/include",
        ],
        cxx_std=20,
    ),
]

setup(
    name="impulse-graph",
    version="2.4.0",
    description="Impulse Graph Engine Python SDK & Zero-Copy C-ABI Binary Snapshot Binding",
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
