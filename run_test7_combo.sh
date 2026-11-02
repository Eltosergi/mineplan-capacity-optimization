#!/bin/bash
# Prueba 7: COMBO (Optimizacion Bayesiana combinatoria, Oh et al. 2019)
# sobre la secuencia de capacidad.
# Uso:
#   ./run_test7_combo.sh [TRIALS] [SEED] [N0] [ACQ_MODE]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

TRIALS="${1:-20}"
SEED="${2:-0}"
N0="${3:-5}"
ACQ_MODE="${4:-local}"
python3 optimizer_combo.py --trials "$TRIALS" --seed "$SEED" --n0 "$N0" --acq-mode "$ACQ_MODE"
