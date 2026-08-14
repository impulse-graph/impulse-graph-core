cmake_minimum_required(VERSION 3.20)
project(impulse_graph VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Compiler Warnings -------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /wd4189)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# --- Sanitizer Option ---------------------------------------------------
option(IMPULSE_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(IMPULSE_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# --- Dependencies: Google Highway --------------------------------------
include(FetchContent)
FetchContent_Declare(
    highway
    GIT_REPOSITORY https://github.com/google/highway.git
    GIT_TAG 1.2.0
)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_CONTRIB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(highway)

# --- Shared Library (.dylib / .so / .dll) --------------------------------
add_library(impulse_graph SHARED src/impulse_graph.cpp src/impulse_index.cpp src/impulse_simd.cpp src/impulse_vm.cpp src/impulse_vm_fluent.cpp)
target_compile_definitions(impulse_graph PRIVATE IMPULSE_BUILDING_DLL)
target_include_directories(impulse_graph PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(impulse_graph PUBLIC hwy)

# --- Static Library (.a / .lib) ------------------------------------------
add_library(impulse_graph_static STATIC src/impulse_graph.cpp src/impulse_index.cpp src/impulse_simd.cpp src/impulse_vm.cpp src/impulse_vm_fluent.cpp)
target_include_directories(impulse_graph_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(impulse_graph_static PUBLIC hwy)

# --- Install Targets & Exports -------------------------------------------
install(TARGETS impulse_graph impulse_graph_static
    EXPORT ImpulseGraphTargets
    DESTINATION lib
)
install(FILES include/impulse_graph.h include/impulse_simd.h include/impulse_sha256.h include/impulse_vm.h include/impulse_vm_fluent.hpp
    DESTINATION include
)
install(EXPORT ImpulseGraphTargets
    FILE ImpulseGraphConfig.cmake
    NAMESPACE ImpulseGraph::
    DESTINATION lib/cmake/ImpulseGraph
)

# --- Tests ---------------------------------------------------------------
option(IMPULSE_BUILD_TESTS "Build impulse-graph tests" ON)
if(IMPULSE_BUILD_TESTS)
    enable_testing()
    add_executable(impulse_graph_test tests/impulse_graph_test.cpp)
    target_link_libraries(impulse_graph_test PRIVATE impulse_graph_static)
    add_test(NAME impulse_graph_test COMMAND impulse_graph_test)

    add_executable(test_vector_suite tests/test_vector_suite.cpp)
    target_link_libraries(test_vector_suite PRIVATE impulse_graph_static)
    add_test(NAME test_vector_suite COMMAND test_vector_suite)

    add_executable(simd_test tests/simd_test.cpp)
    target_link_libraries(simd_test PRIVATE impulse_graph_static)
    add_test(NAME simd_test COMMAND simd_test)

    add_executable(vm_test tests/vm_test.cpp)
    target_link_libraries(vm_test PRIVATE impulse_graph_static)
    add_test(NAME vm_test COMMAND vm_test)

    add_executable(vm_fluent_test tests/vm_fluent_test.cpp)
    target_link_libraries(vm_fluent_test PRIVATE impulse_graph_static)
    add_test(NAME vm_fluent_test COMMAND vm_fluent_test)

    add_executable(test_permutation_index tests/test_permutation_index.cpp)
    target_link_libraries(test_permutation_index PRIVATE impulse_graph_static)
    add_test(NAME test_permutation_index COMMAND test_permutation_index)

    add_executable(test_vm_vector_math tests/test_vm_vector_math.cpp)
    target_link_libraries(test_vm_vector_math PRIVATE impulse_graph_static)
    add_test(NAME test_vm_vector_math COMMAND test_vm_vector_math)

    add_executable(bench_secondary_indexes tests/bench_secondary_indexes.cpp)
    target_link_libraries(bench_secondary_indexes PRIVATE impulse_graph_static)

    add_executable(bench_bfs_lazy_csc tests/bench_bfs_lazy_csc.cpp)
    target_link_libraries(bench_bfs_lazy_csc PRIVATE impulse_graph_static)

    add_executable(build_northwind_imps /Users/jesse/impulse/datasets/northwind/build_northwind_imps.cpp)
    target_link_libraries(build_northwind_imps PRIVATE impulse_graph_static)

    add_executable(convert_neo4j_northwind /Users/jesse/impulse/datasets/northwind/convert_neo4j_northwind.cpp)
    target_link_libraries(convert_neo4j_northwind PRIVATE impulse_graph_static)

    add_executable(bench_scratch_indexes_lifecycle /Users/jesse/impulse/datasets/northwind/bench_scratch_indexes_lifecycle.cpp)
    target_link_libraries(bench_scratch_indexes_lifecycle PRIVATE impulse_graph_static)

    add_executable(bench_large_attribute_dataset /Users/jesse/impulse/datasets/bench_large_attribute_dataset.cpp)
    target_link_libraries(bench_large_attribute_dataset PRIVATE impulse_graph_static)
endif()
