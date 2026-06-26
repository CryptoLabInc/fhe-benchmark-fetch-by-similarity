#!/usr/bin/env bash
# ------------------------------------------------------------
# build_task.sh - Build the CryptoLab's submission
#
# Builds the task executables under submission/export against the prebuilt juvia
# engine vendored at the submission root: headers under submission/include/,
# libjuvia.so under submission/lib/, and the CMake package config under
# submission/lib/cmake/juvia/.
#
# Prerequisites:
#   - CUDA toolkit installed
#   - OpenMP support
#   - CMake 3.19+
# ------------------------------------------------------------
set -euo pipefail
ROOT="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )/.." &> /dev/null && pwd )"
ENGINE_DIR="$ROOT/submission"
BUILD_DIR="$ENGINE_DIR/build"

if [ ! -f "$ENGINE_DIR/lib/libjuvia.so" ] || [ ! -d "$ENGINE_DIR/include/juvia" ] || [ ! -d "$ENGINE_DIR/export" ]; then
    echo "Error: vendored juvia engine not found under $ENGINE_DIR" >&2
    echo "  expected: lib/libjuvia.so, include/juvia/, export/" >&2
    exit 1
fi

echo "[build_task] Configuring..."
cmake -S "$ENGINE_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release

echo "[build_task] Building..."
cmake --build "$BUILD_DIR" -j "$(nproc)"

echo "[build_task] Done. Executables in $BUILD_DIR/export/"
