"""Pruebas de optimizer_bo_gp.py con funciones objetivo sinteticas (sin
correr mineplan): secuencias propuestas validas y sin repetir, invarianza
a permutar Ejemplo.csv, y que encuentra el optimo de una funcion sintetica
simple con menos evaluaciones que una busqueda al azar, en promedio sobre
varias semillas.

Uso:
    pytest test_bo_gp.py -v
"""
import random
import statistics

import capacity_problem as cp
from optimizer_bo_gp import run_bo_gp

T = 3
H_COLUMN, BASE_ID = "h", "BASE"


def _tabla_pequena() -> "dict[str, dict[str, str]]":
    return {
        "BASE": {"h": "10"},
        "E1": {"h": "10"},
        "E2": {"h": "20"},
        "E3": {"h": "30"},
        "E4": {"h": "40"},
        "E5": {"h": "50"},
        "BAJO": {"h": "5"},
    }


def _tabla_pequena_permutada() -> "dict[str, dict[str, str]]":
    return dict(reversed(list(_tabla_pequena().items())))


def test_bo_gp_propuestas_validas_y_sin_repetir():
    tabla = _tabla_pequena()
    valid_sequences = cp.secuencias_validas(tabla, T, H_COLUMN, BASE_ID)
    valid_set = set(valid_sequences)
    elegibles, valores, _h_base = cp.eligible_pool(tabla, H_COLUMN, BASE_ID)

    llamados = []

    def objective_fn(secuencia):
        llamados.append(secuencia)
        return float(sum(valores[id_] for id_ in secuencia))

    evaluadas, valores_obj, mejor, mejor_val = run_bo_gp(
        valid_sequences=valid_sequences, elegibles=elegibles, T=T,
        objective_fn=objective_fn, n0=3, seed=0, budget=10, n_restarts_optimizer=2,
    )

    assert len(evaluadas) == 10
    assert len(set(evaluadas)) == 10
    assert all(s in valid_set for s in evaluadas)
    assert llamados == evaluadas  # objective_fn se llamo exactamente una vez por secuencia, en orden
    assert mejor in valid_set
    assert mejor_val == max(valores_obj)


def test_bo_gp_invariante_a_permutar_filas():
    def correr(tabla):
        valid_sequences = cp.secuencias_validas(tabla, T, H_COLUMN, BASE_ID)
        elegibles, valores, _h_base = cp.eligible_pool(tabla, H_COLUMN, BASE_ID)

        def objective_fn(secuencia):
            return float(sum(valores[id_] for id_ in secuencia))

        return run_bo_gp(
            valid_sequences=valid_sequences, elegibles=elegibles, T=T,
            objective_fn=objective_fn, n0=3, seed=1, budget=8, n_restarts_optimizer=2,
        )

    r1 = correr(_tabla_pequena())
    r2 = correr(_tabla_pequena_permutada())

    assert r1[0] == r2[0]  # mismas secuencias evaluadas, mismo orden
    assert r1[2] == r2[2]  # misma mejor secuencia
    assert r1[3] == r2[3]  # mismo mejor valor


def test_bo_gp_encuentra_optimo_con_menos_evaluaciones_que_random_search():
    """Funcion sintetica con un unico optimo (distancia de Hamming a un
    objetivo fijo, negada): el kernel one-hot de BO-GP deberia explotar
    esa estructura mucho mejor que elegir secuencias al azar."""
    tabla = _tabla_pequena()
    valid_sequences = cp.secuencias_validas(tabla, T, H_COLUMN, BASE_ID)
    elegibles, _valores, _h_base = cp.eligible_pool(tabla, H_COLUMN, BASE_ID)
    objetivo = valid_sequences[len(valid_sequences) // 2]

    def objective_fn(secuencia):
        return float(-sum(1 for a, b in zip(secuencia, objetivo) if a != b))

    budget = 15
    n0 = 3
    seeds = range(6)

    evals_random = []
    evals_bo_gp = []

    for seed in seeds:
        rng = random.Random(seed)
        orden_aleatorio = valid_sequences[:]
        rng.shuffle(orden_aleatorio)
        probadas_random = orden_aleatorio[:budget]
        evals_random.append(
            probadas_random.index(objetivo) + 1 if objetivo in probadas_random else budget + 1
        )

        evaluadas, _valores_obj, _mejor, _mejor_val = run_bo_gp(
            valid_sequences=valid_sequences, elegibles=elegibles, T=T,
            objective_fn=objective_fn, n0=n0, seed=seed, budget=budget, n_restarts_optimizer=2,
        )
        evals_bo_gp.append(evaluadas.index(objetivo) + 1 if objetivo in evaluadas else budget + 1)

    assert statistics.mean(evals_bo_gp) < statistics.mean(evals_random)
