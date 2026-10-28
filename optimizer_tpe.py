#!/usr/bin/env python3
"""Optimizador 2/4: TPE / Bayesian Optimization via Optuna.

Bergstra, Bardenet, Bengio & Kegl (2011), "Algorithms for Hyper-Parameter
Optimization". Usamos el sampler TPE de Optuna (la implementacion de
referencia de los mismos autores) con la interfaz ask/tell: en cada trial
se propone un ID de capacidad por periodo; si la secuencia no es
no-decreciente se descarta (TrialPruned) SIN gastar una corrida real de
mineplan -esa es la ventaja de Optuna que senala el Excel-, y no cuenta
para --trials. Solo las secuencias factibles consumen el presupuesto real.

Uso:
    python3 optimizer_tpe.py --trials 10 --seed 0
"""
import argparse
import sys

import optuna

import capacity_problem as cp

optuna.logging.set_verbosity(optuna.logging.WARNING)


def main() -> int:
    parser = argparse.ArgumentParser(description="TPE (Optuna) sobre la secuencia de capacidad.")
    parser.add_argument("--trials", type=int, default=10, help="Numero de corridas reales de mineplan (default: 10).")
    parser.add_argument("--seed", type=int, default=0, help="Semilla para reproducibilidad (default: 0).")
    parser.add_argument("--scenarios", type=int, default=1, help="NumberOfScenarios a usar durante la busqueda (default: 1).")
    parser.add_argument("--max-attempts", type=int, default=2000, help="Tope de propuestas (incluye descartadas) para evitar un loop infinito (default: 2000).")
    args = parser.parse_args()

    csv_path = cp.PROYECTO_DIR / "resultados_tpe.csv"
    csv_file, writer = cp.open_results_csv(csv_path)

    study = optuna.create_study(
        direction="maximize",
        sampler=optuna.samplers.TPESampler(seed=args.seed),
    )

    mejor_npv = None
    mejor_secuencia = None
    corridas_reales = 0
    propuestas_descartadas = 0

    try:
        with cp.CapacitySearchProblem(number_of_scenarios=args.scenarios) as problem:
            print(f"Pool de IDs seleccionables: {problem.pool}")
            print(f"Periodos (NumberOfPeriods): {problem.number_of_periods}")

            intento = 0
            while corridas_reales < args.trials:
                intento += 1
                if intento > args.max_attempts:
                    print(
                        f"Advertencia: se alcanzo el tope de {args.max_attempts} propuestas "
                        f"({propuestas_descartadas} descartadas) sin llegar a {args.trials} corridas reales.",
                        file=sys.stderr,
                    )
                    break

                trial = study.ask()
                secuencia = [
                    trial.suggest_categorical(f"period_{i}", problem.pool)
                    for i in range(problem.number_of_periods)
                ]

                if not cp.is_monotonic(secuencia, problem.pool):
                    # Se descarta sin correr mineplan: no cuenta para --trials.
                    study.tell(trial, state=optuna.trial.TrialState.PRUNED)
                    propuestas_descartadas += 1
                    continue

                corridas_reales += 1
                print(f"[{corridas_reales}/{args.trials}] Probando {secuencia} ...", flush=True)

                npv, computation_time = problem.evaluate(secuencia)
                study.tell(trial, float(npv))
                cp.log_trial(writer, corridas_reales, secuencia, problem.tabla, npv, computation_time)
                csv_file.flush()
                print(f"    NPV={npv} ComputationTime={computation_time}s", flush=True)

                if mejor_npv is None or npv > mejor_npv:
                    mejor_npv = npv
                    mejor_secuencia = secuencia
    finally:
        csv_file.close()

    print("\n=== Mejor resultado (TPE / Optuna) ===")
    print(f"Secuencia: {mejor_secuencia}")
    print(f"NPV: {mejor_npv}")
    print(f"Propuestas descartadas por restriccion (sin costo real): {propuestas_descartadas}")
    print(f"Resultados guardados en {csv_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
