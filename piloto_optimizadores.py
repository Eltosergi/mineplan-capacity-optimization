#!/usr/bin/env python3
"""Fase 6: estudio piloto. Corre cada optimizador (BO-GP, COMBO en sus dos
modos de adquisicion, SMAC y TPE factibles, y una linea base de busqueda
al azar sobre secuencias validas) con N semillas sobre el caso de prueba,
usando el modo de evaluacion POR TABLA (tabla_objective.TablaObjective,
sin correr mineplan): hace falta haber corrido
evaluar_todas_las_secuencias.py de punta a punta primero (todas las
secuencias validas del caso evaluadas).

La linea base de "busqueda al azar" aqui NO es optimizer_random_search.py
(que no se toca, regla 6, y usa la semantica POSICIONAL antigua de
ids_pool/is_monotonic): es un muestreo uniforme sin reemplazo sobre
secuencias_validas(), para comparar los 5 metodos sobre exactamente el
mismo espacio de busqueda por VALOR.

Para cada semilla, BO-GP y COMBO parten del MISMO diseno inicial: ambos
llaman a cp.diseno_inicial(valid_sequences, n0, seed) con los mismos
argumentos, que es determinista, asi que esto se cumple automaticamente
sin codigo adicional (se verifica con un assert por semilla, mas abajo).

Reporta, por metodo: distancia al optimo verdadero (conocido de la tabla
precalculada), numero de secuencias unicas evaluadas, curva de
convergencia (mejor NPV visto vs. numero de evaluaciones, media y
desviacion estandar sobre las semillas) y pruebas estadisticas (Friedman
entre todos los metodos, Wilcoxon por pares) sobre la distancia final al
optimo.

NO se corrio en esta sesion (a pedido del usuario: solo escribir codigo;
ademas depende de la tabla exhaustiva, que tampoco se genero). Antes de
dejarlo con --n-seeds grande, pruebalo con --n-seeds 2 --budget 8.

Uso:
    python3 piloto_optimizadores.py --tabla resultados_todas_las_secuencias.csv --n-seeds 30 --budget 20
"""
import argparse
import csv
import statistics
import sys

import numpy as np
from scipy.stats import friedmanchisquare, wilcoxon

import capacity_problem as cp
from optimizer_bo_gp import run_bo_gp
from optimizer_combo import run_combo
from optimizer_smac_factible import run_smac_factible
from optimizer_tpe_factible import run_tpe_factible
from tabla_objective import TablaObjective


def _random_search_valido(valid_sequences, objective_fn, seed, budget):
    """Linea base: hasta `budget` secuencias validas distintas, elegidas
    uniformemente al azar (orden canonico de secuencias_validas() +
    semilla), sin repetir."""
    import random

    rng = random.Random(seed)
    orden = list(valid_sequences)
    rng.shuffle(orden)

    evaluator = cp.CachedEvaluator(objective_fn)
    for secuencia in orden:
        if evaluator.n_evaluaciones_reales >= budget:
            break
        evaluator.evaluate(secuencia)

    evaluadas = evaluator.evaluadas()
    valores = [evaluator.cache[s] for s in evaluadas]
    mejor_idx = max(range(len(valores)), key=lambda i: valores[i])
    return evaluadas, valores, evaluadas[mejor_idx], valores[mejor_idx]


def _curva_convergencia(valores: "list[float]") -> "list[float]":
    """Mejor valor visto hasta cada evaluacion (maximo acumulado)."""
    curva = []
    mejor = float("-inf")
    for v in valores:
        mejor = max(mejor, v)
        curva.append(mejor)
    return curva


def _correr_metodo(nombre: str, fn) -> dict:
    evaluadas, valores, mejor_secuencia, mejor_valor = fn()
    return {
        "metodo": nombre,
        "evaluadas": evaluadas,
        "valores": valores,
        "mejor_secuencia": mejor_secuencia,
        "mejor_valor": mejor_valor,
        "n_unicas": len(set(evaluadas)),
        "curva": _curva_convergencia(valores),
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Estudio piloto: compara BO-GP, COMBO (2 modos), SMAC/TPE factibles y random search, usando el modo por tabla."
    )
    parser.add_argument("--tabla", type=str, default="resultados_todas_las_secuencias.csv", help="CSV con todas las secuencias ya evaluadas (de evaluar_todas_las_secuencias.py).")
    parser.add_argument("--n-seeds", type=int, default=30, help="Numero de semillas (default: 30).")
    parser.add_argument("--budget", type=int, default=20, help="Presupuesto de evaluaciones reales por corrida (default: 20).")
    parser.add_argument("--n0", type=int, default=5, help="Tamano del diseno inicial para BO-GP/COMBO (default: 5).")
    parser.add_argument("--h-column", type=str, default=cp.DEFAULT_H_COLUMN, help=f"Columna usada como h (default: {cp.DEFAULT_H_COLUMN!r}).")
    parser.add_argument("--base-id", type=str, default=cp.BASELINE_ID, help=f"ID base (default: {cp.BASELINE_ID!r}).")
    parser.add_argument("--n-samples-combo", type=int, default=10, help="Muestras posteriores L de COMBO (default: 10).")
    parser.add_argument("--warmup-combo", type=int, default=100, help="Calentamiento de COMBO (default: 100).")
    parser.add_argument("--output-prefix", type=str, default="piloto", help="Prefijo de los CSV de salida (default: 'piloto').")
    args = parser.parse_args()

    tabla_obj = TablaObjective.desde_csv(cp.PROYECTO_DIR / args.tabla)
    optimo_secuencia, optimo_valor = tabla_obj.optimo()
    print(f"Optimo verdadero (de la tabla precalculada): {optimo_secuencia} NPV={optimo_valor}")
    print(f"Secuencias en la tabla: {len(tabla_obj)}")

    tabla_raw = cp.load_raw_table()
    T = len(optimo_secuencia)
    elegibles, valores, _h_base_valor = cp.eligible_pool(tabla_raw, args.h_column, args.base_id)
    valid_sequences = cp.secuencias_validas(tabla_raw, T, args.h_column, args.base_id)
    if set(valid_sequences) != set(tabla_obj.valores.keys()):
        print(
            "Advertencia: el conjunto de secuencias validas no coincide con las filas "
            "de la tabla precalculada (revisa --h-column/--base-id, o si "
            "evaluar_todas_las_secuencias.py todavia no termino).",
            file=sys.stderr,
        )

    def vecinos_fn(secuencia):
        return cp.vecinos_validos(secuencia, tabla_raw, args.h_column, args.base_id)

    resultados_por_metodo: "dict[str, list[dict]]" = {}

    for seed in range(args.n_seeds):
        print(f"\n=== Semilla {seed} ===", flush=True)

        diseno_bo_gp = cp.diseno_inicial(valid_sequences, args.n0, seed)
        diseno_combo = cp.diseno_inicial(valid_sequences, args.n0, seed)
        assert diseno_bo_gp == diseno_combo, "BO-GP y COMBO deberian partir del mismo diseno inicial"

        corridas = {
            "random_search": lambda: _random_search_valido(valid_sequences, tabla_obj, seed, args.budget),
            "bo_gp": lambda: run_bo_gp(
                valid_sequences=valid_sequences, elegibles=elegibles, T=T, objective_fn=tabla_obj,
                n0=args.n0, seed=seed, budget=args.budget,
            ),
            "combo_local": lambda: run_combo(
                valid_sequences=valid_sequences, elegibles=elegibles, T=T, objective_fn=tabla_obj,
                n0=args.n0, seed=seed, budget=args.budget, vecinos_fn=vecinos_fn,
                n_samples=args.n_samples_combo, warmup=args.warmup_combo, acq_mode="local",
            ),
            "combo_exhaustive": lambda: run_combo(
                valid_sequences=valid_sequences, elegibles=elegibles, T=T, objective_fn=tabla_obj,
                n0=args.n0, seed=seed, budget=args.budget, vecinos_fn=vecinos_fn,
                n_samples=args.n_samples_combo, warmup=args.warmup_combo, acq_mode="exhaustive",
            ),
            "smac_factible": lambda: run_smac_factible(
                elegibles=elegibles, valores=valores, T=T, objective_fn=tabla_obj,
                trials=args.budget, seed=seed,
                output_directory=cp.PROYECTO_DIR / "smac3_output_piloto" / f"seed_{seed}",
            ),
            "tpe_factible": lambda: run_tpe_factible(
                elegibles=elegibles, valores=valores, h_base_valor=valores[args.base_id], T=T,
                objective_fn=tabla_obj, trials=args.budget, seed=seed,
            ),
        }

        for nombre, fn in corridas.items():
            resultado = _correr_metodo(nombre, fn)
            resultado["seed"] = seed
            resultado["distancia_al_optimo"] = optimo_valor - resultado["mejor_valor"]
            resultados_por_metodo.setdefault(nombre, []).append(resultado)
            print(
                f"  {nombre}: mejor={resultado['mejor_valor']:.2f} "
                f"(distancia={resultado['distancia_al_optimo']:.2f}) "
                f"unicas={resultado['n_unicas']}",
                flush=True,
            )

    # --- CSV de distancias finales y numero de secuencias unicas ---
    distancias_path = cp.PROYECTO_DIR / f"{args.output_prefix}_distancias.csv"
    with distancias_path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["metodo", "seed", "mejor_valor", "distancia_al_optimo", "n_unicas"])
        for nombre, corridas_metodo in resultados_por_metodo.items():
            for r in corridas_metodo:
                writer.writerow([nombre, r["seed"], r["mejor_valor"], r["distancia_al_optimo"], r["n_unicas"]])
    print(f"\nDistancias finales guardadas en {distancias_path}")

    # --- CSV de curvas de convergencia (media/desv.est. por evaluacion) ---
    curvas_path = cp.PROYECTO_DIR / f"{args.output_prefix}_curvas_convergencia.csv"
    with curvas_path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["metodo", "evaluacion", "media", "desviacion_estandar"])
        for nombre, corridas_metodo in resultados_por_metodo.items():
            curvas = [r["curva"] for r in corridas_metodo]
            largo_min = min(len(c) for c in curvas)
            for i in range(largo_min):
                valores_i = [c[i] for c in curvas]
                writer.writerow([nombre, i + 1, statistics.mean(valores_i), statistics.pstdev(valores_i)])
    print(f"Curvas de convergencia guardadas en {curvas_path}")

    # --- Pruebas estadisticas sobre la distancia final al optimo ---
    metodos = list(resultados_por_metodo.keys())
    matriz = np.array([
        [resultados_por_metodo[m][seed]["distancia_al_optimo"] for m in metodos]
        for seed in range(args.n_seeds)
    ])  # (n_seeds, n_metodos)

    print("\n=== Prueba de Friedman (entre todos los metodos) ===")
    if args.n_seeds >= 3 and len(metodos) >= 3:
        estadistico, p_valor = friedmanchisquare(*[matriz[:, j] for j in range(len(metodos))])
        print(f"Chi2={estadistico:.4f}  p-valor={p_valor:.6f}")
    else:
        print("Se necesitan al menos 3 semillas y 3 metodos para Friedman; se omite.")

    print("\n=== Pruebas de Wilcoxon por pares (distancia al optimo) ===")
    resultados_wilcoxon = []
    for i in range(len(metodos)):
        for j in range(i + 1, len(metodos)):
            a, b = matriz[:, i], matriz[:, j]
            if np.allclose(a, b):
                print(f"{metodos[i]} vs {metodos[j]}: identicos, se omite (Wilcoxon no esta definido)")
                continue
            estadistico, p_valor = wilcoxon(a, b)
            print(f"{metodos[i]} vs {metodos[j]}: W={estadistico:.4f}  p-valor={p_valor:.6f}")
            resultados_wilcoxon.append((metodos[i], metodos[j], estadistico, p_valor))

    wilcoxon_path = cp.PROYECTO_DIR / f"{args.output_prefix}_wilcoxon.csv"
    with wilcoxon_path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["metodo_a", "metodo_b", "estadistico", "p_valor"])
        writer.writerows(resultados_wilcoxon)
    print(f"\nResultados de Wilcoxon guardados en {wilcoxon_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
