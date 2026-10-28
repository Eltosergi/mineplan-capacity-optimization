#!/bin/bash
# Prueba 4: TPE / Bayesian Optimization (Optuna) sobre la secuencia de
# capacidad.
# Uso:
#   ./run_test4_tpe.sh [TRIALS] [SEED]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

TRIALS="${1:-50}"
SEED="${2:-0}"
python3 optimizer_tpe.py --trials "$TRIALS" --seed "$SEED"
