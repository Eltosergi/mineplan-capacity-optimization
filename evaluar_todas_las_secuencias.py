#!/usr/bin/env python3
"""Evalua con mineplan TODAS las secuencias validas del caso de prueba
(por defecto: Ejemplo.csv, T=NumberOfPeriods de configuration.txt, columna
'Capacidad Mina', h_base='ID3' -> 1287 secuencias) y guarda cada NPV en un
CSV, fila por fila, a medida que se calculan.

ADVERTENCIA: cada evaluacion real de mineplan puede tardar hasta 20
minutos (ver contexto del problema). Evaluar las 1287 secuencias del caso
por defecto puede tomar MUCHAS HORAS. Este script esta pensado para
dejarse corriendo en background y poder RETOMARSE si se interrumpe: antes
de evaluar una secuencia, revisa si ya esta en el CSV de salida (columna
'secuencia') y, si es asi, la salta sin gastar una corrida real (regla 4).

NO se corrio en esta sesion (a pedido del usuario: solo escribir codigo).
Antes de dejarlo corriendo las horas completas, pruebalo con --limit 2 o 3
para confirmar que escribe el CSV correctamente.

Uso:
    python3 evaluar_todas_las_secuencias.py
    python3 evaluar_todas_las_secuencias.py --limit 3          # prueba rapida
    python3 evaluar_todas_las_secuencias.py                    # reanuda donde quedo
"""
import argparse
import csv
import sys
from pathlib import Path

import capacity_problem as cp


def _leer_evaluadas(output_path: Path) -> "dict[tuple[str, ...], str]":
    """Lee el CSV de salida si ya existe, devolviendo {secuencia: npv} de
    lo que ya se evaluo (para poder retomar sin repetir corridas reales,
    regla 4)."""
    if not output_path.exists():
        return {}
    evaluadas: "dict[tuple[str, ...], str]" = {}
    with output_path.open(newline="") as f:
        reader = csv.DictReader(f)
        for fila in reader:
            secuencia = tuple(fila["secuencia"].split(";"))
            evaluadas[secuencia] = fila["npv"]
    return evaluadas


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Evalua con mineplan todas las secuencias validas del caso de prueba, con capacidad de reanudar."
    )
    parser.add_argument("--output", type=str, default="resultados_todas_las_secuencias.csv", help="CSV de salida (default: resultados_todas_las_secuencias.csv).")
    parser.add_argument("--scenarios", type=int, default=1, help="NumberOfScenarios a usar (default: 1).")
    parser.add_argument("--h-column", type=str, default=cp.DEFAULT_H_COLUMN, help=f"Columna usada como h (default: {cp.DEFAULT_H_COLUMN!r}).")
    parser.add_argument("--base-id", type=str, default=cp.BASELINE_ID, help=f"ID base (default: {cp.BASELINE_ID!r}).")
    parser.add_argument(
        "--limit", type=int, default=None,
        help="Si se da, evalua como maximo esta cantidad de secuencias NUEVAS en esta corrida (para probar el script sin gastar las horas completas).",
    )
    args = parser.parse_args()

    output_path = cp.PROYECTO_DIR / args.output
    tabla_raw = cp.load_raw_table()

    ya_evaluadas = _leer_evaluadas(output_path)
    print(f"Secuencias ya evaluadas en {output_path}: {len(ya_evaluadas)}")

    existe = output_path.exists()
    csv_file = output_path.open("a", newline="")
    writer = csv.writer(csv_file)
    if not existe:
        writer.writerow(["secuencia", "npv", "computation_time"])
        csv_file.flush()

    nuevas_evaluadas = 0

    try:
        with cp.CapacitySearchProblem(number_of_scenarios=args.scenarios) as problem:
            T = problem.number_of_periods
            elegibles, _valores, _h_base_valor = cp.eligible_pool(tabla_raw, args.h_column, args.base_id)
            valid_sequences = cp.secuencias_validas(tabla_raw, T, args.h_column, args.base_id)
            print(f"IDs elegibles: {elegibles}")
            print(f"Periodos: {T}")
            print(f"Secuencias validas totales: {len(valid_sequences)}")

            mineplan_fn = cp.mineplan_objective(problem)

            for i, secuencia in enumerate(valid_sequences, start=1):
                if secuencia in ya_evaluadas:
                    continue
                if args.limit is not None and nuevas_evaluadas >= args.limit:
                    print(f"Limite de {args.limit} secuencias nuevas alcanzado, deteniendo.")
                    break

                print(f"[{i}/{len(valid_sequences)}] Probando {secuencia} ...", flush=True)
                npv, computation_time = mineplan_fn(secuencia)
                writer.writerow([";".join(secuencia), npv, computation_time])
                csv_file.flush()
                nuevas_evaluadas += 1
                print(f"    NPV={npv} ComputationTime={computation_time}s", flush=True)
    finally:
        csv_file.close()

    print(f"\nSecuencias nuevas evaluadas en esta corrida: {nuevas_evaluadas}")
    print(f"Resultados acumulados en {output_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
