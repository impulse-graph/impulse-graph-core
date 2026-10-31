cmake_minimum_required(VERSION 3.20)
project(impulse_python LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(pybind11 REQUIRED)

include_directories(
    ../impulse-cpp/include
    ../impulse-cpp/src
)

pybind11_add_module(_impulse_native
    src/impulse_python.cpp
    ../impulse-cpp/src/impulse_graph.cpp
    ../impulse-cpp/src/impulse_snapshot_writer.cpp
    ../impulse-cpp/src/impulse_index.cpp
)
