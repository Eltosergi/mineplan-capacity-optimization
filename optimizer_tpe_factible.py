#!/usr/bin/env python3
"""Version de TPE/Optuna (Fase 4) que SOLO propone secuencias validas
(reglas 4 y 5): nada de reparar ni descartar, a diferencia de
optimizer_tpe.py (que no se modifica: regla 6).

Investigacion (por que esto SI se puede sin reparar/descartar): Optuna usa
un espacio de busqueda "define-by-run": las opciones que se le pasan a
trial.suggest_categorical() para el periodo t se pueden calcular en el
momento, a partir de lo que ya se sugirio para los periodos anteriores
DENTRO DEL MISMO trial. Esto es un patron estandar y documentado de Optuna
para espacios de busqueda condicionales/secuenciales (no un truco interno).
Concretamente, el dominio ofrecido en cada suggest_categorical ya viene
restringido a los IDs cuyo h es >= el h del ID elegido en el periodo
anterior (o >= h_base en el primer periodo, regla 3): el TPESampler nunca
ve ni puede proponer un ID fuera de ese dominio valido para esa posicion,
en esa trayectoria. No hace falta podar (TrialPruned, como hace
optimizer_tpe.py) ni reparar nada despues: cada trial generado es valido
por construccion.

Uso:
    python3 optimizer_tpe_factible.py --trials 20 --seed 0
"""
import argparse
import sys

import optuna

import capacity_problem as cp

optuna.logging.set_verbosity(optuna.logging.WARNING)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="TPE/Optuna (solo secuencias validas, sin reparar ni descartar) sobre la secuencia de capacidad."
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
    csv_path = cp.PROYECTO_DIR / "resultados_tpe_factible.csv"
    csv_file, writer = cp.open_results_csv(csv_path)

    study = optuna.create_study(
        direction="maximize",
        sampler=optuna.samplers.TPESampler(seed=args.seed),
    )

    mejor_npv = None
    mejor_secuencia = None

    try:
        with cp.CapacitySearchProblem(number_of_scenarios=args.scenarios) as problem:
            T = problem.number_of_periods
            elegibles, valores, h_base_valor = cp.eligible_pool(tabla_raw, args.h_column, args.base_id)
            print(f"IDs elegibles (h={args.h_column!r} >= h({args.base_id!r})): {elegibles}")
            print(f"Periodos (NumberOfPeriods): {T}")

            mineplan_fn = cp.mineplan_objective(problem)

            for trial_idx in range(1, args.trials + 1):
                trial = study.ask()

                secuencia_lista: list[str] = []
                h_min = h_base_valor
                for t in range(T):
                    opciones = [id_ for id_ in elegibles if valores[id_] >= h_min]
                    elegido = trial.suggest_categorical(f"period_{t}", opciones)
                    secuencia_lista.append(elegido)
                    h_min = valores[elegido]
                secuencia = tuple(secuencia_lista)

                print(f"[{trial_idx}/{args.trials}] Probando {secuencia} ...", flush=True)

                npv, computation_time = mineplan_fn(secuencia)
                study.tell(trial, float(npv))
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

    print("\n=== Mejor resultado (TPE factible) ===")
    print(f"Secuencia: {mejor_secuencia}")
    print(f"NPV: {mejor_npv}")
    print(f"Resultados guardados en {csv_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
