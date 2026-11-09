#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "=========================================="
echo " Checking all impulse-graph-core components"
echo "=========================================="

if [ -f "${ROOT_DIR}/impulse-cli/scripts/check-rust.sh" ]; then
    echo "--- impulse-cli ---"
    "${ROOT_DIR}/impulse-cli/scripts/check-rust.sh"
fi

if [ -f "${ROOT_DIR}/impulse-go/scripts/check-go.sh" ]; then
    echo "--- impulse-go ---"
    "${ROOT_DIR}/impulse-go/scripts/check-go.sh"
fi

if [ -f "${ROOT_DIR}/impulse-node/scripts/check-node.sh" ]; then
    echo "--- impulse-node ---"
    "${ROOT_DIR}/impulse-node/scripts/check-node.sh"
fi

if [ -f "${ROOT_DIR}/impulse-python/scripts/check-python.sh" ]; then
    echo "--- impulse-python ---"
    "${ROOT_DIR}/impulse-python/scripts/check-python.sh"
fi

if [ -f "${ROOT_DIR}/impulse-dotnet/scripts/check-csharp.sh" ]; then
    echo "--- impulse-dotnet ---"
    "${ROOT_DIR}/impulse-dotnet/scripts/check-csharp.sh"
fi

echo "=========================================="
echo " All components check passed cleanly!"
echo "=========================================="
