#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
echo "Formatting Rust code in ${REPO_ROOT}..."

cd "${REPO_ROOT}"
cargo fmt --all
echo "Rust formatting complete."
