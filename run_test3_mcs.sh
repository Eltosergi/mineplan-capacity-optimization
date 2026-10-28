#!/bin/bash
# Prueba 3: MCS (Multilevel Coordinate Search, adaptado a discreto) sobre
# la secuencia de capacidad.
# Uso:
#   ./run_test3_mcs.sh [TRIALS]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

TRIALS="${1:-50}"
python3 optimizer_mcs.py --trials "$TRIALS"
