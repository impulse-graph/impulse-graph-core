#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
echo "Formatting Node.js code in ${REPO_ROOT}..."
cd "${REPO_ROOT}"
npx prettier --write "index.js" "index.d.ts" "test.js" "package.json" "examples/**/*.js"
echo "Node.js formatting complete."
