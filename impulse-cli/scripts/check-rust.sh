#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
echo "Running Rust format check in ${REPO_ROOT}..."
cd "${REPO_ROOT}"
cargo fmt --all -- --check

echo "Running Clippy with -D warnings in ${REPO_ROOT}..."
cargo clippy --all-targets -- -D warnings

echo "Rust formatting and linting checks passed successfully."
