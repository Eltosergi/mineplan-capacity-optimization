#!/bin/bash
# Chequeo previo antes de lanzar corridas reales largas (mineplan real, no
# sintetico): binarios correctos para Linux, datos de entrada presentes,
# dependencias de Python instaladas, nada corriendo ya, configuration.txt
# en su estado original. No ejecuta ningun optimizador ni mineplan.
#
# Uso:
#   ./preflight_check.sh
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

FALLOS=0

ok()   { printf "  [OK] %s\n" "$1"; }
fail() { printf "  [FALTA] %s\n" "$1"; FALLOS=$((FALLOS + 1)); }

echo "=== Binarios (deben ser ELF 64-bit, no Mach-O) ==="
for bin in framework/mineplan framework/MineFlow/mineflow; do
    if [ -x "$bin" ]; then
        tipo="$(file -b "$bin" 2>/dev/null)"
        echo "  $bin: $tipo"
        case "$tipo" in
            *ELF*) ok "$bin es ejecutable Linux" ;;
            *) fail "$bin existe pero NO parece ELF/Linux (recompila con ./build_binaries.sh)" ;;
        esac
    else
        fail "$bin no existe o no es ejecutable (correr ./build_binaries.sh)"
    fi
done

echo
echo "=== Datos de entrada ==="
[ -f Ejemplo.csv ] && ok "Ejemplo.csv" || fail "Ejemplo.csv"
[ -f configuration.txt ] && ok "configuration.txt" || fail "configuration.txt"

if [ -f configuration.txt ]; then
    base="$(grep -oP '^FileNameBase=\K.*' configuration.txt)"
    ext="$(grep -oP '^FileNameExtension=\K.*' configuration.txt)"
    n_escenarios="$(grep -oP '^NumberOfScenarios=\K.*' configuration.txt)"
    faltan_escenarios=0
    if [ -n "$base" ] && [ -n "$ext" ] && [ -n "$n_escenarios" ]; then
        for i in $(seq 0 $((n_escenarios - 1))); do
            # mineplan corre con cwd=framework/, asi que FileNameBase
            # (ej. "../escenarios/realizacion0") es relativo a esa carpeta;
            # concatenar literal y dejar que el "-f" resuelva el ".." funciona bien.
            # LoadAndSaveData.cpp inserta un "0" extra antes del indice
            # cuando ScenarioIndex < 10 (y nada mas alla de eso).
            if [ "$i" -lt 10 ]; then
                archivo="framework/${base}0${i}${ext}"
            else
                archivo="framework/${base}${i}${ext}"
            fi
            [ -f "$archivo" ] || faltan_escenarios=$((faltan_escenarios + 1))
        done
        if [ "$faltan_escenarios" -eq 0 ]; then
            ok "los $n_escenarios archivos de escenario que pide configuration.txt estan presentes"
        else
            fail "$faltan_escenarios de $n_escenarios archivos de escenario no se encontraron"
        fi
    else
        fail "no se pudo leer FileNameBase/FileNameExtension/NumberOfScenarios de configuration.txt"
    fi
fi

echo
echo "=== Dependencias de Python ==="
python3 - <<'EOF'
import importlib
import sys

paquetes = ["numpy", "scipy", "sklearn", "optuna", "ConfigSpace", "smac"]
fallos = 0
for nombre in paquetes:
    try:
        mod = importlib.import_module(nombre)
        version = getattr(mod, "__version__", "sin version")
        print(f"  [OK] {nombre} ({version})")
    except ImportError as e:
        print(f"  [FALTA] {nombre}: {e}")
        fallos += 1
sys.exit(1 if fallos else 0)
EOF
[ $? -ne 0 ] && FALLOS=$((FALLOS + 1))

echo
echo "=== Procesos ya corriendo (optimizer_* o mineplan) ==="
procesos="$(pgrep -fa 'optimizer_|mineplan' 2>/dev/null | grep -v preflight_check)"
if [ -n "$procesos" ]; then
    echo "$procesos"
    fail "hay procesos de un optimizador o de mineplan ya corriendo (arriba)"
else
    ok "ninguno corriendo"
fi

echo
echo "=== Estado de configuration.txt (no debe quedar modificado por una corrida anterior interrumpida) ==="
if git diff --quiet -- configuration.txt 2>/dev/null; then
    ok "configuration.txt sin cambios respecto al ultimo commit"
else
    fail "configuration.txt tiene cambios sin commitear: revisa si quedo modificado por una corrida anterior que se corto (deberia restaurarse solo al salir del 'with', pero si el proceso murio de forma abrupta puede no haber pasado)"
fi

echo
echo "=== Espacio en disco ==="
df -h . | tail -1

echo
if [ "$FALLOS" -eq 0 ]; then
    echo "Todo OK: deberia ser seguro lanzar las corridas reales."
    exit 0
else
    echo "$FALLOS chequeo(s) fallaron. Revisa arriba antes de lanzar nada largo."
    exit 1
fi
