cmake_minimum_required(VERSION 3.20)
project(impulse_graph VERSION 0.11.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Compiler Warnings -------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /wd4189 /we4474 /we4477 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS /utf-8 /permissive-)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wformat=2 -Werror=format)
endif()

# --- Assertion Options --------------------------------------------------
option(IMPULSE_ENABLE_ASSERTIONS "Enable internal debug invariant assertions" OFF)
option(IMPULSE_ENABLE_AUDIT_ASSERTIONS "Enable expensive structural audit assertions" OFF)
option(IMPULSE_COVERAGE_TEST "Fold IMPULSE_ALWAYS/IMPULSE_NEVER to constants for coverage/mutation runs" OFF)
option(IMPULSE_DISABLE_COMPUTED_GOTO "Disable computed goto and use switched opcode dispatch" OFF)
if(IMPULSE_ENABLE_ASSERTIONS)
    add_compile_definitions(IMPULSE_ENABLE_ASSERTIONS=1)
endif()
if(IMPULSE_ENABLE_AUDIT_ASSERTIONS)
    add_compile_definitions(IMPULSE_ENABLE_ASSERTIONS=1 IMPULSE_ENABLE_AUDIT_ASSERTIONS=1)
endif()
if(IMPULSE_COVERAGE_TEST)
    add_compile_definitions(IMPULSE_COVERAGE_TEST=1)
endif()
if(IMPULSE_DISABLE_COMPUTED_GOTO)
    add_compile_definitions(IMPULSE_DISABLE_COMPUTED_GOTO=1)
endif()

# --- Sanitizer Options --------------------------------------------------
option(IMPULSE_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(IMPULSE_SANITIZE_THREAD "Enable ThreadSanitizer (mutually exclusive with IMPULSE_SANITIZE)" OFF)
option(IMPULSE_ENABLE_OPENMP "Compile ImpulseVM intra-instruction OpenMP parallel paths" OFF)
if(IMPULSE_SANITIZE AND IMPULSE_SANITIZE_THREAD)
    message(FATAL_ERROR "IMPULSE_SANITIZE (ASan+UBSan) and IMPULSE_SANITIZE_THREAD (TSan) are mutually exclusive")
endif()
if(IMPULSE_SANITIZE)
    if(MSVC)
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined
                            -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()
if(IMPULSE_SANITIZE_THREAD)
    if(MSVC)
        message(FATAL_ERROR "IMPULSE_SANITIZE_THREAD is not supported with MSVC")
    endif()
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()
if(IMPULSE_ENABLE_OPENMP)
    find_package(OpenMP REQUIRED COMPONENTS CXX)
    message(STATUS "ImpulseVM OpenMP parallel paths enabled")
endif()

# --- MC/DC Coverage Option ----------------------------------------------
option(IMPULSE_ENABLE_MCDC_COVERAGE "Enable LLVM source-based MC/DC code coverage" OFF)
if(IMPULSE_ENABLE_MCDC_COVERAGE)
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping -fcoverage-mcdc)
    add_link_options(-fprofile-instr-generate)
endif()

# --- LibFuzzer Option ---------------------------------------------------
option(IMPULSE_ENABLE_LIBFUZZER "Build fuzz targets with LLVM libFuzzer (-fsanitize=fuzzer)" OFF)
if(IMPULSE_ENABLE_LIBFUZZER)
    add_compile_definitions(IMPULSE_LIBFUZZER_ENABLED=1)
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

set(IMPULSE_CORE_SOURCES
    src/impulse_graph.cpp
    src/impulse_snapshot_writer.cpp
    src/impulse_index.cpp
    src/impulse_simd.cpp
    src/impulse_test_control.cpp
    src/impulse_statement_c_api.cpp
    src/vm/vm_bytecode_emitter.cpp
    src/vm/vm_impscm_compiler.cpp
    src/vm/vm_ictx_v2.cpp
    src/vm/vm_validator.cpp
    src/vm/vm_memory_planner.cpp
    src/vm/vm_dispatch.cpp
    src/vm/vm_simd_hwy.cpp
)

# --- Shared Library (.dylib / .so / .dll) --------------------------------
add_library(impulse_graph SHARED ${IMPULSE_CORE_SOURCES})
target_compile_definitions(impulse_graph PRIVATE IMPULSE_BUILDING_DLL)
target_include_directories(impulse_graph PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(impulse_graph PUBLIC hwy)
if(IMPULSE_ENABLE_OPENMP)
    target_link_libraries(impulse_graph PRIVATE OpenMP::OpenMP_CXX)
endif()

# --- Static Library (.a / .lib) ------------------------------------------
add_library(impulse_graph_static STATIC ${IMPULSE_CORE_SOURCES})
target_include_directories(impulse_graph_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(impulse_graph_static PUBLIC hwy)
if(IMPULSE_ENABLE_OPENMP)
    target_link_libraries(impulse_graph_static PUBLIC $<BUILD_INTERFACE:OpenMP::OpenMP_CXX>)
endif()

# --- Install Targets & Exports -------------------------------------------
install(TARGETS impulse_graph impulse_graph_static
    EXPORT ImpulseGraphTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES
    include/impulse_format_v0_9.h
    include/impulse_format_v0_10.h
    include/impulse_format_v0_11.h
    include/impulse_graph.h
    include/impulse_assert.h
    include/impulse_hexio.h
    include/impulse_simd.h
    include/impulse_sha256.h
    include/impulse_math_ops.h
    include/impulse_index.h
    include/impulse_vm.h
    include/impulse_vm_v11.h
    include/impulse_cel.h
    include/impulse_scm_ast.hpp
    include/impulse_compiler.hpp
    include/impulse_cypher.hpp
    include/impulse_datalog.hpp
    include/impulse_impk.hpp
    include/impulse_implog.hpp
    include/impulse_impscm.hpp
    include/vm/impulse_bytecode_emitter.hpp
    include/vm/impulse_impscm.hpp
    include/impulse_sexpr.hpp
    DESTINATION include
)
install(EXPORT ImpulseGraphTargets
    FILE ImpulseGraphConfig.cmake
    NAMESPACE ImpulseGraph::
    DESTINATION lib/cmake/ImpulseGraph
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ImpulseGraphConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ImpulseGraphConfigVersion.cmake"
    DESTINATION lib/cmake/ImpulseGraph
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/impulse-graph.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/impulse-graph.pc"
    @ONLY
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/impulse-graph.pc"
    DESTINATION lib/pkgconfig
)

# --- Tests ---------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_IMPULSE_DEFAULT_BUILD_TESTS ON)
else()
    set(_IMPULSE_DEFAULT_BUILD_TESTS OFF)
endif()
option(IMPULSE_BUILD_TESTS "Build impulse-graph tests" ${_IMPULSE_DEFAULT_BUILD_TESTS})
if(IMPULSE_BUILD_TESTS)
    enable_testing()

    set(IMPULSE_ACTIVE_TESTS
        test_assert
        impulse_graph_test
        test_vector_suite
        simd_test
        test_permutation_index
        test_cel_parser
        test_snapshot_mcdc_boundaries
        bench_cel_compiler
        test_cypher_compiler
        test_datalog_compiler
        test_c_statement_api
        test_impscm_mcdc_boundaries
        test_impscm_decimal64
        test_imps_malicious_hardening
        test_property_snapshot
        test_vm
    )
    foreach(t ${IMPULSE_ACTIVE_TESTS})
        add_executable(${t} tests/${t}.cpp)
        target_link_libraries(${t} PRIVATE impulse_graph_static)
        add_test(NAME ${t} COMMAND ${t})
    endforeach()

    add_executable(vm_fixture_batch_runner tests/vm_fixture_batch_runner.cpp)
    target_link_libraries(vm_fixture_batch_runner PRIVATE impulse_graph_static)

    add_executable(bench_vm_e2e tests/bench_vm_e2e.cpp)
    target_link_libraries(bench_vm_e2e PRIVATE impulse_graph_static)

    add_executable(bench_storage_coverage tests/bench_storage_coverage.cpp)
    target_link_libraries(bench_storage_coverage PRIVATE impulse_graph_static)
endif()

# --- Fuzz Targets (libFuzzer / standalone mutation driver) ---------------
option(IMPULSE_BUILD_FUZZ "Build fuzz targets (standalone mutation driver) and ctest smoke runs" OFF)
option(IMPULSE_ENABLE_LIBFUZZER "Build fuzz targets with LLVM libFuzzer (-fsanitize=fuzzer)" OFF)
set(IMPULSE_FUZZ_SMOKE_RUNS 1500 CACHE STRING "Mutation iterations per fuzz ctest smoke test")
if(IMPULSE_BUILD_FUZZ OR IMPULSE_ENABLE_LIBFUZZER)
    if(MSVC)
        message(FATAL_ERROR "Fuzz targets are not supported with MSVC")
    endif()
    set(IMPULSE_FUZZ_TARGETS fuzz_snapshot fuzz_dsl_frontends fuzz_cel_parser fuzz_vm_bytecode)
    set(fuzz_corpus_fuzz_snapshot "")
    set(fuzz_corpus_fuzz_vm_bytecode "")
    set(fuzz_corpus_fuzz_dsl_frontends ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz_corpus/dsl)
    set(fuzz_corpus_fuzz_cel_parser ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz_corpus/cel)
    foreach(fz ${IMPULSE_FUZZ_TARGETS})
        add_executable(${fz} tests/${fz}.cpp)
        target_link_libraries(${fz} PRIVATE impulse_graph_static)
        target_include_directories(${fz} PRIVATE tests)
        if(IMPULSE_ENABLE_LIBFUZZER)
            target_compile_definitions(${fz} PRIVATE IMPULSE_LIBFUZZER_ENABLED=1)
            target_compile_options(${fz} PRIVATE -fsanitize=fuzzer-no-link)
            target_link_options(${fz} PRIVATE -fsanitize=fuzzer)
        else()
            add_test(NAME ${fz}_smoke
                     COMMAND ${fz} -runs=${IMPULSE_FUZZ_SMOKE_RUNS} ${fuzz_corpus_${fz}})
        endif()
    endforeach()
endif()
