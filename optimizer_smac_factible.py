#!/usr/bin/env python3
"""Version de SMAC (Fase 4) que SOLO propone secuencias validas (reglas 4 y
5): nada de reparar ni descartar, a diferencia de optimizer_smac.py (que
no se modifica: regla 6).

Investigacion (por que esto SI se puede sin reparar/descartar): ConfigSpace
(la libreria de espacios de busqueda que usa SMAC3) soporta "forbidden
clauses" (ForbiddenEqualsClause + ForbiddenAndConjunction) para excluir
COMBINACIONES de valores del espacio de busqueda. ConfigurationSpace
muestrea por rechazo interno respetando esas clausulas: tanto
sample_configuration() (usado por el diseno inicial) como la busqueda de
vecinos que usa el optimizador de la funcion de adquisicion de SMAC
(get_one_exchange_neighbourhood) SOLO devuelven configuraciones que las
satisfacen. Desde este codigo, smac.ask() nunca entrega una secuencia
invalida: no hay nada que reparar ni descartar de este lado (regla 5).

Dos mecanismos, ambos sobre el VALOR de h_column (nunca por posicion ni por
el numero del ID, regla 1):
  1. El dominio de cada variable period_t se restringe de entrada a los IDs
     elegibles (h >= h_base), lo que ya garantiza la regla 3 (y la regla 2
     en el primer periodo) sin necesitar clausulas.
  2. Para cada par de periodos ADYACENTES (t, t+1) se agregan clausulas
     ForbiddenAndConjunction(period_t=idA, period_t+1=idB) para todo par
     (idA, idB) con h(idB) < h(idA). "No decreciente" es una propiedad
     transitiva (se cumple en toda la secuencia si y solo si se cumple
     entre cada par de periodos adyacentes), asi que basta con prohibir los
     pares adyacentes; no hace falta uno por cada par de periodos.

Uso:
    python3 optimizer_smac_factible.py --trials 20 --seed 0
"""
import argparse
import sys

from ConfigSpace import Categorical, ConfigurationSpace, ForbiddenAndConjunction, ForbiddenEqualsClause
from smac import HyperparameterOptimizationFacade, Scenario
from smac.runhistory.dataclasses import TrialValue

import capacity_problem as cp


def _build_configspace(elegibles: "list[str]", valores: "dict[str, object]", T: int, seed: int) -> ConfigurationSpace:
    configspace = ConfigurationSpace(seed=seed)
    for t in range(T):
        configspace.add(Categorical(f"period_{t}", elegibles))

    for t in range(T - 1):
        for id_a in elegibles:
            for id_b in elegibles:
                if valores[id_b] < valores[id_a]:
                    configspace.add(
                        ForbiddenAndConjunction(
                            ForbiddenEqualsClause(configspace[f"period_{t}"], id_a),
                            ForbiddenEqualsClause(configspace[f"period_{t + 1}"], id_b),
                        )
                    )
    return configspace


def main() -> int:
    parser = argparse.ArgumentParser(
        description="SMAC (solo secuencias validas, sin reparar ni descartar) sobre la secuencia de capacidad."
    )
    parser.add_argument("--trials", type=int, default=20, help="Numero de corridas reales de mineplan (default: 20).")
    parser.add_argument("--seed", type=int, default=0, help="Semilla para reproducibilidad (default: 0).")
    parser.add_argument("--scenarios", type=int, default=1, help="NumberOfScenarios a usar durante la busqueda (default: 1).")
    parser.add_argument(
        "--h-column", type=str, default=cp.DEFAULT_H_COLUMN,
        help=f"Columna de Ejemplo.csv usada como h para la restriccion de monotonia (default: {cp.DEFAULT_H_COLUMN!r}).",
    )
    parser.add_argument(
        "--base-id", type=str, default=cp.BASELINE_ID,
        help=f"ID base: h(x[1]) >= h(base_id) (default: {cp.BASELINE_ID!r}).",
    )
    args = parser.parse_args()

    tabla_raw = cp.load_raw_table()
    csv_path = cp.PROYECTO_DIR / "resultados_smac_factible.csv"
    csv_file, writer = cp.open_results_csv(csv_path)

    mejor_npv = None
    mejor_secuencia = None

    try:
        with cp.CapacitySearchProblem(number_of_scenarios=args.scenarios) as problem:
            T = problem.number_of_periods
            elegibles, valores, _h_base_valor = cp.eligible_pool(tabla_raw, args.h_column, args.base_id)
            print(f"IDs elegibles (h={args.h_column!r} >= h({args.base_id!r})): {elegibles}")
            print(f"Periodos (NumberOfPeriods): {T}")

            configspace = _build_configspace(elegibles, valores, T, args.seed)
            print(f"Clausulas prohibidas (pares adyacentes que violan monotonia por valor): {len(configspace.forbidden_clauses)}")

            scenario = Scenario(
                configspace,
                output_directory=cp.PROYECTO_DIR / "smac3_output_factible",
                deterministic=True,
                n_trials=args.trials,
                seed=args.seed,
            )
            smac = HyperparameterOptimizationFacade(scenario, target_function=None, overwrite=True)

            mineplan_fn = cp.mineplan_objective(problem)

            for trial_idx in range(1, args.trials + 1):
                info = smac.ask()
                config = info.config
                secuencia = tuple(config[f"period_{t}"] for t in range(T))

                print(f"[{trial_idx}/{args.trials}] Probando {secuencia} ...", flush=True)

                npv, computation_time = mineplan_fn(secuencia)
                # Mismo escalado que optimizer_smac.py (commit 3d66772): el
                # encoder log-scaled de SMAC resta un epsilon de 1e-10 que,
                # en float64, se pierde contra un NPV de magnitud ~1e7.
                costo_escalado = float(-npv) / 1_000_000
                smac.tell(info, TrialValue(cost=costo_escalado, time=float(computation_time)))
                cp.log_trial(
                    writer, trial_idx, secuencia, problem.tabla,
                    problem.number_of_scenarios, npv, computation_time,
                )
                csv_file.flush()
                print(f"    NPV={npv} ComputationTime={computation_time}s", flush=True)

                if mejor_npv is None or npv > mejor_npv:
                    mejor_npv = npv
                    mejor_secuencia = secuencia
    finally:
        csv_file.close()

    print("\n=== Mejor resultado (SMAC factible) ===")
    print(f"Secuencia: {mejor_secuencia}")
    print(f"NPV: {mejor_npv}")
    print(f"Resultados guardados en {csv_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
