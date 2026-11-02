"""Pruebas de optimizer_smac_factible.py y optimizer_tpe_factible.py con
funciones objetivo sinteticas (sin correr mineplan): secuencias propuestas
validas y sin repetir, e invarianza a permutar Ejemplo.csv.

Uso:
    pytest test_smac_tpe_factible.py -v
"""
import capacity_problem as cp
from optimizer_smac_factible import run_smac_factible
from optimizer_tpe_factible import run_tpe_factible

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


# ---------------------------------------------------------------------------
# SMAC factible.
# ---------------------------------------------------------------------------

def test_smac_factible_propuestas_validas_y_sin_repetir(tmp_path):
    tabla = _tabla_pequena()
    elegibles, valores, _h_base = cp.eligible_pool(tabla, H_COLUMN, BASE_ID)
    valid_set = set(cp.secuencias_validas(tabla, T, H_COLUMN, BASE_ID))

    llamados = []

    def objective_fn(secuencia):
        llamados.append(secuencia)
        return float(sum(valores[id_] for id_ in secuencia))

    evaluadas, valores_obj, mejor, mejor_val = run_smac_factible(
        elegibles=elegibles, valores=valores, T=T, objective_fn=objective_fn,
        trials=10, seed=0, output_directory=tmp_path / "smac3_output_test",
    )

    assert len(evaluadas) <= 10  # <=: si SMAC repropone una ya cacheada, no cuenta una segunda vez
    assert len(set(evaluadas)) == len(evaluadas)
    assert all(s in valid_set for s in evaluadas)
    assert llamados == evaluadas
    assert mejor_val == max(valores_obj)


def test_smac_factible_invariante_a_permutar_filas(tmp_path):
    def correr(tabla, sufijo):
        elegibles, valores, _h_base = cp.eligible_pool(tabla, H_COLUMN, BASE_ID)

        def objective_fn(secuencia):
            return float(sum(valores[id_] for id_ in secuencia))

        return run_smac_factible(
            elegibles=elegibles, valores=valores, T=T, objective_fn=objective_fn,
            trials=8, seed=3, output_directory=tmp_path / f"smac3_output_test_{sufijo}",
        )

    r1 = correr(_tabla_pequena(), "a")
    r2 = correr(_tabla_pequena_permutada(), "b")

    assert r1[0] == r2[0]
    assert r1[2] == r2[2]
    assert r1[3] == r2[3]


# ---------------------------------------------------------------------------
# TPE factible.
# ---------------------------------------------------------------------------

def test_tpe_factible_propuestas_validas_y_sin_repetir():
    tabla = _tabla_pequena()
    elegibles, valores, h_base_valor = cp.eligible_pool(tabla, H_COLUMN, BASE_ID)
    valid_set = set(cp.secuencias_validas(tabla, T, H_COLUMN, BASE_ID))

    llamados = []

    def objective_fn(secuencia):
        llamados.append(secuencia)
        return float(sum(valores[id_] for id_ in secuencia))

    evaluadas, valores_obj, mejor, mejor_val = run_tpe_factible(
        elegibles=elegibles, valores=valores, h_base_valor=h_base_valor, T=T,
        objective_fn=objective_fn, trials=10, seed=0,
    )

    assert len(evaluadas) <= 10
    assert len(set(evaluadas)) == len(evaluadas)
    assert all(s in valid_set for s in evaluadas)
    assert llamados == evaluadas
    assert mejor_val == max(valores_obj)


def test_tpe_factible_invariante_a_permutar_filas():
    def correr(tabla):
        elegibles, valores, h_base_valor = cp.eligible_pool(tabla, H_COLUMN, BASE_ID)

        def objective_fn(secuencia):
            return float(sum(valores[id_] for id_ in secuencia))

        return run_tpe_factible(
            elegibles=elegibles, valores=valores, h_base_valor=h_base_valor, T=T,
            objective_fn=objective_fn, trials=8, seed=3,
        )

    r1 = correr(_tabla_pequena())
    r2 = correr(_tabla_pequena_permutada())

    assert r1[0] == r2[0]
    assert r1[2] == r2[2]
    assert r1[3] == r2[3]
