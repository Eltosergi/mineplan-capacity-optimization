#!/usr/bin/env python3
"""Optimizador 4/4: MCS (Multilevel Coordinate Search), adaptado a discreto.

Huyer & Neumaier (1999), "Global Optimization by Multilevel Coordinate
Search". El paper original trabaja sobre cajas continuas con division en
3 usando interpolacion cuadratica para elegir los puntos de corte, mas una
fase de busqueda local con cuasi-Newton. Aqui se simplifica porque nuestro
dominio ya es discreto (9 IDs por periodo, no un continuo): cada dimension
es un entero en [0, m-1] (indice dentro del pool de IDs). La idea central
del paper -mantener "niveles" por dimension para repartir la exploracion
entre regiones poco visitadas y regiones prometedoras- se conserva:

  - Cada caja tiene limites enteros por dimension y un nivel (cantidad de
    veces que esa dimension ya fue dividida en esa caja).
  - En cada iteracion se elige la caja con MENOR suma de niveles (la menos
    explorada); el empate se rompe por mejor valor encontrado.
  - Se divide esa caja por la dimension con menor nivel, bisectandola en 2
    (el paper usa trisection con interpolacion cuadratica; aqui, bisection
    simple sobre el entero, que es la simplificacion documentada).

run_mcs() es el algoritmo puro (no sabe nada de mineplan ni de IDs: solo
ve "objective_fn(x_idx) -> costo a minimizar"), asi se puede probar con
una funcion sintetica antes de conectarla a una corrida real.

Uso:
    python3 optimizer_mcs.py --trials 10 --seed 0
"""
import argparse
import sys
from dataclasses import dataclass, field


@dataclass
class _Box:
    lower: list[int]
    upper: list[int]
    x_idx: list[int]
    levels: list[int]
    cost: float


def run_mcs(n: int, m: int, budget: int, objective_fn, cache: dict | None = None) -> tuple[float, list[int], int]:
    """Busca el x_idx (vector de n enteros en [0, m-1]) que minimiza
    objective_fn, gastando a lo sumo `budget` evaluaciones NUEVAS (los
    puntos repetidos se resuelven con `cache` sin gastar presupuesto).

    Devuelve (mejor_costo, mejor_x_idx, evaluaciones_reales_usadas)."""
    if cache is None:
        cache = {}
    evaluaciones = 0

    def evaluar(x_idx: list[int]) -> float:
        nonlocal evaluaciones
        clave = tuple(x_idx)
        if clave in cache:
            return cache[clave]
        costo = objective_fn(x_idx)
        cache[clave] = costo
        evaluaciones += 1
        return costo

    lower0 = [0] * n
    upper0 = [m - 1] * n
    x0 = [(lo + hi) // 2 for lo, hi in zip(lower0, upper0)]
    costo0 = evaluar(x0)

    mejor_costo = costo0
    mejor_x = x0[:]

    cajas = [_Box(lower0, upper0, x0, [0] * n, costo0)]

    while evaluaciones < budget and cajas:
        cajas.sort(key=lambda b: (sum(b.levels), b.cost))
        caja = cajas.pop(0)

        candidatas = [i for i in range(n) if caja.upper[i] > caja.lower[i]]
        if not candidatas:
            continue  # ya es un punto unico, no se puede seguir dividiendo
        dim = min(candidatas, key=lambda i: caja.levels[i])

        lo, hi = caja.lower[dim], caja.upper[dim]
        mid = (lo + hi) // 2

        for nuevo_lo, nuevo_hi in ((lo, mid), (mid + 1, hi)):
            if evaluaciones >= budget:
                break
            nuevo_lower = caja.lower[:]
            nuevo_upper = caja.upper[:]
            nuevo_lower[dim], nuevo_upper[dim] = nuevo_lo, nuevo_hi
            nuevo_x = caja.x_idx[:]
            nuevo_x[dim] = (nuevo_lo + nuevo_hi) // 2
            nuevo_levels = caja.levels[:]
            nuevo_levels[dim] += 1

            costo = evaluar(nuevo_x)
            if costo < mejor_costo:
                mejor_costo = costo
                mejor_x = nuevo_x[:]
            cajas.append(_Box(nuevo_lower, nuevo_upper, nuevo_x, nuevo_levels, costo))

    return mejor_costo, mejor_x, evaluaciones


def _cli_main() -> int:
    import capacity_problem as cp

    parser = argparse.ArgumentParser(description="MCS (adaptado a discreto) sobre la secuencia de capacidad.")
    parser.add_argument("--trials", type=int, default=10, help="Numero de corridas reales de mineplan (default: 10).")
    parser.add_argument("--seed", type=int, default=0, help="No usado por MCS (determinista), se mantiene por simetria con los otros 3 scripts.")
    parser.add_argument("--scenarios", type=int, default=1, help="NumberOfScenarios a usar durante la busqueda (default: 1).")
    args = parser.parse_args()

    csv_path = cp.PROYECTO_DIR / "resultados_mcs.csv"
    csv_file, writer = cp.open_results_csv(csv_path)
    trial_counter = {"n": 0}

    try:
        with cp.CapacitySearchProblem(number_of_scenarios=args.scenarios) as problem:
            pool = problem.pool
            m = len(pool)
            n = problem.number_of_periods
            print(f"Pool de IDs seleccionables: {pool}")
            print(f"Periodos (NumberOfPeriods): {n}")

            def objective_fn(x_idx: list[int]) -> float:
                secuencia_propuesta = [pool[i] for i in x_idx]
                secuencia = cp.repair_monotonic(secuencia_propuesta, pool)
                print(f"[{trial_counter['n'] + 1}/{args.trials}] Probando {secuencia} ...", flush=True)

                npv, computation_time = problem.evaluate(secuencia)
                trial_counter["n"] += 1
                cp.log_trial(writer, trial_counter["n"], secuencia, problem.tabla, problem.number_of_scenarios, npv, computation_time)
                csv_file.flush()
                print(f"    NPV={npv} ComputationTime={computation_time}s", flush=True)
                return float(-npv)  # MCS minimiza; nosotros maximizamos NPV.

            mejor_costo, mejor_x, usadas = run_mcs(n, m, args.trials, objective_fn)
            mejor_secuencia = cp.repair_monotonic([pool[i] for i in mejor_x], pool)
    finally:
        csv_file.close()

    print("\n=== Mejor resultado (MCS) ===")
    print(f"Secuencia: {mejor_secuencia}")
    print(f"NPV: {-mejor_costo}")
    print(f"Corridas reales usadas: {usadas}")
    print(f"Resultados guardados en {csv_path}")
    return 0


if __name__ == "__main__":
    sys.exit(_cli_main())
