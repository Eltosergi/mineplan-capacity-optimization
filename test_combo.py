"""Pruebas de optimizer_combo.py con funciones objetivo sinteticas (sin
correr mineplan): el kernel en forma cerrada contra scipy.linalg.expm,
secuencias propuestas validas y sin repetir (en los dos modos de
adquisicion), invarianza a permutar Ejemplo.csv, y que encuentra el optimo
de una funcion sintetica simple con menos evaluaciones que una busqueda al
azar, en promedio sobre varias semillas.

Uso:
    pytest test_combo.py -v
"""
import random
import statistics

import numpy as np
from scipy.linalg import expm

import capacity_problem as cp
from optimizer_combo import dense_diffusion_kernel, run_combo

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


def _vecinos_fn_para(tabla):
    return lambda secuencia: cp.vecinos_validos(secuencia, tabla, H_COLUMN, BASE_ID)


# ---------------------------------------------------------------------------
# Kernel en forma cerrada vs. exp(-beta*L) con L el laplaciano del grafo
# completo (L = m*I - J).
# ---------------------------------------------------------------------------

def test_dense_diffusion_kernel_coincide_con_expm():
    for m in (2, 3, 5, 9):
        L = m * np.eye(m) - np.ones((m, m))
        for beta in (0.0, 0.05, 0.3, 1.0, 3.0):
            esperado = expm(-beta * L)
            obtenido = dense_diffusion_kernel(m, beta)
            assert np.allclose(obtenido, esperado, atol=1e-8), (m, beta)


# ---------------------------------------------------------------------------
# Secuencias propuestas validas y sin repetir, en los dos modos de
# adquisicion.
# ---------------------------------------------------------------------------

def _correr_combo(tabla, acq_mode, seed=0, budget=10, n0=3):
    valid_sequences = cp.secuencias_validas(tabla, T, H_COLUMN, BASE_ID)
    elegibles, valores, _h_base = cp.eligible_pool(tabla, H_COLUMN, BASE_ID)

    llamados = []

    def objective_fn(secuencia):
        llamados.append(secuencia)
        return float(sum(valores[id_] for id_ in secuencia))

    resultado = run_combo(
        valid_sequences=valid_sequences, elegibles=elegibles, T=T,
        objective_fn=objective_fn, n0=n0, seed=seed, budget=budget,
        vecinos_fn=_vecinos_fn_para(tabla), n_samples=5, warmup=20,
        acq_mode=acq_mode, n_starts=5,
    )
    return resultado, llamados, set(valid_sequences)


def test_combo_propuestas_validas_y_sin_repetir_exhaustive():
    (evaluadas, valores_obj, mejor, mejor_val), llamados, valid_set = _correr_combo(
        _tabla_pequena(), acq_mode="exhaustive", budget=8,
    )
    assert len(evaluadas) == 8
    assert len(set(evaluadas)) == 8
    assert all(s in valid_set for s in evaluadas)
    assert llamados == evaluadas
    assert mejor_val == max(valores_obj)


def test_combo_propuestas_validas_y_sin_repetir_local():
    (evaluadas, valores_obj, mejor, mejor_val), llamados, valid_set = _correr_combo(
        _tabla_pequena(), acq_mode="local", budget=8,
    )
    assert len(evaluadas) == 8
    assert len(set(evaluadas)) == 8
    assert all(s in valid_set for s in evaluadas)
    assert llamados == evaluadas
    assert mejor_val == max(valores_obj)


# ---------------------------------------------------------------------------
# Invarianza a permutar las filas de la tabla (modo exhaustive: el modo
# local tambien depende de rng.choice sobre una lista cuyo orden ya es
# canonico via secuencias_validas/eligible_pool, asi que hereda la misma
# invarianza).
# ---------------------------------------------------------------------------

def test_combo_invariante_a_permutar_filas():
    resultado_a, _, _ = _correr_combo(_tabla_pequena(), acq_mode="exhaustive", seed=2, budget=8)
    resultado_b, _, _ = _correr_combo(_tabla_pequena_permutada(), acq_mode="exhaustive", seed=2, budget=8)

    evaluadas_a, _valores_a, mejor_a, mejor_val_a = resultado_a
    evaluadas_b, _valores_b, mejor_b, mejor_val_b = resultado_b

    assert evaluadas_a == evaluadas_b
    assert mejor_a == mejor_b
    assert mejor_val_a == mejor_val_b


# ---------------------------------------------------------------------------
# Encuentra el optimo de una funcion sintetica con menos evaluaciones que
# una busqueda al azar, en promedio sobre varias semillas (modo exhaustive,
# para que la comparacion con BO-GP aisle el efecto del kernel/modelo y no
# el de la busqueda local).
# ---------------------------------------------------------------------------

def test_combo_encuentra_optimo_con_menos_evaluaciones_que_random_search():
    """Usa T=4 (en vez del T=3 del resto del archivo, modulo-level): con
    T=3 la distancia de Hamming al objetivo solo toma 4 valores distintos
    (0..3), un paisaje de recompensa demasiado plano/con muchos empates
    para que el kernel muestre ventaja frente al azar con un presupuesto
    chico y muy pocos puntos iniciales (el MCMC del horseshoe no tiene
    evidencia suficiente para aprender nada util todavia). Con T=4 hay 5
    niveles distintos, mas diseno inicial y mas presupuesto/semillas."""
    tabla = _tabla_pequena()
    T_local = 4
    valid_sequences = cp.secuencias_validas(tabla, T_local, H_COLUMN, BASE_ID)
    elegibles, _valores, _h_base = cp.eligible_pool(tabla, H_COLUMN, BASE_ID)
    objetivo = valid_sequences[len(valid_sequences) // 2]

    def objective_fn(secuencia):
        return float(-sum(1 for a, b in zip(secuencia, objetivo) if a != b))

    budget = 25
    n0 = 5
    seeds = range(8)

    evals_random = []
    evals_combo = []

    for seed in seeds:
        rng = random.Random(seed)
        orden_aleatorio = valid_sequences[:]
        rng.shuffle(orden_aleatorio)
        probadas_random = orden_aleatorio[:budget]
        evals_random.append(
            probadas_random.index(objetivo) + 1 if objetivo in probadas_random else budget + 1
        )

        evaluadas, _valores_obj, _mejor, _mejor_val = run_combo(
            valid_sequences=valid_sequences, elegibles=elegibles, T=T_local,
            objective_fn=objective_fn, n0=n0, seed=seed, budget=budget,
            vecinos_fn=_vecinos_fn_para(tabla), n_samples=5, warmup=20,
            acq_mode="exhaustive",
        )
        evals_combo.append(evaluadas.index(objetivo) + 1 if objetivo in evaluadas else budget + 1)

    assert statistics.mean(evals_combo) < statistics.mean(evals_random)
