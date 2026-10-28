#!/usr/bin/env python3
"""Corre main (via executer) variando NumberOfScenarios=min..max y guarda
cada resultado en un .csv a medida que llega (fila por fila, para no
perder lo ya corrido si se interrumpe a medio camino).

Uso:
    python3 sweep_scenarios.py --min 1 --max 50
"""
import argparse
import csv
import re
import sys
from pathlib import Path

import executer

PROYECTO_DIR = Path(__file__).resolve().parent
CONFIG_PATH = PROYECTO_DIR / "configuration.txt"
CSV_PATH = PROYECTO_DIR / "resultados_escenarios.csv"

NUMBER_OF_SCENARIOS_RE = re.compile(r"^NumberOfScenarios=.*$", re.MULTILINE)


def set_number_of_scenarios(n: int) -> None:
    texto = CONFIG_PATH.read_text()
    nuevo_texto, reemplazos = NUMBER_OF_SCENARIOS_RE.subn(f"NumberOfScenarios={n}", texto)
    if reemplazos != 1:
        raise RuntimeError("No se encontro (una sola vez) NumberOfScenarios= en configuration.txt")
    CONFIG_PATH.write_text(nuevo_texto)


def main() -> int:
    parser = argparse.ArgumentParser(description="Barre NumberOfScenarios de min a max, una corrida real por valor.")
    parser.add_argument("--min", type=int, default=1, help="Primer valor de NumberOfScenarios (default: 1).")
    parser.add_argument("--max", type=int, default=50, help="Ultimo valor de NumberOfScenarios, inclusive (default: 50).")
    args = parser.parse_args()

    original_texto = CONFIG_PATH.read_text()

    with CSV_PATH.open("w", newline="") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(["NumberOfScenarios", "NPV", "ComputationTime"])

        try:
            for n in range(args.min, args.max + 1):
                set_number_of_scenarios(n)
                print(f"=== NumberOfScenarios={n} ===", flush=True)
                try:
                    npv, computation_time = executer.ejecutar()
                except (FileNotFoundError, RuntimeError) as error:
                    print(f"Error en NumberOfScenarios={n}: {error}", file=sys.stderr)
                    writer.writerow([n, "ERROR", str(error)])
                    csv_file.flush()
                    continue

                writer.writerow([n, npv, computation_time])
                csv_file.flush()
                print(f"NPV={npv} ComputationTime={computation_time}s", flush=True)
        finally:
            # Restaura configuration.txt a su estado original sin importar
            # si el barrido termino completo o se interrumpio a medio camino.
            CONFIG_PATH.write_text(original_texto)

    print(f"Resultados guardados en {CSV_PATH}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
