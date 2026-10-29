#!/bin/bash
# Prueba: para cada uno de los primeros N IDs de Ejemplo.csv (plan estatico,
# mismo ID en los 5 periodos, inversion completa solo en el periodo 0),
# barre NumberOfScenarios de 1 a MAX. N es un parametro: hoy 5, despues 11.
#
# Uso:
#   ./run_test_ids_scenarios.sh [NUM_IDS] [MAX_ESCENARIOS]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

NUM_IDS="${1:-5}"
MAX_ESCENARIOS="${2:-50}"
python3 sweep_ids_scenarios.py --num-ids "$NUM_IDS" --min-scenarios 1 --max-scenarios "$MAX_ESCENARIOS"
