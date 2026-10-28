#!/usr/bin/env python3
"""Corre el framework (mineplan) y entrega el resultado final como numeros.

El framework envia el NPV y el tiempo de computo por un pipe en memoria
(dos "double" binarios), no por texto ni por archivo. Este modulo solo
ejecuta y recibe esos numeros; quien los imprime es main.py.
"""
import os
import shutil
import struct
import subprocess
from decimal import Decimal
from pathlib import Path

PROYECTO_DIR = Path(__file__).resolve().parent
FRAMEWORK_DIR = PROYECTO_DIR / "framework"
MINEPLAN = FRAMEWORK_DIR / "mineplan"
DEFAULT_CONFIG = PROYECTO_DIR / "configuration.txt"
RESULTS_DIR = PROYECTO_DIR / "sensor"

# El framework envia estos dos valores como "double" binarios crudos
# (formato nativo C, little-endian en este Mac arm64), en ese orden.
RESULT_FORMAT = "<2d"
RESULT_SIZE = struct.calcsize(RESULT_FORMAT)


def read_exact(fd: int, size: int) -> bytes:
    """Lee exactamente `size` bytes del pipe (o menos si el framework cerro antes)."""
    chunks = []
    remaining = size
    while remaining > 0:
        chunk = os.read(fd, remaining)
        if not chunk:
            break
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def ejecutar(config_path: Path | str | None = None) -> tuple[Decimal, Decimal]:
    """Corre mineplan y devuelve (npv, tiempo_computo) recibidos por el pipe."""
    config_path = Path(config_path).resolve() if config_path else DEFAULT_CONFIG

    if not MINEPLAN.exists():
        raise FileNotFoundError(f"No se encontro el ejecutable {MINEPLAN}")
    if not config_path.exists():
        raise FileNotFoundError(f"No se encontro el archivo de configuracion {config_path}")

    # Pipe anonimo en memoria: el framework escribe el resultado directo en
    # este descriptor, sin pasar por disco. Se le pasa el numero de
    # descriptor por argv para que escriba ahi con write().
    read_fd, write_fd = os.pipe()
    os.set_inheritable(write_fd, True)

    proc = subprocess.Popen(
        [str(MINEPLAN), str(config_path), str(write_fd)],
        cwd=FRAMEWORK_DIR,
        pass_fds=(write_fd,),
    )
    os.close(write_fd)

    payload = read_exact(read_fd, RESULT_SIZE)
    os.close(read_fd)

    returncode = proc.wait()
    if returncode != 0:
        raise RuntimeError(f"mineplan termino con codigo {returncode}")

    RESULTS_DIR.mkdir(exist_ok=True)
    for f in (FRAMEWORK_DIR / "sensor").glob("*"):
        if f.is_file():
            shutil.copy2(f, RESULTS_DIR / f.name)

    if len(payload) != RESULT_SIZE:
        raise RuntimeError(
            f"El framework envio {len(payload)} de {RESULT_SIZE} bytes esperados por el pipe"
        )

    npv, computation_time = struct.unpack(RESULT_FORMAT, payload)

    # Decimal(str(x)) en vez de Decimal(x): toma los digitos que ya muestra
    # Python para ese double (la representacion mas corta que redondea al
    # mismo valor), en vez de arrastrar la cola binaria exacta del float
    # (que puede tener 50+ digitos de ruido). De ahi en adelante, toda
    # operacion con estos valores es aritmetica decimal exacta, no binaria.
    return Decimal(str(npv)), Decimal(str(computation_time))
