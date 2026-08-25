#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SPEC_DIR="$(cd "${CPP_DIR}/../../impulse-graph-spec" && pwd)"
BUILD_DIR="${CPP_DIR}/build-mcdc"
PROFILE_DIR="${BUILD_DIR}/profiles"
REPORT_DIR="${BUILD_DIR}/coverage_report"

echo "================================================================"
echo " ImpulseVM 100% MC/DC Coverage Build & Verification Pipeline"
echo "================================================================"

# 1. Clean & Prepare Build Directory
rm -rf "${PROFILE_DIR}" "${REPORT_DIR}"
mkdir -p "${PROFILE_DIR}" "${REPORT_DIR}"

echo "[1/5] Configuring CMake with LLVM MC/DC Coverage..."
cmake -B "${BUILD_DIR}" -S "${CPP_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DIMPULSE_ENABLE_MCDC_COVERAGE=ON \
    -DIMPULSE_BUILD_TESTS=ON

echo "[2/5] Building instrumented binaries..."
cmake --build "${BUILD_DIR}" -- -j"$(sysctl -n hw.ncpu || nproc || echo 4)"

# 2. Execute Tests with LLVM Profile Instrumentation
echo "[3/5] Executing test harnesses with MC/DC profile collection..."
export LLVM_PROFILE_FILE="${PROFILE_DIR}/cov_%p_%m.profraw"

echo " -> Running test suites..."
for t in impulse_graph_test test_vector_suite simd_test vm_test vm_fluent_test test_vm_vector_math test_permutation_index test_cel_parser test_variable_node_id_widths test_cpp_compiler_parity test_cypher_compiler test_datalog_compiler test_c_statement_api test_vm_mcdc_boundaries test_snapshot_mcdc_boundaries test_impscm_mcdc_boundaries; do
    if [[ -x "${BUILD_DIR}/${t}" ]]; then
        echo "    * ${t}"
        "${BUILD_DIR}/${t}" > /dev/null 2>&1 || echo "Warning: ${t} had failures"
    fi
done

# Run the spec test vectors suite against the instrumented shared library (Pass 1: standard, Pass 2: fuel enabled)
if [[ -f "${SPEC_DIR}/tools/run_vm_asm_suite.py" ]]; then
    echo " -> Running spec vm-impas test vectors suite (Pass 1: Standard)..."
    DYLD_LIBRARY_PATH="${BUILD_DIR}:${DYLD_LIBRARY_PATH:-}" \
    LD_LIBRARY_PATH="${BUILD_DIR}:${LD_LIBRARY_PATH:-}" \
    python3 "${SPEC_DIR}/tools/run_vm_asm_suite.py" || true

    echo " -> Running spec vm-impas test vectors suite (Pass 2: Fuel Enabled)..."
    DYLD_LIBRARY_PATH="${BUILD_DIR}:${DYLD_LIBRARY_PATH:-}" \
    LD_LIBRARY_PATH="${BUILD_DIR}:${LD_LIBRARY_PATH:-}" \
    IMPULSE_TEST_ENABLE_FUEL=1 \
    python3 "${SPEC_DIR}/tools/run_vm_asm_suite.py" || true
fi

# 3. Merge Profile Data
echo "[4/5] Merging raw profile counters..."
xcrun llvm-profdata merge -sparse "${PROFILE_DIR}"/*.profraw -o "${BUILD_DIR}/impulse_vm.profdata"

# 4. Generate Coverage Reports
echo "[5/5] Generating MC/DC Coverage Summary..."
echo "================================================================"

xcrun llvm-cov report \
    "${BUILD_DIR}/libimpulse_graph_static.a" \
    -object="${BUILD_DIR}/test_impscm_mcdc_boundaries" \
    -instr-profile="${BUILD_DIR}/impulse_vm.profdata" \
    "${CPP_DIR}/src/impulse_vm.cpp" \
    "${CPP_DIR}/src/impulse_graph.cpp" \
    "${CPP_DIR}/src/impulse_simd.cpp" \
    "${CPP_DIR}/include/impulse_compiler.hpp" \
    "${CPP_DIR}/include/impulse_sexpr.hpp" \
    "${CPP_DIR}/include/impulse_math_ops.h" \
    --show-mcdc-summary

echo "================================================================"
echo "Generating Detailed MC/DC Analysis to ${REPORT_DIR}/mcdc_report.txt..."

xcrun llvm-cov show \
    "${BUILD_DIR}/libimpulse_graph_static.a" \
    -object="${BUILD_DIR}/test_impscm_mcdc_boundaries" \
    -instr-profile="${BUILD_DIR}/impulse_vm.profdata" \
    "${CPP_DIR}/src/impulse_vm.cpp" \
    "${CPP_DIR}/src/impulse_graph.cpp" \
    "${CPP_DIR}/src/impulse_simd.cpp" \
    "${CPP_DIR}/include/impulse_compiler.hpp" \
    "${CPP_DIR}/include/impulse_sexpr.hpp" \
    "${CPP_DIR}/include/impulse_math_ops.h" \
    --show-mcdc \
    --show-mcdc-summary \
    --show-branches=count > "${REPORT_DIR}/mcdc_report.txt"

xcrun llvm-cov show \
    "${BUILD_DIR}/libimpulse_graph_static.a" \
    -object="${BUILD_DIR}/test_impscm_mcdc_boundaries" \
    -instr-profile="${BUILD_DIR}/impulse_vm.profdata" \
    "${CPP_DIR}/src/impulse_vm.cpp" \
    "${CPP_DIR}/src/impulse_graph.cpp" \
    "${CPP_DIR}/src/impulse_simd.cpp" \
    "${CPP_DIR}/include/impulse_compiler.hpp" \
    "${CPP_DIR}/include/impulse_sexpr.hpp" \
    "${CPP_DIR}/include/impulse_math_ops.h" \
    --show-mcdc \
    --show-branches=count \
    --format=html \
    --output-dir="${REPORT_DIR}/html"

echo "MC/DC Report saved to: ${REPORT_DIR}/mcdc_report.txt"
echo "HTML Report saved to: ${REPORT_DIR}/html/index.html"
