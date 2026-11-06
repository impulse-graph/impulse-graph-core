#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "=========================================="
echo " Formatting all impulse-graph-core components"
echo "=========================================="

if [ -f "${ROOT_DIR}/impulse-cli/scripts/fmt-rust.sh" ]; then
    echo "--- impulse-cli ---"
    "${ROOT_DIR}/impulse-cli/scripts/fmt-rust.sh"
fi

if [ -f "${ROOT_DIR}/impulse-go/scripts/fmt-go.sh" ]; then
    echo "--- impulse-go ---"
    "${ROOT_DIR}/impulse-go/scripts/fmt-go.sh"
fi

if [ -f "${ROOT_DIR}/impulse-node/scripts/fmt-node.sh" ]; then
    echo "--- impulse-node ---"
    "${ROOT_DIR}/impulse-node/scripts/fmt-node.sh"
fi

if [ -f "${ROOT_DIR}/impulse-python/scripts/fmt-python.sh" ]; then
    echo "--- impulse-python ---"
    "${ROOT_DIR}/impulse-python/scripts/fmt-python.sh"
fi

if [ -f "${ROOT_DIR}/impulse-dotnet/scripts/fmt-csharp.sh" ]; then
    echo "--- impulse-dotnet ---"
    "${ROOT_DIR}/impulse-dotnet/scripts/fmt-csharp.sh"
fi

echo "=========================================="
echo " All components formatted successfully!"
echo "=========================================="
