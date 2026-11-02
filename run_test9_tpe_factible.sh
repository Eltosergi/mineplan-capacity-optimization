#!/bin/bash
# Prueba 9: TPE/Optuna que solo propone secuencias validas (sin reparar ni
# descartar), via espacio de busqueda condicional (define-by-run).
# Uso:
#   ./run_test9_tpe_factible.sh [TRIALS] [SEED]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

TRIALS="${1:-20}"
SEED="${2:-0}"
python3 optimizer_tpe_factible.py --trials "$TRIALS" --seed "$SEED"
