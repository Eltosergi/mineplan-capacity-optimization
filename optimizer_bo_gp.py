#!/usr/bin/env python3
"""Metodo propuesto: Optimizacion Bayesiana con Proceso Gaussiano (BO-GP) y
transformacion categorica, sobre la secuencia de capacidad.

Garrido-Merchan, E.C.; Hernandez-Lobato, D. (2020), "Dealing with
Categorical and Integer-valued Variables in Bayesian Optimization with
Gaussian Processes", Neurocomputing 380. Cada ID de capacidad se codifica
como un bloque one-hot (un 1 en su posicion dentro del orden canonico de
IDs elegibles, el resto 0); una secuencia de T periodos es la concatenacion
de T bloques one-hot de largo m (m = numero de IDs elegibles), es decir un
vector en R^(T*m). El proceso gaussiano modela el NPV normalizado sobre ese
espacio con un kernel Matern(nu=2.5) con una escala de longitud por
dimension (ARD), multiplicado por una constante y con un termino de ruido
chico fijo (para estabilidad numerica del Cholesky, no como ruido de
observacion real: la evaluacion es determinista a NumberOfScenarios fijo).

A diferencia de random_search/SMAC/TPE/MCS, aqui Expected Improvement (EI)
se evalua exhaustivamente sobre TODAS las secuencias validas del problema
(secuencias_validas() de capacity_problem.py) que aun no se evaluaron, no
sobre una muestra: el espacio de secuencias validas es finito y chico
(1287 para el caso de prueba por defecto), asi que no hace falta optimizar
la adquisicion con un metodo numerico aparte.

Separa el algoritmo puro (run_bo_gp(), que solo ve una funcion objetivo
inyectable) de la parte que llama a mineplan, igual que run_mcs() en
optimizer_mcs.py: run_bo_gp() se puede probar con funciones sinteticas sin
correr una sola vez el binario real.

Uso:
    python3 optimizer_bo_gp.py --trials 20 --seed 0 --n0 5
"""
import argparse
import sys
from typing import Callable, Sequence

import numpy as np
from scipy.stats import norm
from sklearn.gaussian_process import GaussianProcessRegressor
from sklearn.gaussian_process.kernels import ConstantKernel, Matern, WhiteKernel

import capacity_problem as cp


def _encoder(elegibles: Sequence[str]) -> "tuple[dict[str, int], int]":
    """Indice de cada ID dentro del orden canonico de `elegibles` (su
    posicion dentro del bloque one-hot que le corresponde), y m = numero de
    IDs elegibles (largo de cada bloque)."""
    indice_de = {id_: i for i, id_ in enumerate(elegibles)}
    return indice_de, len(elegibles)


def _encode_sequence(secuencia: Sequence[str], indice_de: "dict[str, int]", T: int, m: int) -> "np.ndarray":
    """Codifica una secuencia de T IDs como un vector one-hot de largo T*m:
    un bloque de largo m por periodo, con un 1 en indice_de[id_elegido]."""
    vector = np.zeros(T * m)
    for t, id_ in enumerate(secuencia):
        vector[t * m + indice_de[id_]] = 1.0
    return vector


def _encode_batch(secuencias: Sequence[Sequence[str]], indice_de: "dict[str, int]", T: int, m: int) -> "np.ndarray":
    return np.array([_encode_sequence(s, indice_de, T, m) for s in secuencias])


def expected_improvement(mu: "np.ndarray", sigma: "np.ndarray", f_best: float, xi: float = 0.0) -> "np.ndarray":
    """EI de maximizacion: E[max(f(x) - f_best - xi, 0)] bajo
    f(x) ~ N(mu, sigma^2). Devuelve 0 donde sigma es practicamente 0 (sin
    incertidumbre, nada que explorar ahi)."""
    mu = np.asarray(mu, dtype=float)
    sigma = np.asarray(sigma, dtype=float)
    con_incertidumbre = sigma > 1e-12

    imp = mu - f_best - xi
    z = np.zeros_like(mu)
    z[con_incertidumbre] = imp[con_incertidumbre] / sigma[con_incertidumbre]

    ei = imp * norm.cdf(z) + sigma * norm.pdf(z)
    return np.where(con_incertidumbre, ei, 0.0)


def _build_kernel(n_dims: int):
    return (
        ConstantKernel(1.0, (1e-3, 1e3))
        * Matern(length_scale=np.ones(n_dims), length_scale_bounds=(1e-2, 1e2), nu=2.5)
        + WhiteKernel(noise_level=1e-6, noise_level_bounds="fixed")
    )


def run_bo_gp(
    valid_sequences: Sequence[Sequence[str]],
    elegibles: Sequence[str],
    T: int,
    objective_fn: Callable[[Sequence[str]], float],
    n0: int,
    seed: int,
    budget: int,
    n_restarts_optimizer: int = 10,
) -> "tuple[list[tuple[str, ...]], list[float], tuple[str, ...], float]":
    """Algoritmo BO-GP puro: no sabe nada de mineplan, Decimal ni CSV, solo
    ve objective_fn(secuencia) -> float a MAXIMIZAR. Evalua como maximo
    `budget` secuencias reales en total (n0 del diseno inicial + el resto
    por EI), nunca repite una secuencia (via CachedEvaluator, regla 4) y
    solo propone secuencias de `valid_sequences` (regla 5: nada de reparar
    ni descartar).

    Devuelve (secuencias_evaluadas_en_orden, valores, mejor_secuencia,
    mejor_valor)."""
    valid_sequences = [tuple(s) for s in valid_sequences]
    indice_de, m = _encoder(elegibles)
    n_dims = T * m

    evaluator = cp.CachedEvaluator(objective_fn)

    diseno = cp.diseno_inicial(valid_sequences, n0, seed)
    for s in diseno:
        if evaluator.n_evaluaciones_reales >= budget:
            break
        evaluator.evaluate(s)

    kernel = _build_kernel(n_dims)

    while evaluator.n_evaluaciones_reales < budget:
        evaluadas = evaluator.evaluadas()
        y = np.array([evaluator.cache[s] for s in evaluadas], dtype=float)

        y_mean = y.mean()
        y_std = y.std()
        if y_std < 1e-12:
            y_std = 1.0
        y_norm = (y - y_mean) / y_std

        X = _encode_batch(evaluadas, indice_de, T, m)

        gp = GaussianProcessRegressor(
            kernel=kernel,
            normalize_y=False,
            n_restarts_optimizer=n_restarts_optimizer,
            random_state=seed,
        )
        gp.fit(X, y_norm)

        evaluadas_set = set(evaluadas)
        pendientes = [s for s in valid_sequences if s not in evaluadas_set]
        if not pendientes:
            break

        X_pendientes = _encode_batch(pendientes, indice_de, T, m)
        mu, sigma = gp.predict(X_pendientes, return_std=True)

        ei = expected_improvement(mu, sigma, f_best=float(y_norm.max()))
        siguiente = pendientes[int(np.argmax(ei))]
        evaluator.evaluate(siguiente)

    evaluadas = evaluator.evaluadas()
    valores = [evaluator.cache[s] for s in evaluadas]
    mejor_idx = max(range(len(valores)), key=lambda i: valores[i])
    return evaluadas, valores, evaluadas[mejor_idx], valores[mejor_idx]


def _cli_main() -> int:
    parser = argparse.ArgumentParser(
        description="BO-GP (Proceso Gaussiano + transformacion categorica) sobre la secuencia de capacidad."
    )
    parser.add_argument(
        "--trials", type=int, default=20,
        help="Numero de corridas reales de mineplan, incluyendo el diseno inicial (default: 20).",
    )
    parser.add_argument("--seed", type=int, default=0, help="Semilla para reproducibilidad (default: 0).")
    parser.add_argument("--scenarios", type=int, default=1, help="NumberOfScenarios a usar durante la busqueda (default: 1).")
    parser.add_argument("--n0", type=int, default=5, help="Tamano del diseno inicial (default: 5).")
    parser.add_argument(
        "--h-column", type=str, default=cp.DEFAULT_H_COLUMN,
        help=f"Columna de Ejemplo.csv usada como h para la restriccion de monotonia (default: {cp.DEFAULT_H_COLUMN!r}).",
    )
    parser.add_argument(
        "--base-id", type=str, default=cp.BASELINE_ID,
        help=f"ID base: h(x[1]) >= h(base_id) (default: {cp.BASELINE_ID!r}).",
    )
    args = parser.parse_args()

    if args.n0 > args.trials:
        print(f"Error: --n0 ({args.n0}) no puede ser mayor que --trials ({args.trials})", file=sys.stderr)
        return 1

    tabla_raw = cp.load_raw_table()
    csv_path = cp.PROYECTO_DIR / "resultados_bo_gp.csv"
    csv_file, writer = cp.open_results_csv(csv_path)
    trial_counter = {"n": 0}

    try:
        with cp.CapacitySearchProblem(number_of_scenarios=args.scenarios) as problem:
            T = problem.number_of_periods
            elegibles, _valores, _h_base_valor = cp.eligible_pool(tabla_raw, args.h_column, args.base_id)
            print(f"IDs elegibles (h={args.h_column!r} >= h({args.base_id!r})): {elegibles}")
            print(f"Periodos (NumberOfPeriods): {T}")

            valid_sequences = cp.secuencias_validas(tabla_raw, T, args.h_column, args.base_id)
            print(f"Secuencias validas totales: {len(valid_sequences)}")

            mineplan_fn = cp.mineplan_objective(problem)

            def objective_fn(secuencia: Sequence[str]) -> float:
                trial_counter["n"] += 1
                print(f"[{trial_counter['n']}/{args.trials}] Probando {secuencia} ...", flush=True)
                npv, computation_time = mineplan_fn(secuencia)
                cp.log_trial(
                    writer, trial_counter["n"], secuencia, problem.tabla,
                    problem.number_of_scenarios, npv, computation_time,
                )
                csv_file.flush()
                print(f"    NPV={npv} ComputationTime={computation_time}s", flush=True)
                return float(npv)

            evaluadas, valores, mejor_secuencia, mejor_npv = run_bo_gp(
                valid_sequences=valid_sequences,
                elegibles=elegibles,
                T=T,
                objective_fn=objective_fn,
                n0=args.n0,
                seed=args.seed,
                budget=args.trials,
            )
    finally:
        csv_file.close()

    print("\n=== Mejor resultado (BO-GP) ===")
    print(f"Secuencia: {mejor_secuencia}")
    print(f"NPV: {mejor_npv}")
    print(f"Secuencias evaluadas: {len(evaluadas)}")
    print(f"Resultados guardados en {csv_path}")
    return 0


if __name__ == "__main__":
    sys.exit(_cli_main())
