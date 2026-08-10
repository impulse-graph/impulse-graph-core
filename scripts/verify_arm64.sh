#!/usr/bin/env bash
set -e

echo "================================================================"
echo " IMPULSE GRAPH CORE — LINUX/ARM64 VERIFICATION SUITE"
echo " Architecture: $(uname -m)"
echo " OS: $(uname -s)"
echo " GCC Version: $(g++ --version | head -n 1)"
echo "================================================================"

# 1. C++ Core Kernel
echo ""
echo "=== [1/6] Building & Testing C++ Core Kernel (impulse-cpp) ==="
cd /workspace/impulse-graph-core/impulse-cpp
rm -rf build
cmake -B build -GNinja -DCMAKE_BUILD_TYPE=Release -DHWY_ENABLE_CONTRIB=OFF -DHWY_ENABLE_TESTS=OFF -DHWY_ENABLE_EXAMPLES=OFF
cmake --build build -- -j 4
ctest --test-dir build --output-on-failure

# 2. Rust Core Crate
echo ""
echo "=== [2/6] Building & Testing Rust Core Crate (impulse-rust) ==="
cd /workspace/impulse-graph-core/impulse-rust
cargo test --verbose

# 3. Go Bindings
echo ""
echo "=== [3/6] Building & Testing Go Bindings (impulse-go) ==="
cd /workspace/impulse-graph-core/impulse-go
go test -v ./...

# 4. Python Bindings
echo ""
echo "=== [4/6] Building & Testing Python Bindings (impulse-python) ==="
cd /workspace/impulse-graph-core/impulse-python
python3 setup.py build_ext --inplace
pip install --no-build-isolation --no-deps -e .
pytest -v

# 5. Node.js Bindings
echo ""
echo "=== [5/6] Building & Testing Node.js Bindings (impulse-node) ==="
cd /workspace/impulse-graph-core/impulse-node
export PYTHON=/usr/bin/python3
npm install
npm test

# 6. .NET Bindings
echo ""
echo "=== [6/6] Building .NET Bindings (impulse-dotnet) ==="
cd /workspace/impulse-graph-core/impulse-dotnet
dotnet build

echo ""
echo "================================================================"
echo " SUCCESS: ALL IMPULSE CORE MODULES VERIFIED ON LINUX/ARM64!"
echo "================================================================"
