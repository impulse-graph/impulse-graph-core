#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=== Checking Python formatting & lint (ruff format --check & ruff check) ==="
cd "${ROOT_DIR}"
ruff format --check .
ruff check .
echo "=== Python checks passed! ==="
