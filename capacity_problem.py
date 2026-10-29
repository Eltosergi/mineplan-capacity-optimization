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
import re
from dataclasses import dataclass
from decimal import Decimal
from pathlib import Path
from typing import Sequence

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
