#!/bin/bash
# Compila los binarios del framework desde el codigo fuente y los deja
# donde ejecutor.py/executer.py los esperan (framework/mineplan y
# framework/MineFlow/mineflow). Pensado para correr en el servidor Linux
# nuevo (los binarios que venian en el repo son Mach-O de Mac, no corren
# ahi) pero funciona igual en Mac.
#
# Requisitos: g++ con C++17 y pthread, cmake, make.
#
# Uso:
#   ./build_binaries.sh
set -euo pipefail

PROYECTO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FRAMEWORK_DIR="$PROYECTO_DIR/framework"

echo "=== Compilando mineplan (AmtanaOptimizer) ==="
cd "$FRAMEWORK_DIR/AmtanaOptimizer"
g++ -std=c++17 -O3 -pthread \
    DataStructures.cpp LoadAndSaveData.cpp ProcessingOptimizer.cpp Optimization.cpp main.cpp \
    -o mineplan
cp mineplan "$FRAMEWORK_DIR/mineplan"
chmod +x "$FRAMEWORK_DIR/mineplan"
echo "OK -> $FRAMEWORK_DIR/mineplan"

echo "=== Compilando mineflow (MineFlow) ==="
cd "$FRAMEWORK_DIR/MineFlow"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mineflow -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
cp build/bin/mineflow "$FRAMEWORK_DIR/MineFlow/mineflow"
chmod +x "$FRAMEWORK_DIR/MineFlow/mineflow"
echo "OK -> $FRAMEWORK_DIR/MineFlow/mineflow"

echo "=== Listo ==="
