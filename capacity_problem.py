#!/usr/bin/env python3
"""Infraestructura comun para los 4 optimizadores de capacidad.

El problema: en cada periodo t_n hay que elegir un ID de capacidad de
Ejemplo.csv (el set de paquetes "capacidad mina + planta + costos" que usa
el experto). Reglas del negocio:

  1. La capacidad de la mina nunca puede ser menor que la capacidad de
     procesamiento. Esto se cumple automaticamente porque cada fila de
     Ejemplo.csv ya es un paquete consistente (Capacidad Mina > Mineral
     procesable) construido por el experto.
  2. Solo se puede AGREGAR capacidad a lo largo del tiempo, nunca quitar.
     El punto de partida es ID3 (coincide exactamente con los valores que
     ya tenia configuration.txt antes de este cambio: 1.000.000 ton,
     costo 25, tasa 225 ton/h, costos 13/18.5). Por eso el pool de IDs
     seleccionables es ID3..ID11 (9 opciones), nunca algo por debajo.
  3. La inversion de cada periodo es la diferencia entre el Inversion()
     del ID elegido ese periodo y el Inversion() del ID vigente en el
     periodo anterior (o de ID3 para el primer periodo, que es el
     "estado cero" ya construido y pagado).

Cada uno de los 4 optimizadores (random search, SMAC, TPE/Optuna, MCS)
importa este modulo para no duplicar la logica de leer Ejemplo.csv,
escribir configuration.txt y llamar a executer.ejecutar().
"""
import csv
import random
import re
from dataclasses import dataclass
from decimal import Decimal
from pathlib import Path
from typing import Callable, Iterable, Sequence

import executer

PROYECTO_DIR = Path(__file__).resolve().parent
CAPACITY_TABLE_PATH = PROYECTO_DIR / "Ejemplo.csv"
CONFIG_PATH = PROYECTO_DIR / "configuration.txt"

BASELINE_ID = "ID3"  # Capacidad ya construida/pagada antes del primer periodo.


@dataclass(frozen=True)
class CapacityOption:
    id: str
    mining_capacity: Decimal
    mining_cost: Decimal
    processing_rate: Decimal
    processing_cost_a: Decimal
    processing_cost_b: Decimal
    inversion: Decimal


def _to_decimal(texto: str) -> Decimal:
    """Convierte numeros con coma decimal ('25,31') a Decimal (25.31)."""
    return Decimal(texto.strip().replace(",", "."))


def load_capacity_table() -> "dict[str, CapacityOption]":
    """Lee Ejemplo.csv y devuelve {ID: CapacityOption}, en orden ID1..ID11."""
    tabla: "dict[str, CapacityOption]" = {}
    with CAPACITY_TABLE_PATH.open(newline="", encoding="utf-8") as f:
        for fila in csv.DictReader(f):
            tabla[fila["Caso"]] = CapacityOption(
                id=fila["Caso"],
                mining_capacity=_to_decimal(fila["Capacidad Mina"]),
                mining_cost=_to_decimal(fila["Costo Mina"]),
                processing_rate=_to_decimal(fila["Toneladas por Hora Planta"]),
                processing_cost_a=_to_decimal(fila["Costo Procesamiento (Modo A)"]),
                processing_cost_b=_to_decimal(fila["Costo Procesamiento (Modo B)"]),
                inversion=_to_decimal(fila["Inversion"]),
            )
    return tabla


def ids_pool(tabla: "dict[str, CapacityOption]") -> list[str]:
    """IDs seleccionables: desde BASELINE_ID hasta el ultimo de la tabla."""
    todos = list(tabla.keys())
    return todos[todos.index(BASELINE_ID):]


def read_number_of_periods(config_text: str) -> int:
    match = re.search(r"^NumberOfPeriods=(\d+)", config_text, re.MULTILINE)
    if not match:
        raise RuntimeError("No se encontro NumberOfPeriods= en configuration.txt")
    return int(match.group(1))


def is_monotonic(id_sequence: Sequence[str], pool: Sequence[str]) -> bool:
    """True si los indices (en el pool) nunca bajan periodo a periodo."""
    indices = [pool.index(i) for i in id_sequence]
    return all(b >= a for a, b in zip(indices, indices[1:]))


def repair_monotonic(id_sequence: Sequence[str], pool: Sequence[str]) -> list[str]:
    """Corrige una secuencia para que nunca baje de capacidad: cada periodo
    queda en el maximo acumulado hasta ese punto (equivalente a "si
    proponen bajar, se queda en lo que ya se tenia"). Usado por SMAC y MCS,
    que a diferencia de TPE no descartan (prune) propuestas invalidas antes
    de evaluarlas, sino que las corrigen antes de gastar la corrida real."""
    indices = [pool.index(i) for i in id_sequence]
    reparado = []
    maximo = 0
    for idx in indices:
        maximo = max(maximo, idx)
        reparado.append(maximo)
    return [pool[i] for i in reparado]


def investments_for(id_sequence: Sequence[str], tabla: "dict[str, CapacityOption]") -> list[Decimal]:
    """Inversion incremental de cada periodo: Inversion(ID_n) - Inversion(ID_{n-1}),
    con ID_{-1} = BASELINE_ID (capacidad ya pagada, inversion marginal 0 si no se sube)."""
    inversiones = []
    anterior = tabla[BASELINE_ID].inversion
    for id_actual in id_sequence:
        actual = tabla[id_actual].inversion
        inversiones.append(actual - anterior)
        anterior = actual
    return inversiones


def _format_number(valor: Decimal) -> str:
    # Decimal normalizado, sin exponentes raros, con punto decimal
    # (configuration.txt espera notacion inglesa, no coma).
    return format(valor.normalize(), "f")


def _replace_scalar_line(texto: str, clave: str, valor: str) -> str:
    patron = re.compile(rf"^{clave}=.*$", re.MULTILINE)
    nuevo_texto, n = patron.subn(f"{clave}={valor}", texto)
    if n != 1:
        raise RuntimeError(f"Se esperaba exactamente una linea '{clave}=' en configuration.txt (se encontraron {n})")
    return nuevo_texto


def _replace_operational_mode(texto: str, nombre_modo: str, processing_rate_list: str, processing_cost_list: str) -> str:
    """Reemplaza ProcessingRate= y ProcessingCost= dentro de la linea
    OperationalMode=Name=<nombre_modo>;..., sin tocar Recoveries= ni w=."""
    patron = re.compile(
        rf"^(OperationalMode=Name={nombre_modo};ProcessingRate=)[^;]*(;ProcessingCost=)[^;]*(;.*)$",
        re.MULTILINE,
    )
    nuevo_texto, n = patron.subn(
        lambda m: f"{m.group(1)}{processing_rate_list}{m.group(2)}{processing_cost_list}{m.group(3)}",
        texto,
    )
    if n != 1:
        raise RuntimeError(f"Se esperaba exactamente una linea OperationalMode Name={nombre_modo} (se encontraron {n})")
    return nuevo_texto


def build_config_text(
    original_text: str,
    id_sequence: Sequence[str],
    tabla: "dict[str, CapacityOption]",
    number_of_scenarios: int | None = None,
) -> str:
    """Devuelve el texto de configuration.txt con los valores del ID elegido
    en cada periodo (MiningCapacity, MiningCost, ProcessingRate/Cost de
    ambos modos e Investment), sin tocar nada mas."""
    opciones = [tabla[i] for i in id_sequence]
    inversiones = investments_for(id_sequence, tabla)

    mining_capacity_list = ",".join(_format_number(o.mining_capacity) for o in opciones)
    mining_cost_list = ",".join(_format_number(o.mining_cost) for o in opciones)
    processing_rate_list = ",".join(_format_number(o.processing_rate) for o in opciones)
    processing_cost_a_list = ",".join(_format_number(o.processing_cost_a) for o in opciones)
    processing_cost_b_list = ",".join(_format_number(o.processing_cost_b) for o in opciones)
    investment_list = ",".join(_format_number(v) for v in inversiones)

    texto = original_text
    texto = _replace_scalar_line(texto, "MiningCapacity", mining_capacity_list)
    texto = _replace_scalar_line(texto, "MiningCost", mining_cost_list)
    texto = _replace_scalar_line(texto, "Investment", investment_list)
    texto = _replace_operational_mode(texto, "1", processing_rate_list, processing_cost_a_list)
    texto = _replace_operational_mode(texto, "2", processing_rate_list, processing_cost_b_list)
    if number_of_scenarios is not None:
        texto = _replace_scalar_line(texto, "NumberOfScenarios", str(number_of_scenarios))
    return texto


def build_static_config_text(
    original_text: str,
    id_name: str,
    tabla: "dict[str, CapacityOption]",
    number_of_scenarios: int | None = None,
) -> str:
    """Variante de build_config_text para un PLAN ESTATICO: un solo ID se
    aplica igual en los 5 periodos (sin crecer como en los 4 optimizadores),
    y su inversion completa se paga una sola vez, en el periodo 0 (0 en
    los periodos siguientes, porque no hay mas cambios de capacidad)."""
    opcion = tabla[id_name]
    n = read_number_of_periods(original_text)

    mining_capacity_list = ",".join([_format_number(opcion.mining_capacity)] * n)
    mining_cost_list = ",".join([_format_number(opcion.mining_cost)] * n)
    processing_rate_list = ",".join([_format_number(opcion.processing_rate)] * n)
    processing_cost_a_list = ",".join([_format_number(opcion.processing_cost_a)] * n)
    processing_cost_b_list = ",".join([_format_number(opcion.processing_cost_b)] * n)
    investment_list = ",".join([_format_number(opcion.inversion)] + ["0"] * (n - 1))

    texto = original_text
    texto = _replace_scalar_line(texto, "MiningCapacity", mining_capacity_list)
    texto = _replace_scalar_line(texto, "MiningCost", mining_cost_list)
    texto = _replace_scalar_line(texto, "Investment", investment_list)
    texto = _replace_operational_mode(texto, "1", processing_rate_list, processing_cost_a_list)
    texto = _replace_operational_mode(texto, "2", processing_rate_list, processing_cost_b_list)
    if number_of_scenarios is not None:
        texto = _replace_scalar_line(texto, "NumberOfScenarios", str(number_of_scenarios))
    return texto


class CapacitySearchProblem:
    """Envuelve lectura/escritura de configuration.txt y la llamada real a
    mineplan (via executer.ejecutar). Se usa como context manager para
    garantizar que configuration.txt vuelve a su estado original al salir,
    sea cual sea el motivo (fin normal, excepcion, Ctrl+C)."""

    def __init__(self, number_of_scenarios: int = 1):
        self.number_of_scenarios = number_of_scenarios
        self.tabla = load_capacity_table()
        self.pool = ids_pool(self.tabla)
        self._original_text: str | None = None
        self.number_of_periods: int = 0

    def __enter__(self) -> "CapacitySearchProblem":
        self._original_text = CONFIG_PATH.read_text()
        self.number_of_periods = read_number_of_periods(self._original_text)
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self._original_text is not None:
            CONFIG_PATH.write_text(self._original_text)

    def is_feasible(self, id_sequence: Sequence[str]) -> bool:
        return len(id_sequence) == self.number_of_periods and is_monotonic(id_sequence, self.pool)

    def evaluate(self, id_sequence: Sequence[str]) -> tuple[Decimal, Decimal]:
        """Escribe configuration.txt con este ID_sequence y corre mineplan
        de verdad. Devuelve (npv, tiempo_computo). Lanza ValueError si la
        secuencia no es factible (para que el caller decida que hacer sin
        gastar una corrida real)."""
        if self._original_text is None:
            raise RuntimeError("CapacitySearchProblem debe usarse dentro de un 'with'")
        if not self.is_feasible(id_sequence):
            raise ValueError(f"Secuencia de IDs no factible (debe ser no-decreciente, largo {self.number_of_periods}): {id_sequence}")

        nuevo_texto = build_config_text(self._original_text, id_sequence, self.tabla, self.number_of_scenarios)
        CONFIG_PATH.write_text(nuevo_texto)
        return executer.ejecutar()


def open_results_csv(path: Path):
    """Abre (modo 'w') el CSV de resultados y devuelve (file, writer) con
    el encabezado ya escrito. El caller es responsable de cerrarlo."""
    f = path.open("w", newline="")
    writer = csv.writer(f)
    writer.writerow(["trial", "ids", "investment_per_period", "number_of_scenarios", "npv", "computation_time"])
    return f, writer


def log_trial(writer, trial_idx: int, id_sequence: Sequence[str], tabla: "dict[str, CapacityOption]", number_of_scenarios: int, npv: Decimal, computation_time: Decimal) -> None:
    inversiones = investments_for(id_sequence, tabla)
    writer.writerow([
        trial_idx,
        ";".join(id_sequence),
        ";".join(_format_number(v) for v in inversiones),
        number_of_scenarios,
        npv,
        computation_time,
    ])


# ============================================================================
# Infraestructura nueva: validez por VALOR de una columna h (no por posicion
# en Ejemplo.csv ni por el numero del ID), usada por BO-GP, COMBO y las
# versiones factibles de SMAC/TPE. No modifica nada de lo anterior; ids_pool,
# is_monotonic, repair_monotonic y CapacitySearchProblem.evaluate() (que usa
# la semantica posicional antigua) siguen intactos para random search, SMAC,
# TPE y MCS existentes.
#
# Regla de validez (igual para todo lo de aqui abajo): una secuencia
# x[0..T-1] de IDs es valida si h(x[0]) >= h(h_base) y h(x[t]) >= h(x[t-1])
# para t=1..T-1, comparando siempre por VALOR de la columna h_column (nunca
# por posicion de fila ni por el numero del ID). h_base es el NOMBRE del ID
# base (por defecto BASELINE_ID = "ID3"), buscado por nombre en la tabla.
# ============================================================================

DEFAULT_H_COLUMN = "Capacidad Mina"


def load_raw_table() -> "dict[str, dict[str, str]]":
    """Lee Ejemplo.csv completo, como texto, indexado por 'Caso'. A
    diferencia de load_capacity_table(), no fija de antemano que columnas
    importan, asi que sirve para leer cualquier columna h por nombre."""
    tabla: "dict[str, dict[str, str]]" = {}
    with CAPACITY_TABLE_PATH.open(newline="", encoding="utf-8") as f:
        for fila in csv.DictReader(f):
            tabla[fila["Caso"]] = dict(fila)
    return tabla


def canonical_order(ids: Iterable[str]) -> list[str]:
    """Orden canonico interno: por nombre del ID como texto. No depende de
    la posicion de sus filas en Ejemplo.csv, asi que permutar esas filas no
    cambia este orden ni nada que dependa de el (desempates, muestreo,
    indices de codificacion one-hot en BO-GP/COMBO)."""
    return sorted(set(ids), key=str)


def h_values(tabla: "dict[str, dict[str, str]]", h_column: str) -> "dict[str, Decimal]":
    """{ID: Decimal(valor de h_column)} para cada fila de `tabla`. `tabla`
    tiene la forma de load_raw_table() (texto crudo por columna), para que
    tambien se le puedan pasar tablas sinteticas de prueba."""
    return {id_: _to_decimal(str(fila[h_column])) for id_, fila in tabla.items()}


def eligible_pool(
    tabla: "dict[str, dict[str, str]]",
    h_column: str = DEFAULT_H_COLUMN,
    h_base: str = BASELINE_ID,
) -> "tuple[list[str], dict[str, Decimal], Decimal]":
    """IDs elegibles (h >= h del ID base), en orden canonico, junto con el
    diccionario completo de valores h y el valor base h(h_base)."""
    valores = h_values(tabla, h_column)
    if h_base not in valores:
        raise KeyError(f"ID base {h_base!r} no esta en la tabla (columna {h_column!r})")
    h_base_valor = valores[h_base]
    elegibles = [id_ for id_ in canonical_order(valores.keys()) if valores[id_] >= h_base_valor]
    return elegibles, valores, h_base_valor


def secuencias_validas(
    tabla: "dict[str, dict[str, str]]",
    T: int,
    h_column: str = DEFAULT_H_COLUMN,
    h_base: str = BASELINE_ID,
) -> "list[tuple[str, ...]]":
    """Todas las secuencias validas de T IDs, por busqueda en profundidad
    sobre el orden canonico de los IDs elegibles. Si dos IDs comparten el
    mismo valor de h, ambos ordenes entre ellos son validos y se enumeran
    por separado (la decision es el ID, no el valor de h: regla 1 y 2).

    Para el caso real (Ejemplo.csv, columna "Capacidad Mina", h_base="ID3"):
    9 IDs elegibles (ID3..ID11) con h todos distintos, T=5 -> exactamente
    C(9+5-1, 5) = C(13, 5) = 1287 secuencias (formula estandar de
    combinaciones con repeticion / secuencias no decrecientes sobre un
    orden total de 9 valores)."""
    elegibles, valores, h_base_valor = eligible_pool(tabla, h_column, h_base)

    resultados: "list[tuple[str, ...]]" = []
    actual: list[str] = []

    def dfs(h_min: Decimal) -> None:
        if len(actual) == T:
            resultados.append(tuple(actual))
            return
        for id_ in elegibles:
            if valores[id_] >= h_min:
                actual.append(id_)
                dfs(valores[id_])
                actual.pop()

    dfs(h_base_valor)
    return resultados


def vecinos_validos(
    x: Sequence[str],
    tabla: "dict[str, dict[str, str]]",
    h_column: str = DEFAULT_H_COLUMN,
    h_base: str = BASELINE_ID,
) -> "list[tuple[str, ...]]":
    """Secuencias validas que difieren de x en exactamente una posicion. x
    debe ser en si misma una secuencia valida; no se revalida de punta a
    punta. Para la posicion t, el nuevo ID debe cumplir
    h(x[t-1]) <= h(nuevo) <= h(x[t+1]) (con h(x[-1]) = h(h_base) en el
    borde izquierdo, y sin cota superior en el borde derecho)."""
    elegibles, valores, h_base_valor = eligible_pool(tabla, h_column, h_base)
    T = len(x)
    vecinos: "list[tuple[str, ...]]" = []

    for t in range(T):
        h_min = valores[x[t - 1]] if t > 0 else h_base_valor
        h_max = valores[x[t + 1]] if t + 1 < T else None
        for id_ in elegibles:
            if id_ == x[t]:
                continue
            v = valores[id_]
            if v < h_min:
                continue
            if h_max is not None and v > h_max:
                continue
            nuevo = list(x)
            nuevo[t] = id_
            vecinos.append(tuple(nuevo))

    return vecinos


class CachedEvaluator:
    """Envoltorio con cache (regla 4: ninguna secuencia se evalua dos veces)
    alrededor de una funcion objetivo inyectable. objective_fn(secuencia)
    puede ser una corrida real de mineplan (ver mineplan_objective()) o una
    funcion sintetica de prueba: CachedEvaluator no sabe ni le importa cual.
    Esto separa el "algoritmo puro" de busqueda (que solo ve .evaluate()) de
    la parte que llama a mineplan, igual que hace run_mcs() en
    optimizer_mcs.py."""

    def __init__(self, objective_fn: Callable[[Sequence[str]], object]):
        self.objective_fn = objective_fn
        self.cache: "dict[tuple[str, ...], object]" = {}
        self.n_evaluaciones_reales = 0

    def evaluate(self, secuencia: Sequence[str]) -> object:
        clave = tuple(secuencia)
        if clave in self.cache:
            return self.cache[clave]
        resultado = self.objective_fn(clave)
        self.cache[clave] = resultado
        self.n_evaluaciones_reales += 1
        return resultado

    def evaluadas(self) -> "list[tuple[str, ...]]":
        return list(self.cache.keys())

    def __len__(self) -> int:
        return len(self.cache)


def mineplan_objective(problem: "CapacitySearchProblem") -> Callable[[Sequence[str]], "tuple[Decimal, Decimal]"]:
    """Funcion objetivo real: escribe configuration.txt para `secuencia` y
    corre mineplan, devolviendo (npv, computation_time). A proposito NO pasa
    por CapacitySearchProblem.is_feasible()/is_monotonic(), porque esas
    funciones usan la semantica POSICIONAL antigua sobre ids_pool() (regla 6:
    no se tocan). Las secuencias que llegan aqui ya fueron validadas por
    VALOR con secuencias_validas()/vecinos_validos() (reglas 1 y 2), asi que
    no corresponde revalidarlas con el criterio antiguo basado en posicion."""

    def objective_fn(secuencia: Sequence[str]) -> "tuple[Decimal, Decimal]":
        if problem._original_text is None:
            raise RuntimeError("CapacitySearchProblem debe usarse dentro de un 'with'")
        nuevo_texto = build_config_text(
            problem._original_text, secuencia, problem.tabla, problem.number_of_scenarios
        )
        CONFIG_PATH.write_text(nuevo_texto)
        return executer.ejecutar()

    return objective_fn


def diseno_inicial(secuencias: Sequence[Sequence[str]], n0: int, seed: int) -> "list[tuple[str, ...]]":
    """Diseno inicial compartido por BO-GP y COMBO: n0 secuencias validas
    elegidas al azar (semilla fija) con primeros IDs x[0] todos distintos
    entre si en lo posible (una por cada x[0] disponible, hasta agotar n0 o
    los primeros-IDs distintos; si n0 excede la cantidad de primeros-IDs
    distintos, se completa al azar entre las secuencias restantes).
    Determinista e invariante a permutar las filas de Ejemplo.csv, siempre
    que `secuencias` venga de secuencias_validas() (que ya usa orden
    canonico internamente)."""
    rng = random.Random(seed)

    por_primer_id: "dict[str, list[tuple[str, ...]]]" = {}
    for s in secuencias:
        por_primer_id.setdefault(s[0], []).append(tuple(s))

    primeros_ids = canonical_order(por_primer_id.keys())
    rng.shuffle(primeros_ids)

    elegidas: "list[tuple[str, ...]]" = []
    for primer_id in primeros_ids:
        if len(elegidas) >= n0:
            break
        candidatas = por_primer_id[primer_id]
        elegidas.append(candidatas[rng.randrange(len(candidatas))])

    if len(elegidas) < n0:
        ya_elegidas = set(elegidas)
        restantes = [tuple(s) for s in secuencias if tuple(s) not in ya_elegidas]
        rng.shuffle(restantes)
        for s in restantes:
            if len(elegidas) >= n0:
                break
            elegidas.append(s)

    return elegidas
