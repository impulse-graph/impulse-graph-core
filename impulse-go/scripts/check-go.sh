#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH="/opt/homebrew/bin:/usr/local/bin:${HOME}/go/bin:${PATH}"

echo "Running gofmt check in ${REPO_ROOT}..."
cd "${REPO_ROOT}"
UNFORMATTED=$(gofmt -l .)
if [ -n "${UNFORMATTED}" ]; then
    echo "ERROR: The following Go files are not formatted:"
    echo "${UNFORMATTED}"
    exit 1
fi

echo "Running go vet ./... in ${REPO_ROOT}..."
go vet ./...

if command -v golangci-lint >/dev/null 2>&1; then
    echo "Running golangci-lint in ${REPO_ROOT}..."
    golangci-lint run ./...
fi

echo "Go formatting and linting checks passed successfully."
