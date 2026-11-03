cmake_minimum_required(VERSION 3.20)
project(impulse_graph VERSION 0.9.0 LANGUAGES C CXX)

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
# IMPULSE_SANITIZE        : AddressSanitizer + UndefinedBehaviorSanitizer. UBSan findings are
#                           fatal (-fno-sanitize-recover=undefined) so they fail ctest.
# IMPULSE_SANITIZE_THREAD : ThreadSanitizer (mutually exclusive with ASan/UBSan).
#                           NOTE: the OpenMP paths in src/opcodes/*.inc are guarded by `_OPENMP`
#                           and are compiled out unless IMPULSE_ENABLE_OPENMP=ON. libomp is not
#                           TSan-instrumented, so OpenMP+TSan reports false positives from the
#                           runtime's own barriers; use TSan with OpenMP OFF (default) unless an
#                           instrumented runtime (e.g. libgomp built with -fsanitize=thread or
#                           archer) is available.
option(IMPULSE_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(IMPULSE_SANITIZE_THREAD "Enable ThreadSanitizer (mutually exclusive with IMPULSE_SANITIZE)" OFF)
option(IMPULSE_ENABLE_OPENMP "Compile ImpulseVM intra-instruction OpenMP parallel paths" OFF)
if(IMPULSE_SANITIZE AND IMPULSE_SANITIZE_THREAD)
    message(FATAL_ERROR "IMPULSE_SANITIZE (ASan+UBSan) and IMPULSE_SANITIZE_THREAD (TSan) are mutually exclusive")
endif()
if(IMPULSE_SANITIZE)
    if(MSVC)
        # MSVC supports ASan only (/fsanitize=address); UBSan/TSan are not available.
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

# --- Shared Library (.dylib / .so / .dll) --------------------------------
add_library(impulse_graph SHARED
    src/impulse_graph.cpp
    src/impulse_snapshot_writer.cpp
    src/impulse_index.cpp
    src/impulse_simd.cpp
    src/impulse_vm.cpp
    src/impulse_vm_context.cpp
    src/impulse_vm_fluent.cpp
    src/impulse_vm_fluent_traversal.cpp
    src/impulse_vm_fluent_multilane.cpp
    src/impulse_statement_c_api.cpp
    src/impulse_bytecode_emitter.cpp
)
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
add_library(impulse_graph_static STATIC
    src/impulse_graph.cpp
    src/impulse_snapshot_writer.cpp
    src/impulse_index.cpp
    src/impulse_simd.cpp
    src/impulse_vm.cpp
    src/impulse_vm_context.cpp
    src/impulse_vm_fluent.cpp
    src/impulse_vm_fluent_traversal.cpp
    src/impulse_vm_fluent_multilane.cpp
    src/impulse_statement_c_api.cpp
    src/impulse_bytecode_emitter.cpp
)
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
    include/impulse_graph.h
    include/impulse_assert.h
    include/impulse_hexio.h
    include/impulse_simd.h
    include/impulse_sha256.h
    include/impulse_math_ops.h
    include/impulse_index.h
    include/impulse_vm.h
    include/impulse_vm_fluent.hpp
    include/impulse_cel.h
    include/impulse_scm_ast.hpp
    include/impulse_compiler.hpp
    include/impulse_cypher.hpp
    include/impulse_datalog.hpp
    include/impulse_impk.hpp
    include/impulse_implog.hpp
    include/impulse_impscm.hpp
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
    add_executable(test_assert tests/test_assert.cpp)
    target_link_libraries(test_assert PRIVATE impulse_graph_static)
    add_test(NAME test_assert COMMAND test_assert)

    add_executable(test_sqlite_harnesses tests/test_sqlite_harnesses.cpp)
    target_link_libraries(test_sqlite_harnesses PRIVATE impulse_graph_static)
    add_test(NAME test_sqlite_harnesses COMMAND test_sqlite_harnesses)

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

    add_executable(test_cel_parser tests/test_cel_parser.cpp)
    target_link_libraries(test_cel_parser PRIVATE impulse_graph_static)
    add_test(NAME test_cel_parser COMMAND test_cel_parser)

    add_executable(test_vm_mcdc_boundaries tests/test_vm_mcdc_boundaries.cpp)
    target_link_libraries(test_vm_mcdc_boundaries PRIVATE impulse_graph_static)
    add_test(NAME test_vm_mcdc_boundaries COMMAND test_vm_mcdc_boundaries)

    add_executable(test_snapshot_mcdc_boundaries tests/test_snapshot_mcdc_boundaries.cpp)

    target_link_libraries(test_snapshot_mcdc_boundaries PRIVATE impulse_graph_static)

    add_test(NAME test_snapshot_mcdc_boundaries COMMAND test_snapshot_mcdc_boundaries)


    add_executable(bench_secondary_indexes tests/bench_secondary_indexes.cpp)
    target_link_libraries(bench_secondary_indexes PRIVATE impulse_graph_static)

    add_executable(bench_bfs_lazy_csc tests/bench_bfs_lazy_csc.cpp)
    target_link_libraries(bench_bfs_lazy_csc PRIVATE impulse_graph_static)

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../datasets/northwind/build_northwind_imps.cpp")
        add_executable(build_northwind_imps ${CMAKE_CURRENT_SOURCE_DIR}/../../datasets/northwind/build_northwind_imps.cpp)
        target_link_libraries(build_northwind_imps PRIVATE impulse_graph_static)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../datasets/northwind/convert_neo4j_northwind.cpp")
        add_executable(convert_neo4j_northwind ${CMAKE_CURRENT_SOURCE_DIR}/../../datasets/northwind/convert_neo4j_northwind.cpp)
        target_link_libraries(convert_neo4j_northwind PRIVATE impulse_graph_static)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../datasets/northwind/bench_scratch_indexes_lifecycle.cpp")
        add_executable(bench_scratch_indexes_lifecycle ${CMAKE_CURRENT_SOURCE_DIR}/../../datasets/northwind/bench_scratch_indexes_lifecycle.cpp)
        target_link_libraries(bench_scratch_indexes_lifecycle PRIVATE impulse_graph_static)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../datasets/bench_large_attribute_dataset.cpp")
        add_executable(bench_large_attribute_dataset ${CMAKE_CURRENT_SOURCE_DIR}/../../datasets/bench_large_attribute_dataset.cpp)
        target_link_libraries(bench_large_attribute_dataset PRIVATE impulse_graph_static)
    endif()

    add_executable(bench_opcode_dispatch tests/bench_opcode_dispatch.cpp)
    target_link_libraries(bench_opcode_dispatch PRIVATE impulse_graph_static)
    add_test(NAME bench_opcode_dispatch COMMAND bench_opcode_dispatch)

    add_executable(bench_cel_compiler tests/bench_cel_compiler.cpp)
    target_link_libraries(bench_cel_compiler PRIVATE impulse_graph_static)
    add_test(NAME bench_cel_compiler COMMAND bench_cel_compiler)

    add_executable(test_cpp_compiler_parity tests/test_cpp_compiler_parity.cpp)
    target_link_libraries(test_cpp_compiler_parity PRIVATE impulse_graph_static)
    add_test(NAME test_cpp_compiler_parity COMMAND test_cpp_compiler_parity)

    add_executable(test_variable_node_id_widths tests/test_variable_node_id_widths.cpp)
    target_link_libraries(test_variable_node_id_widths PRIVATE impulse_graph_static)
    add_test(NAME test_variable_node_id_widths COMMAND test_variable_node_id_widths)

    add_executable(bench_cpp_compiler_ablation tests/bench_cpp_compiler_ablation.cpp)
    target_link_libraries(bench_cpp_compiler_ablation PRIVATE impulse_graph_static)
    add_test(NAME bench_cpp_compiler_ablation COMMAND bench_cpp_compiler_ablation)

    add_executable(bench_prefetch_distance tests/bench_prefetch_distance.cpp)
    target_link_libraries(bench_prefetch_distance PRIVATE impulse_graph_static)
    add_test(NAME bench_prefetch_distance COMMAND bench_prefetch_distance)

    add_executable(bench_all_drugs_medical tests/bench_all_drugs_medical.cpp)
    target_link_libraries(bench_all_drugs_medical PRIVATE impulse_graph_static)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(bench_all_drugs_medical PRIVATE OpenMP::OpenMP_CXX)
    endif()
    add_test(NAME bench_all_drugs_medical COMMAND bench_all_drugs_medical)

    add_executable(test_cypher_compiler tests/test_cypher_compiler.cpp)
    target_link_libraries(test_cypher_compiler PRIVATE impulse_graph_static)
    add_test(NAME test_cypher_compiler COMMAND test_cypher_compiler)

    add_executable(bench_cypher_medical tests/bench_cypher_medical.cpp)
    target_link_libraries(bench_cypher_medical PRIVATE impulse_graph_static)
    add_test(NAME bench_cypher_medical COMMAND bench_cypher_medical)

    add_executable(bench_cypher_all_diseases tests/bench_cypher_all_diseases.cpp)
    target_link_libraries(bench_cypher_all_diseases PRIVATE impulse_graph_static)
    add_test(NAME bench_cypher_all_diseases COMMAND bench_cypher_all_diseases)

    add_executable(test_datalog_compiler tests/test_datalog_compiler.cpp)
    target_link_libraries(test_datalog_compiler PRIVATE impulse_graph_static)
    add_test(NAME test_datalog_compiler COMMAND test_datalog_compiler)

    add_executable(test_c_statement_api tests/test_c_statement_api.cpp)
    target_link_libraries(test_c_statement_api PRIVATE impulse_graph_static)
    add_test(NAME test_c_statement_api COMMAND test_c_statement_api)

    add_executable(test_impscm_mcdc_boundaries tests/test_impscm_mcdc_boundaries.cpp)
    target_link_libraries(test_impscm_mcdc_boundaries PRIVATE impulse_graph_static)
    add_test(NAME test_impscm_mcdc_boundaries COMMAND test_impscm_mcdc_boundaries)

    add_executable(test_multilane_vm tests/test_multilane_vm.cpp)
    target_link_libraries(test_multilane_vm PRIVATE impulse_graph_static)
    add_test(NAME test_multilane_vm COMMAND test_multilane_vm)

    add_executable(test_opcode_contracts tests/test_opcode_contracts.cpp)
    target_link_libraries(test_opcode_contracts PRIVATE impulse_graph_static)
    add_test(NAME test_opcode_contracts COMMAND test_opcode_contracts)

    add_executable(test_security_hardening tests/test_security_hardening.cpp)
    target_link_libraries(test_security_hardening PRIVATE impulse_graph_static)
    add_test(NAME test_security_hardening COMMAND test_security_hardening)

    add_executable(test_imps_malicious_hardening tests/test_imps_malicious_hardening.cpp)
    target_link_libraries(test_imps_malicious_hardening PRIVATE impulse_graph_static)
    add_test(NAME test_imps_malicious_hardening COMMAND test_imps_malicious_hardening)

    # --- Mutation, Pairwise & Property-Based Verification Targets ---
    add_executable(test_mutation_cpp tests/test_mutation_cpp.cpp)
    target_link_libraries(test_mutation_cpp PRIVATE impulse_graph_static)
    add_test(NAME test_mutation_cpp COMMAND test_mutation_cpp)

    add_executable(test_pairwise_cpp tests/test_pairwise_cpp.cpp)
    target_link_libraries(test_pairwise_cpp PRIVATE impulse_graph_static)
    add_test(NAME test_pairwise_cpp COMMAND test_pairwise_cpp)

    add_executable(test_property_snapshot tests/test_property_snapshot.cpp)
    target_link_libraries(test_property_snapshot PRIVATE impulse_graph_static)
    add_test(NAME test_property_snapshot COMMAND test_property_snapshot)

    add_executable(test_property_vm tests/test_property_vm.cpp)
    target_link_libraries(test_property_vm PRIVATE impulse_graph_static)
    add_test(NAME test_property_vm COMMAND test_property_vm)

    add_executable(test_issues_49_50_51_52 tests/test_issues_49_50_51_52.cpp)
    target_link_libraries(test_issues_49_50_51_52 PRIVATE impulse_graph_static)
    add_test(NAME test_issues_49_50_51_52 COMMAND test_issues_49_50_51_52)

    add_executable(kleisli_json_runner tests/kleisli_json_runner.cpp)
    target_link_libraries(kleisli_json_runner PRIVATE impulse_graph_static)

    add_executable(test_multilane_frontend_golden tests/test_multilane_frontend_golden.cpp)
    target_link_libraries(test_multilane_frontend_golden PRIVATE impulse_graph_static)
    add_test(NAME test_multilane_frontend_golden COMMAND test_multilane_frontend_golden)
endif()

# --- Fuzz Targets --------------------------------------------------------
# IMPULSE_BUILD_FUZZ      : build fuzz targets with the standalone random-mutation driver
#                           (tests/fuzz_driver.hpp) and register short ctest smoke runs.
# IMPULSE_ENABLE_LIBFUZZER: build the same targets with LLVM libFuzzer (-fsanitize=fuzzer,
#                           requires clang with the libFuzzer runtime; Apple clang does not ship it,
#                           use Homebrew llvm). No ctest smoke runs are registered in this mode.
# Seed corpora: tests/fuzz_corpus/<target>/ (text only). Binary seeds (.imps, bytecode) are
# generated at runtime by the harness: AGENTS.md 3.6 forbids binary blobs in Git.
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

# Regression tests for defects found by the sanitizer/fuzz campaign.
if(IMPULSE_BUILD_TESTS)
    add_executable(test_robustness_regressions tests/test_robustness_regressions.cpp)
    target_link_libraries(test_robustness_regressions PRIVATE impulse_graph_static)
    add_test(NAME test_robustness_regressions COMMAND test_robustness_regressions)
endif()

# --- Examples ------------------------------------------------------------
option(IMPULSE_BUILD_EXAMPLES "Build impulse-cpp examples" ON)
if(IMPULSE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
