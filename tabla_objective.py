#!/usr/bin/env python3
"""Modo de evaluacion 'por tabla': en vez de correr mineplan, consulta un
CSV ya calculado por evaluar_todas_las_secuencias.py (columnas
secuencia;npv;computation_time). Pensado para el estudio piloto (Fase 6):
corre cada optimizador en segundos en vez de horas, usando el NPV real ya
precalculado de cada secuencia del caso de prueba.

Uso como modulo:
    from tabla_objective import TablaObjective
    tabla_obj = TablaObjective.desde_csv("resultados_todas_las_secuencias.csv")
    npv = tabla_obj(("ID3", "ID3", "ID5", "ID5", "ID7"))
    secuencia_optima, npv_optimo = tabla_obj.optimo()
"""
import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


@dataclass
class TablaObjective:
    """Callable objective_fn(secuencia) -> float, respaldado por una tabla
    ya calculada. Lanza KeyError si se consulta una secuencia que no esta
    en la tabla (el modo por tabla requiere que
    evaluar_todas_las_secuencias.py haya evaluado TODAS las secuencias
    validas del caso antes de usarse en el piloto)."""

    valores: "dict[tuple[str, ...], float]"

    @classmethod
    def desde_csv(cls, path: "Path | str") -> "TablaObjective":
        path = Path(path)
        valores: "dict[tuple[str, ...], float]" = {}
        with path.open(newline="") as f:
            reader = csv.DictReader(f)
            for fila in reader:
                secuencia = tuple(fila["secuencia"].split(";"))
                valores[secuencia] = float(fila["npv"])
        if not valores:
            raise ValueError(f"{path} no tiene filas: corre evaluar_todas_las_secuencias.py primero")
        return cls(valores=valores)

    def __call__(self, secuencia: Sequence[str]) -> float:
        clave = tuple(secuencia)
        if clave not in self.valores:
            raise KeyError(
                f"Secuencia {clave} no esta en la tabla precalculada; el modo por "
                "tabla requiere que evaluar_todas_las_secuencias.py haya evaluado "
                "TODAS las secuencias validas del caso (revisa --h-column/--base-id)."
            )
        return self.valores[clave]

    def optimo(self) -> "tuple[tuple[str, ...], float]":
        mejor_secuencia = max(self.valores, key=lambda s: self.valores[s])
        return mejor_secuencia, self.valores[mejor_secuencia]

    def __len__(self) -> int:
        return len(self.valores)
