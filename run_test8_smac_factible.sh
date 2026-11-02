#!/bin/bash
# Prueba 8: SMAC que solo propone secuencias validas (sin reparar ni
# descartar), via forbidden clauses de ConfigSpace.
# Uso:
#   ./run_test8_smac_factible.sh [TRIALS] [SEED]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

TRIALS="${1:-20}"
SEED="${2:-0}"
python3 optimizer_smac_factible.py --trials "$TRIALS" --seed "$SEED"
