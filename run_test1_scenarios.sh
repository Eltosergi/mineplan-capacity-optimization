#!/bin/bash
# Prueba 1: corre el sistema variando NumberOfScenarios de 1 a N (default 50),
# una corrida real por valor. Resultados en resultados_escenarios.csv.
#
# Uso:
#   ./run_test1_scenarios.sh [N]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

N="${1:-50}"
python3 sweep_scenarios.py --min 1 --max "$N"
