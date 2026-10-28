#!/bin/bash
# Prueba 5: SMAC (Sequential Model-Based Algorithm Configuration) sobre la
# secuencia de capacidad.
# Uso:
#   ./run_test5_smac.sh [TRIALS] [SEED]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

TRIALS="${1:-50}"
SEED="${2:-0}"
python3 optimizer_smac.py --trials "$TRIALS" --seed "$SEED"
