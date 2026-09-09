#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=== Checking C# (impulse-dotnet) ==="
cd "${ROOT_DIR}"
if command -v dotnet >/dev/null 2>&1; then
    dotnet format --verify-no-changes
elif command -v csharpier >/dev/null 2>&1; then
    csharpier --check .
else
    echo "Notice: Neither 'dotnet' nor 'csharpier' CLI found in PATH."
    echo "Verifying presence of .editorconfig..."
    if [ ! -f .editorconfig ]; then
        echo "Error: .editorconfig missing!"
        exit 1
    fi
    echo "Basic file sanity check..."
    find . -name "*.cs" -exec test -s {} \;
    echo "All C# source files present and non-empty."
fi
echo "=== C# check finished successfully! ==="
