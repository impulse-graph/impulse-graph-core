#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=== Formatting Python (ruff format & ruff check --fix) ==="
cd "${ROOT_DIR}"
ruff format .
ruff check --fix .
echo "=== Python formatted successfully ==="
