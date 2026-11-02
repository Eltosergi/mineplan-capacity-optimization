"""Pruebas de la infraestructura de la Fase 1 (capacity_problem.py):
conteo de secuencias validas, invarianza a permutar Ejemplo.csv, y que la
restriccion se revisa por VALOR (no por posicion ni por el numero del ID).
No corren mineplan.

Uso:
    pytest test_capacity_problem.py -v
"""
import itertools

import capacity_problem as cp


def _tabla_pequena() -> "dict[str, dict[str, str]]":
    """6 IDs elegibles con un empate (BASE y E1 comparten h=10) y un ID
    por debajo del base (BAJO, debe quedar excluido)."""
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
    """Mismo contenido que _tabla_pequena(), insertado en otro orden."""
    return dict(reversed(list(_tabla_pequena().items())))


# ---------------------------------------------------------------------------
# Conteo de secuencias validas.
# ---------------------------------------------------------------------------

def test_secuencias_validas_count_caso_real():
    """Ejemplo.csv real, columna 'Capacidad Mina', h_base='ID3', T=5: 9 IDs
    elegibles (ID3..ID11) con h todos distintos -> C(9+5-1,5) = C(13,5) =
    1287 secuencias no decrecientes."""
    tabla = cp.load_raw_table()
    secuencias = cp.secuencias_validas(tabla, T=5)
    assert len(secuencias) == 1287
    assert len(set(secuencias)) == 1287


def test_secuencias_validas_count_tabla_pequena():
    """6 elegibles con 5 VALORES distintos de h (BASE y E1 empatan en 10):
    C(6+3-1,3)=56 NO aplica aqui, esa formula es solo para valores todos
    distintos (como en el caso real de arriba). Con un empate, cada
    posicion que cae en el nivel empatado (10) puede tomar BASE o E1 de
    forma independiente, asi que hay que sumar por cuantas veces aparece
    ese nivel en la secuencia:

        k=0 (nivel 10 no aparece): C(4+3-1,3)   * 2^0 = C(6,3)*1 = 20*1 = 20
        k=1 (aparece una vez):      C(4+2-1,2)   * 2^1 = C(5,2)*2 = 10*2 = 20
        k=2 (aparece dos veces):    C(4+1-1,1)   * 2^2 = C(4,1)*4 =  4*4 = 16
        k=3 (aparece las 3 veces):  C(4+0-1,0)   * 2^3 = C(3,0)*8 =  1*8 =  8
        total = 20+20+16+8 = 64
    """
    tabla = _tabla_pequena()
    secuencias = cp.secuencias_validas(tabla, T=3, h_column="h", h_base="BASE")
    assert len(secuencias) == 64
    assert len(set(secuencias)) == 64
    assert all(len(s) == 3 for s in secuencias)


# ---------------------------------------------------------------------------
# La restriccion se revisa por VALOR: tabla con h fuera del orden de los
# IDs y con valores repetidos, verificada contra una enumeracion por
# fuerza bruta independiente (itertools.product, sin usar el DFS interno).
# ---------------------------------------------------------------------------

def test_secuencias_validas_coincide_con_fuerza_bruta():
    tabla = {
        "X1": {"h": "30"},
        "X2": {"h": "10"},
        "X3": {"h": "20"},
        "X4": {"h": "20"},  # empate con X3
        "X5": {"h": "40"},
        "BASE": {"h": "15"},
    }
    T = 3
    h_column, base_id = "h", "BASE"

    obtenidas = cp.secuencias_validas(tabla, T, h_column, base_id)
    assert len(obtenidas) == len(set(obtenidas))

    valores = {k: float(v[h_column]) for k, v in tabla.items()}
    h_base = valores[base_id]
    elegibles = [k for k in tabla if valores[k] >= h_base]

    esperadas = {
        combo
        for combo in itertools.product(elegibles, repeat=T)
        if all(valores[combo[i]] >= valores[combo[i - 1]] for i in range(1, T))
    }

    assert set(obtenidas) == esperadas


# ---------------------------------------------------------------------------
# Invarianza a permutar las filas de la tabla.
# ---------------------------------------------------------------------------

def test_secuencias_validas_invariante_a_permutar_filas():
    a = cp.secuencias_validas(_tabla_pequena(), T=3, h_column="h", h_base="BASE")
    b = cp.secuencias_validas(_tabla_pequena_permutada(), T=3, h_column="h", h_base="BASE")
    assert a == b


def test_eligible_pool_invariante_a_permutar_filas():
    elegibles_a, valores_a, h_base_a = cp.eligible_pool(_tabla_pequena(), "h", "BASE")
    elegibles_b, valores_b, h_base_b = cp.eligible_pool(_tabla_pequena_permutada(), "h", "BASE")
    assert elegibles_a == elegibles_b
    assert valores_a == valores_b
    assert h_base_a == h_base_b


def test_diseno_inicial_invariante_a_permutar_filas_misma_semilla():
    secs_a = cp.secuencias_validas(_tabla_pequena(), T=3, h_column="h", h_base="BASE")
    secs_b = cp.secuencias_validas(_tabla_pequena_permutada(), T=3, h_column="h", h_base="BASE")
    diseno_a = cp.diseno_inicial(secs_a, n0=4, seed=7)
    diseno_b = cp.diseno_inicial(secs_b, n0=4, seed=7)
    assert diseno_a == diseno_b


# ---------------------------------------------------------------------------
# vecinos_validos: vecinos validos, a exactamente una posicion de x.
# ---------------------------------------------------------------------------

def test_vecinos_validos_son_validos_y_a_una_posicion():
    tabla = _tabla_pequena()
    T = 3
    h_column, base_id = "h", "BASE"
    validas = set(cp.secuencias_validas(tabla, T, h_column, base_id))

    x = next(iter(validas))
    vecinos = cp.vecinos_validos(x, tabla, h_column, base_id)

    assert len(vecinos) > 0
    for v in vecinos:
        assert v in validas
        diferencias = sum(1 for a, b in zip(x, v) if a != b)
        assert diferencias == 1
    assert x not in vecinos


# ---------------------------------------------------------------------------
# diseno_inicial: primeros IDs distintos cuando es posible.
# ---------------------------------------------------------------------------

def test_diseno_inicial_primeros_ids_distintos():
    tabla = _tabla_pequena()
    secuencias = cp.secuencias_validas(tabla, T=3, h_column="h", h_base="BASE")
    diseno = cp.diseno_inicial(secuencias, n0=4, seed=0)

    assert len(diseno) == 4
    assert len(set(diseno)) == 4
    primeros_ids = [s[0] for s in diseno]
    assert len(set(primeros_ids)) == len(primeros_ids)  # 4 <= numero de elegibles (6): todos distintos


# ---------------------------------------------------------------------------
# CachedEvaluator: regla 4, ninguna secuencia se evalua dos veces.
# ---------------------------------------------------------------------------

def test_cached_evaluator_no_repite_llamadas():
    llamados = []

    def objective_fn(secuencia):
        llamados.append(secuencia)
        return sum(secuencia.count(x) for x in secuencia)

    evaluator = cp.CachedEvaluator(objective_fn)
    x = ("A", "B", "C")
    y = ("A", "B", "D")

    evaluator.evaluate(x)
    evaluator.evaluate(y)
    evaluator.evaluate(x)  # repetido: no debe volver a llamar objective_fn
    evaluator.evaluate(x)

    assert llamados == [x, y]
    assert evaluator.n_evaluaciones_reales == 2
    assert len(evaluator) == 2
