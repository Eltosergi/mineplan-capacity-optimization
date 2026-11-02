#!/bin/bash
# Prueba 6: BO-GP (Optimizacion Bayesiana con Proceso Gaussiano y
# transformacion categorica) sobre la secuencia de capacidad.
# Uso:
#   ./run_test6_bo_gp.sh [TRIALS] [SEED] [N0]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

TRIALS="${1:-20}"
SEED="${2:-0}"
N0="${3:-5}"
python3 optimizer_bo_gp.py --trials "$TRIALS" --seed "$SEED" --n0 "$N0"
