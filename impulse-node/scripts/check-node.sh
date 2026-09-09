#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
echo "Checking Node.js formatting in ${REPO_ROOT}..."
cd "${REPO_ROOT}"
npx prettier --check "index.js" "index.d.ts" "test.js" "package.json" "examples/**/*.js"

echo "Linting Node.js code in ${REPO_ROOT}..."
npx eslint "index.js" "test.js" "examples/**/*.js"

echo "Node.js formatting and linting checks passed successfully."
