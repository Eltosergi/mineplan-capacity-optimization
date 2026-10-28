#!/usr/bin/env python3
"""Optimizador 3/4: SMAC (Sequential Model-Based Algorithm Configuration).

Hutter, Hoos & Leyton-Brown (2011), "Sequential Model-Based Optimization
for General Algorithm Configuration". Usamos SMAC3 (la implementacion de
referencia de los mismos autores) con la interfaz ask/tell: en cada trial
SMAC propone un ID de capacidad por periodo (variables categoricas, una
por periodo). A diferencia de TPE, aqui no se descartan propuestas: se
corrige la propuesta para que sea no-decreciente (repair_monotonic) y
*esa* secuencia corregida es la que se evalua de verdad. Por eso cada
trial de SMAC es siempre una corrida real de mineplan (--trials = numero
de corridas, 1 a 1).

Uso:
    python3 optimizer_smac.py --trials 10 --seed 0
"""
import argparse
import sys

from ConfigSpace import Categorical, ConfigurationSpace
from smac import HyperparameterOptimizationFacade, Scenario
from smac.runhistory.dataclasses import TrialValue

import capacity_problem as cp


def main() -> int:
    parser = argparse.ArgumentParser(description="SMAC sobre la secuencia de capacidad.")
    parser.add_argument("--trials", type=int, default=10, help="Numero de corridas reales de mineplan (default: 10).")
    parser.add_argument("--seed", type=int, default=0, help="Semilla para reproducibilidad (default: 0).")
    parser.add_argument("--scenarios", type=int, default=1, help="NumberOfScenarios a usar durante la busqueda (default: 1).")
    args = parser.parse_args()

    csv_path = cp.PROYECTO_DIR / "resultados_smac.csv"
    csv_file, writer = cp.open_results_csv(csv_path)

    mejor_npv = None
    mejor_secuencia = None

    try:
        with cp.CapacitySearchProblem(number_of_scenarios=args.scenarios) as problem:
            print(f"Pool de IDs seleccionables: {problem.pool}")
            print(f"Periodos (NumberOfPeriods): {problem.number_of_periods}")

            configspace = ConfigurationSpace(seed=args.seed)
            for i in range(problem.number_of_periods):
                configspace.add(Categorical(f"period_{i}", problem.pool))

            scenario = Scenario(
                configspace,
                output_directory=cp.PROYECTO_DIR / "smac3_output",
                deterministic=True,
                n_trials=args.trials,
                seed=args.seed,
            )
            smac = HyperparameterOptimizationFacade(scenario, target_function=None, overwrite=True)

            for trial_idx in range(1, args.trials + 1):
                info = smac.ask()
                config = info.config
                secuencia_propuesta = [config[f"period_{i}"] for i in range(problem.number_of_periods)]
                secuencia = cp.repair_monotonic(secuencia_propuesta, problem.pool)

                print(f"[{trial_idx}/{args.trials}] Propuesta {secuencia_propuesta} -> evaluando {secuencia} ...", flush=True)

                npv, computation_time = problem.evaluate(secuencia)
                # SMAC minimiza "cost"; nosotros maximizamos NPV, asi que el costo es -NPV.
                smac.tell(info, TrialValue(cost=float(-npv), time=float(computation_time)))
                cp.log_trial(writer, trial_idx, secuencia, problem.tabla, npv, computation_time)
                csv_file.flush()
                print(f"    NPV={npv} ComputationTime={computation_time}s", flush=True)

                if mejor_npv is None or npv > mejor_npv:
                    mejor_npv = npv
                    mejor_secuencia = secuencia
    finally:
        csv_file.close()

    print("\n=== Mejor resultado (SMAC) ===")
    print(f"Secuencia: {mejor_secuencia}")
    print(f"NPV: {mejor_npv}")
    print(f"Resultados guardados en {csv_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
