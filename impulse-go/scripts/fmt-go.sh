#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH="/opt/homebrew/bin:/usr/local/bin:${HOME}/go/bin:${PATH}"

echo "Formatting Go code in ${REPO_ROOT}..."
cd "${REPO_ROOT}"
gofmt -w .
echo "Go formatting complete."
