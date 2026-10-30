#!/usr/bin/env python3
"""Calcula las metricas de estabilidad de decision del paper (Diaz,
Quelopana & Maleki, 2026), Ecuaciones (3) y (4):

    J(n)   = |M(xn) interseccion M(xn+1)| / |M(xn) union M(xn+1)|
    Phi(n) = sum_b |w_b(xn) - w_b(xn+1)| / (0.5 * sum_b [w_b(xn) + w_b(xn+1)])

donde M(x) es el conjunto de bloques minados (sin importar el periodo) y
w_b(x) = gamma^(periodo en que se mina b) si b esta minado, 0 si no;
gamma = 1/(1+tasa).

Usa los result.txt archivados en sensor_results/<ID>/scenarios_NN/ (uno
por cada corrida real de la prueba de IDs x escenarios), comparando
escenarios consecutivos n vs n+1 para cada ID.

Genera:
- resultados_estabilidad.csv con columnas ID, Escenarios comparados,
  J(n), Phi(n) (un CSV combinado para todos los IDs).
- 2 PNG por ID (Escenarios vs J(n), Escenarios vs Phi(n)).

Uso:
    python3 analizar_estabilidad.py
"""
import csv
import re
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

PROYECTO_DIR = Path(__file__).resolve().parent
SENSOR_RESULTS_DIR = PROYECTO_DIR / "sensor_results"
CONFIG_PATH = PROYECTO_DIR / "configuration.txt"
CSV_OUT = PROYECTO_DIR / "resultados_estabilidad.csv"

# Umbrales para decidir "estabilizado": el paper solo dice "J cercano a 1 y
# Phi chico" en forma cualitativa; estos valores (J>=0.95, Phi<=0.05) son la
# lectura numerica de eso que usamos aca, documentada para poder discutirla.
J_THRESHOLD = 0.90
PHI_THRESHOLD = 0.1


def find_nopt(escenarios_x: list[int], valores_j: list[float], valores_phi: list[float]) -> int | None:
    """Primer n desde el cual J>=J_THRESHOLD y Phi<=PHI_THRESHOLD se cumplen
    para TODAS las comparaciones siguientes (no un toque puntual: una vez
    que se alcanza, no se vuelve a violar hasta el final del rango
    probado). Devuelve None si no existe ese punto."""
    n_total = len(escenarios_x)
    for i in range(n_total):
        if all(
            valores_j[j] >= J_THRESHOLD and valores_phi[j] <= PHI_THRESHOLD
            for j in range(i, n_total)
        ):
            return escenarios_x[i]
    return None


def leer_tasa_descuento() -> float:
    texto = CONFIG_PATH.read_text()
    match = re.search(r"^AnnualDiscountRate=([\d.]+)", texto, re.MULTILINE)
    if not match:
        raise RuntimeError("No se encontro AnnualDiscountRate= en configuration.txt")
    return float(match.group(1))


def leer_plan(path: Path) -> dict[tuple[int, int, int], int]:
    """Lee result.txt y devuelve {(X,Y,Z): Period} para cada bloque minado."""
    plan: dict[tuple[int, int, int], int] = {}
    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        for fila in reader:
            bloque = (int(fila["XIndex"]), int(fila["YIndex"]), int(fila["ZIndex"]))
            plan[bloque] = int(fila["Period"])
    return plan


def jaccard(plan_a: dict, plan_b: dict) -> float:
    bloques_a = set(plan_a.keys())
    bloques_b = set(plan_b.keys())
    union = bloques_a | bloques_b
    if not union:
        return 1.0
    interseccion = bloques_a & bloques_b
    return len(interseccion) / len(union)


def phi(plan_a: dict, plan_b: dict, gamma: float) -> float:
    bloques = set(plan_a.keys()) | set(plan_b.keys())
    numerador = 0.0
    denominador = 0.0
    for b in bloques:
        w_a = gamma ** plan_a[b] if b in plan_a else 0.0
        w_b_ = gamma ** plan_b[b] if b in plan_b else 0.0
        numerador += abs(w_a - w_b_)
        denominador += w_a + w_b_
    denominador *= 0.5
    if denominador == 0:
        return 0.0
    return numerador / denominador


def main() -> int:
    gamma = 1.0 / (1.0 + leer_tasa_descuento())

    ids_disponibles = sorted(
        (d.name for d in SENSOR_RESULTS_DIR.iterdir() if d.is_dir()),
        key=lambda nombre: int(nombre.replace("ID", "")),
    )
    if not ids_disponibles:
        print(f"Error: no hay IDs en {SENSOR_RESULTS_DIR}", file=sys.stderr)
        return 1

    filas_csv = []  # (id, "n vs n+1", J, Phi)
    datos_por_id: dict[str, dict[str, list]] = {}

    for id_name in ids_disponibles:
        carpeta_id = SENSOR_RESULTS_DIR / id_name
        escenarios_disponibles = sorted(
            int(d.name.replace("scenarios_", "")) for d in carpeta_id.glob("scenarios_*") if d.is_dir()
        )

        escenarios_x = []
        valores_j = []
        valores_phi = []

        for n in escenarios_disponibles:
            n_sig = n + 1
            if n_sig not in escenarios_disponibles:
                continue  # no hay par consecutivo (ultimo escenario de la lista)

            archivo_n = carpeta_id / f"scenarios_{n:02d}" / "result.txt"
            archivo_n_sig = carpeta_id / f"scenarios_{n_sig:02d}" / "result.txt"
            if not archivo_n.exists() or not archivo_n_sig.exists():
                print(f"Advertencia: falta result.txt para {id_name} en {n} o {n_sig}, se omite esa comparacion", file=sys.stderr)
                continue

            plan_n = leer_plan(archivo_n)
            plan_n_sig = leer_plan(archivo_n_sig)

            j_valor = jaccard(plan_n, plan_n_sig)
            phi_valor = phi(plan_n, plan_n_sig, gamma)

            filas_csv.append((id_name, f"{n} vs {n_sig}", j_valor, phi_valor))
            escenarios_x.append(n)
            valores_j.append(j_valor)
            valores_phi.append(phi_valor)

        datos_por_id[id_name] = {"x": escenarios_x, "j": valores_j, "phi": valores_phi}
        print(f"{id_name}: {len(escenarios_x)} comparaciones calculadas", flush=True)

    # --- CSV combinado ---
    with CSV_OUT.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["ID", "Escenarios comparados", "J(n)", "Phi(n)"])
        writer.writerows(filas_csv)
    print(f"CSV guardado en {CSV_OUT}")

    # --- 2 graficos PNG por ID, con el punto de estabilizacion marcado ---
    for id_name, datos in datos_por_id.items():
        if not datos["x"]:
            continue

        nopt = find_nopt(datos["x"], datos["j"], datos["phi"])
        print(f"{id_name}: Nopt = {nopt if nopt is not None else 'NO SE ESTABILIZA'}", flush=True)

        fig, ax = plt.subplots(figsize=(9, 5), dpi=150)
        ax.plot(datos["x"], datos["j"], marker="o", markersize=3.5, linewidth=1.3, color="#2458a4")
        ax.axhline(J_THRESHOLD, color="#999999", linestyle="--", linewidth=1, label=f"umbral J≥{J_THRESHOLD}")
        if nopt is not None:
            ax.axvline(nopt, color="#2a9d55", linestyle=":", linewidth=1.6)
            ax.scatter([nopt], [datos["j"][datos["x"].index(nopt)]], color="#2a9d55", s=90, zorder=5, label=f"Nopt = {nopt}")
        else:
            ax.text(0.5, 0.5, "NO SE ESTABILIZA\nen el rango 1–50", transform=ax.transAxes,
                     ha="center", va="center", fontsize=14, fontweight="bold", color="#bb4b4f",
                     bbox=dict(boxstyle="round", facecolor="white", edgecolor="#bb4b4f"))
        ax.set_xlabel("Escenarios (n, comparado contra n+1)")
        ax.set_ylabel("J(n) — indice de Jaccard")
        ax.set_title(f"Estabilidad de bloques minados — {id_name}")
        ax.set_ylim(0, 1.05)
        ax.grid(True, alpha=0.3)
        ax.legend(loc="lower right", fontsize=9)
        fig.tight_layout()
        salida = PROYECTO_DIR / f"estabilidad_{id_name}_jaccard.png"
        fig.savefig(salida)
        plt.close(fig)
        print(f"Guardado {salida}")

        fig, ax = plt.subplots(figsize=(9, 5), dpi=150)
        ax.plot(datos["x"], datos["phi"], marker="o", markersize=3.5, linewidth=1.3, color="#bb4b4f")
        ax.axhline(PHI_THRESHOLD, color="#999999", linestyle="--", linewidth=1, label=f"umbral Φ≤{PHI_THRESHOLD}")
        if nopt is not None:
            ax.axvline(nopt, color="#2a9d55", linestyle=":", linewidth=1.6)
            ax.scatter([nopt], [datos["phi"][datos["x"].index(nopt)]], color="#2a9d55", s=90, zorder=5, label=f"Nopt = {nopt}")
        else:
            ax.text(0.5, 0.5, "NO SE ESTABILIZA\nen el rango 1–50", transform=ax.transAxes,
                     ha="center", va="center", fontsize=14, fontweight="bold", color="#bb4b4f",
                     bbox=dict(boxstyle="round", facecolor="white", edgecolor="#bb4b4f"))
        ax.set_xlabel("Escenarios (n, comparado contra n+1)")
        ax.set_ylabel("Phi(n) — indice de cambio de calendario")
        ax.set_title(f"Estabilidad de calendario (ponderado por descuento) — {id_name}")
        ax.set_ylim(bottom=0)
        ax.grid(True, alpha=0.3)
        ax.legend(loc="upper right", fontsize=9)
        fig.tight_layout()
        salida = PROYECTO_DIR / f"estabilidad_{id_name}_phi.png"
        fig.savefig(salida)
        plt.close(fig)
        print(f"Guardado {salida}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
