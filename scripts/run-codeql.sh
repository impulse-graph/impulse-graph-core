#!/usr/bin/env bash
# ==============================================================================
# scripts/run-codeql.sh — Local CodeQL Static Analysis for Impulse Graph Core
# ==============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CPP_DIR="${ROOT_DIR}/impulse-cpp"
BUILD_DIR="${CPP_DIR}/build-codeql"
DB_DIR="${ROOT_DIR}/.codeql_db"
SARIF_OUT="${ROOT_DIR}/codeql-results.sarif"

echo "========================================================="
echo " Impulse Graph Engine — Local CodeQL Analysis"
echo "========================================================="

# 1. Verify CodeQL CLI is available
if ! command -v codeql &>/dev/null; then
    echo "[-] CodeQL CLI not found on PATH."
    echo ""
    echo "To install CodeQL locally on macOS:"
    echo "  1. brew install --cask codeql"
    echo "  2. codeql pack download codeql/cpp-queries"
    echo ""
    echo "For Linux / Windows or manual setup, see:"
    echo "  https://docs.github.com/en/code-security/codeql-cli"
    exit 1
fi

echo "[1/4] Checking CodeQL packs..."
if ! codeql resolve queries codeql/cpp-queries:codeql-suites/cpp-security-and-quality.qls &>/dev/null; then
    echo " -> Downloading codeql/cpp-queries pack..."
    codeql pack download codeql/cpp-queries
fi

echo "[2/4] Configuring clean CMake build for database generation..."
rm -rf "${BUILD_DIR}" "${DB_DIR}" "${SARIF_OUT}"
mkdir -p "${BUILD_DIR}"

cmake -B "${BUILD_DIR}" -S "${CPP_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DIMPULSE_ENABLE_ASSERTIONS=ON

echo "[3/4] Creating CodeQL database for C++20 kernel..."
codeql database create "${DB_DIR}" \
    --language=cpp \
    --source-root="${CPP_DIR}" \
    --command="cmake --build ${BUILD_DIR} -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

echo "[4/4] Analyzing CodeQL database with cpp-security-and-quality suite..."
codeql database analyze "${DB_DIR}" \
    codeql/cpp-queries:codeql-suites/cpp-security-and-quality.qls \
    --format=sarif-latest \
    --output="${SARIF_OUT}"

# Parse SARIF results
python3 -c "
import json
import sys

with open('${SARIF_OUT}', 'r', encoding='utf-8') as f:
    sarif = json.load(f)

results = [r for run in sarif.get('runs', []) for r in run.get('results', [])]
if results:
    print('\n\033[31m=========================================================\033[0m')
    print(f'\033[31m[-] CodeQL Found {len(results)} Alert(s):\033[0m')
    print('\033[31m=========================================================\033[0m')
    for r in results:
        rule = r.get('ruleId', 'unknown-rule')
        msg = r.get('message', {}).get('text', '')
        locs = r.get('locations', [])
        loc_str = 'unknown location'
        if locs:
            phys = locs[0].get('physicalLocation', {})
            uri = phys.get('artifactLocation', {}).get('uri', '')
            line = phys.get('region', {}).get('startLine', 0)
            loc_str = f'{uri}:{line}'
        print(f' -> {loc_str} [{rule}]')
        print(f'    {msg}')
    sys.exit(1)
else:
    print('\n\033[32m=========================================================\033[0m')
    print('\033[32m[+] CodeQL Clean: 0 alerts found across C++ codebase!\033[0m')
    print('\033[32m=========================================================\033[0m')
"

# Cleanup temporary build and db if clean
rm -rf "${BUILD_DIR}" "${DB_DIR}"
