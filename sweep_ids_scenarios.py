#!/usr/bin/env python3
"""Prueba: para cada uno de los primeros N IDs de Ejemplo.csv (plan
ESTATICO: el mismo ID se aplica en los 5 periodos, sin crecer, y su
inversion completa se paga una sola vez en el periodo 0), barre
NumberOfScenarios de min a max.

N es un parametro (--num-ids), no esta fijo en el codigo: hoy se corre
con 5, y despues se puede repetir con --num-ids 11 para la tabla
completa sin tocar nada.

Guarda:
- resultados_ids_escenarios.csv con columnas Escenarios, ID, NPV,
  Tiempo de ejecucion (una fila por corrida real).
- Una copia de framework/sensor/{period_results.csv,result.txt} de CADA
  corrida, archivada en sensor_results/<ID>/scenarios_<NN>/, para que no
  se pierdan cuando la siguiente corrida los sobreescriba.

Uso:
    python3 sweep_ids_scenarios.py --num-ids 5 --min-scenarios 1 --max-scenarios 50
"""
import argparse
import csv
import shutil
import sys

import capacity_problem as cp
import executer


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Barre ID x NumberOfScenarios con un plan estatico (mismo ID en los 5 periodos)."
    )
    parser.add_argument(
        "--num-ids", type=int, default=5,
        help="Cuantos IDs probar, empezando desde ID1 (default: 5; usar 11 para la tabla completa).",
    )
    parser.add_argument("--min-scenarios", type=int, default=1, help="Primer valor de NumberOfScenarios (default: 1).")
    parser.add_argument("--max-scenarios", type=int, default=50, help="Ultimo valor de NumberOfScenarios, inclusive (default: 50).")
    args = parser.parse_args()

    tabla = cp.load_capacity_table()
    todos_los_ids = list(tabla.keys())
    if args.num_ids < 1 or args.num_ids > len(todos_los_ids):
        print(f"Error: --num-ids debe estar entre 1 y {len(todos_los_ids)}", file=sys.stderr)
        return 1
    ids_a_probar = todos_los_ids[: args.num_ids]

    csv_path = cp.PROYECTO_DIR / "resultados_ids_escenarios.csv"
    sensor_archive = cp.PROYECTO_DIR / "sensor_results"
    sensor_archive.mkdir(exist_ok=True)

    original_text = cp.CONFIG_PATH.read_text()

    with csv_path.open("w", newline="") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(["Escenarios", "ID", "NPV", "Tiempo de ejecucion"])

        try:
            total = len(ids_a_probar) * (args.max_scenarios - args.min_scenarios + 1)
            hecho = 0
            for id_name in ids_a_probar:
                for n in range(args.min_scenarios, args.max_scenarios + 1):
                    hecho += 1
                    nuevo_texto = cp.build_static_config_text(original_text, id_name, tabla, n)
                    cp.CONFIG_PATH.write_text(nuevo_texto)

                    print(f"=== [{hecho}/{total}] ID={id_name} NumberOfScenarios={n} ===", flush=True)
                    try:
                        npv, computation_time = executer.ejecutar()
                    except (FileNotFoundError, RuntimeError) as error:
                        print(f"Error en ID={id_name} NumberOfScenarios={n}: {error}", file=sys.stderr)
                        writer.writerow([n, id_name, "ERROR", str(error)])
                        csv_file.flush()
                        continue

                    writer.writerow([n, id_name, npv, computation_time])
                    csv_file.flush()
                    print(f"NPV={npv} ComputationTime={computation_time}s", flush=True)

                    # Archiva sensor/ de ESTA corrida antes de que la
                    # siguiente lo sobreescriba (executer.ejecutar() ya
                    # copio framework/sensor/* a sensor/ en la raiz).
                    destino = sensor_archive / id_name / f"scenarios_{n:02d}"
                    destino.mkdir(parents=True, exist_ok=True)
                    for nombre in ("period_results.csv", "result.txt"):
                        origen_archivo = executer.RESULTS_DIR / nombre
                        if origen_archivo.exists():
                            shutil.copy2(origen_archivo, destino / nombre)
        finally:
            # Restaura configuration.txt a su estado original sin importar
            # si el barrido termino completo o se interrumpio a medio camino.
            cp.CONFIG_PATH.write_text(original_text)

    print(f"Resultados guardados en {csv_path}")
    print(f"Archivos de sensor/ guardados en {sensor_archive}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
