#!/usr/bin/env bash
# =============================================================================
# run_e2e_csv_pipeline.sh
# Compiles and executes the standalone C++20 Core E2E CSV-to-Query & GraphBLAS
# Test Harness (`test_e2e_csv_pipeline.cpp`) into `/tmp/test_e2e_csv_pipeline_bin`
# and cleans up temporary artifacts on exit.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMPULSE_CPP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT_BIN="/tmp/test_e2e_csv_pipeline_bin"
TMP_IMPS="/tmp/e2e_core_test.imps"

cleanup() {
    rm -f "${OUT_BIN}" "${TMP_IMPS}"
}
trap cleanup EXIT

CXX_BIN="${CXX:-clang++}"

# Locate Google Highway headers and static library
HWY_INC_FLAGS=()
HWY_LINK_FLAGS=()

if [[ -d "/opt/homebrew/include/hwy" && -f "/opt/homebrew/lib/libhwy.a" ]]; then
    HWY_INC_FLAGS+=("-I/opt/homebrew/include")
    HWY_LINK_FLAGS+=("/opt/homebrew/lib/libhwy.a")
elif [[ -d "/usr/local/include/hwy" && -f "/usr/local/lib/libhwy.a" ]]; then
    HWY_INC_FLAGS+=("-I/usr/local/include")
    HWY_LINK_FLAGS+=("/usr/local/lib/libhwy.a")
elif [[ -d "/Users/jesse/impulse/impulse-graph-core/impulse-cpp/build/_deps/highway-src" ]]; then
    HWY_INC_FLAGS+=("-I/Users/jesse/impulse/impulse-graph-core/impulse-cpp/build/_deps/highway-src")
    if [[ -f "/Users/jesse/impulse/impulse-graph-core/impulse-cpp/build_on/_deps/highway-build/libhwy.a" ]]; then
        HWY_LINK_FLAGS+=("/Users/jesse/impulse/impulse-graph-core/impulse-cpp/build_on/_deps/highway-build/libhwy.a")
    elif [[ -f "/Users/jesse/impulse/impulse-graph-core/impulse-cpp/build/_deps/highway-build/libhwy.a" ]]; then
        HWY_LINK_FLAGS+=("/Users/jesse/impulse/impulse-graph-core/impulse-cpp/build/_deps/highway-build/libhwy.a")
    fi
fi

CORE_SOURCES=(
    "${IMPULSE_CPP_DIR}/src/impulse_graph.cpp"
    "${IMPULSE_CPP_DIR}/src/impulse_snapshot_writer.cpp"
    "${IMPULSE_CPP_DIR}/src/impulse_index.cpp"
    "${IMPULSE_CPP_DIR}/src/impulse_simd.cpp"
    "${IMPULSE_CPP_DIR}/src/impulse_vm.cpp"
    "${IMPULSE_CPP_DIR}/src/impulse_vm_context.cpp"
    "${IMPULSE_CPP_DIR}/src/impulse_vm_fluent.cpp"
    "${IMPULSE_CPP_DIR}/src/impulse_vm_fluent_traversal.cpp"
    "${IMPULSE_CPP_DIR}/src/impulse_vm_fluent_multilane.cpp"
    "${IMPULSE_CPP_DIR}/src/impulse_statement_c_api.cpp"
    "${IMPULSE_CPP_DIR}/src/impulse_bytecode_emitter.cpp"
)

echo "[Build] Compiling test_e2e_csv_pipeline.cpp -> ${OUT_BIN} ..."
"${CXX_BIN}" -std=c++20 -O2 \
    -I"${IMPULSE_CPP_DIR}/include" \
    -I"${IMPULSE_CPP_DIR}/src" \
    "${HWY_INC_FLAGS[@]}" \
    "${SCRIPT_DIR}/test_e2e_csv_pipeline.cpp" \
    "${CORE_SOURCES[@]}" \
    "${HWY_LINK_FLAGS[@]}" \
    -pthread \
    -o "${OUT_BIN}"

echo "[Run] Executing ${OUT_BIN} ..."
"${OUT_BIN}"
