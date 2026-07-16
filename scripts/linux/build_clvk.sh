#!/usr/bin/env bash
set -e

echo "==================================================="
echo "  OmniGPU: Automated clvk Build Script (Linux)"
echo "==================================================="
echo ""
echo "WARNING: Building clvk requires compiling LLVM and Clang from source."
echo "This process can take 2 to 4 hours and requires high RAM / disk space."
echo ""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLVK_DIR="$SCRIPT_DIR/../../third_party/clvk"
OUTPUT_DIR="$CLVK_DIR"

if [ ! -f "$CLVK_DIR/CMakeLists.txt" ]; then
    echo "ERROR: clvk submodule is not initialized. Run:"
    echo "  git submodule update --init --recursive"
    exit 1
fi
echo "[1/4] clvk submodule verified."

echo "[2/4] Fetching clspv dependencies (LLVM, Clang, SPIR-V)..."
cd "$CLVK_DIR/external/clspv"
python3 utils/fetch_sources.py

echo "[3/4] Configuring CMake for clvk..."
mkdir -p "$CLVK_DIR/build"
cd "$CLVK_DIR/build"
cmake .. -DCMAKE_BUILD_TYPE=Release

echo "[4/4] Compiling clvk (Release mode)..."
cmake --build . --config Release --target OpenCL

echo ""
echo "==================================================="
echo "  Build Successful!"
echo "==================================================="
echo "Copying binaries to third_party/clvk..."
mkdir -p "$OUTPUT_DIR"
cp -f libOpenCL.so* "$OUTPUT_DIR/"
cp -f libclspv.so* "$OUTPUT_DIR/"
cp -f libclvk.so* "$OUTPUT_DIR/"

echo ""
echo "Done! Please configure CMake with -DOMNIGPU_FETCH_CLVK=ON to deploy clvk."
