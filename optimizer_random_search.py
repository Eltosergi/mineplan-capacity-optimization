#!/usr/bin/env python3
"""Optimizador 1/4: Random Search (baseline).

En cada trial construye una secuencia de IDs de capacidad al azar que ya
es factible por construccion (nunca baja de capacidad): en cada periodo
se queda en el ID actual o salta a uno mas alto, nunca mas bajo. Cada
trial es una corrida real de mineplan (--trials = numero de corridas).

Uso:
    python3 optimizer_random_search.py --trials 10 --seed 0
"""
import argparse
import sys

import capacity_problem as cp


def construir_secuencia_aleatoria(rng, pool: list[str], n_periodos: int) -> list[str]:
    secuencia = []
    idx_actual = 0  # arranca en pool[0] == BASELINE_ID (ID3)
    for _ in range(n_periodos):
        idx_actual = rng.randint(idx_actual, len(pool) - 1)
        secuencia.append(pool[idx_actual])
    return secuencia


def main() -> int:
    parser = argparse.ArgumentParser(description="Random Search sobre la secuencia de capacidad.")
    parser.add_argument("--trials", type=int, default=10, help="Numero de corridas reales de mineplan (default: 10).")
    parser.add_argument("--seed", type=int, default=0, help="Semilla para reproducibilidad (default: 0).")
    parser.add_argument("--scenarios", type=int, default=1, help="NumberOfScenarios a usar durante la busqueda (default: 1).")
    args = parser.parse_args()

    import random
    rng = random.Random(args.seed)

    csv_path = cp.PROYECTO_DIR / "resultados_random_search.csv"
    csv_file, writer = cp.open_results_csv(csv_path)

    mejor_npv = None
    mejor_secuencia = None

    try:
        with cp.CapacitySearchProblem(number_of_scenarios=args.scenarios) as problem:
            print(f"Pool de IDs seleccionables: {problem.pool}")
            print(f"Periodos (NumberOfPeriods): {problem.number_of_periods}")

            for trial in range(1, args.trials + 1):
                secuencia = construir_secuencia_aleatoria(rng, problem.pool, problem.number_of_periods)
                print(f"[{trial}/{args.trials}] Probando {secuencia} ...", flush=True)

                npv, computation_time = problem.evaluate(secuencia)
                cp.log_trial(writer, trial, secuencia, problem.tabla, problem.number_of_scenarios, npv, computation_time)
                csv_file.flush()
                print(f"    NPV={npv} ComputationTime={computation_time}s", flush=True)

                if mejor_npv is None or npv > mejor_npv:
                    mejor_npv = npv
                    mejor_secuencia = secuencia
    finally:
        csv_file.close()

    print("\n=== Mejor resultado (Random Search) ===")
    print(f"Secuencia: {mejor_secuencia}")
    print(f"NPV: {mejor_npv}")
    print(f"Resultados guardados en {csv_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
