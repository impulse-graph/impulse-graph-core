#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=== Formatting C# (impulse-dotnet) ==="
cd "${ROOT_DIR}"
if command -v dotnet >/dev/null 2>&1; then
    dotnet format
elif command -v csharpier >/dev/null 2>&1; then
    csharpier .
else
    echo "Notice: Neither 'dotnet' nor 'csharpier' CLI found in PATH. Skipping active formatting."
    echo "Configuration (.editorconfig) is present."
fi
echo "=== C# format finished ==="
