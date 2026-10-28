#!/bin/bash
# Prueba 2: Random Search sobre la secuencia de capacidad.
# Uso:
#   ./run_test2_random_search.sh [TRIALS] [SEED]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

TRIALS="${1:-50}"
SEED="${2:-0}"
python3 optimizer_random_search.py --trials "$TRIALS" --seed "$SEED"
