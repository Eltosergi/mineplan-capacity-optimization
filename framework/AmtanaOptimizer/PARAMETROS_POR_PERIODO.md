# Parámetros por período

Los siguientes campos aceptan un valor constante o una lista separada por comas
con exactamente `NumberOfPeriods` valores, desde el período **0** hasta **N−1**:

| Campo | Unidad | Regla |
|---|---|---|
| `Investment` | moneda total | No negativo; si se omite, cero |
| `MiningCapacity` | toneladas extraídas por período | No negativo |
| `MiningCost` | moneda por tonelada extraída | No negativo |
| `ProcessingCapacity` | horas de planta por período | No negativo |
| `ProcessingRate`, dentro de cada modo | toneladas por hora | Estrictamente positivo |
| `ProcessingCost`, dentro de cada modo | moneda por tonelada procesada | No negativo |

Las capacidades y tasas se indican como valores **totales vigentes**, no como
incrementos acumulables. La inversión es un dato fijo: no calcula ni modifica
automáticamente capacidades, tasas ni costos. Recuperaciones, mezclas y modos
de operación se mantienen constantes en el horizonte.

## Ejemplo para cinco períodos

Reemplazar estos campos en la configuración; conservar las demás claves y evitar
duplicarlas. Los valores son ilustrativos, no una recomendación económica.

```text
NumberOfPeriods=5
AnnualDiscountRate=0.08
Investment=5000000,0,2000000,0,0
MiningCapacity=1000000,1200000,1500000,1500000,1200000
MiningCost=25,25,23,23,24
ProcessingCapacity=4100,4500,5000,5000,4500
OperationalMode=Name=1;ProcessingRate=225,225,260,260,260;ProcessingCost=13,13,12,12,12;Recoveries=0.89,0.89;w=0.70,0.30
OperationalMode=Name=2;ProcessingRate=225,225,240,240,240;ProcessingCost=18.5,18.5,17,17,17;Recoveries=0.80,0.80;w=0.30,0.70
```

Un único valor se repite en todo el horizonte, también para `Investment`.
Para una inversión inicial única, usar por ejemplo `Investment=5000000,0,0,0,0`.
Una lista incompleta, un valor vacío, negativo, infinito o no numérico produce
un error. El punto es el separador decimal. Las capacidades pueden ser cero;
las tasas de los modos deben ser positivas incluso cuando no hay horas de planta.

## VAN y reportes

En cada período `t`:

```text
VAN_t = (Ingresos_t − CostoProcesamiento_t − CostoMina_t − Investment_t)
        / (1 + AnnualDiscountRate)^t
VAN = suma de VAN_t
```

La inversión se cobra aunque el período no tenga extracción ni procesamiento.
El período 0 no se descuenta. No se cobra inversión en el período ficticio de
bloques no extraídos. Al ser fija, la inversión cambia el VAN pero no los
beneficios relativos de los movimientos del plan.

El optimizador conserva `sensor/result.txt` y genera además
`sensor/period_results.csv`, con inversión nominal y descontada, capacidad y
tonelaje de mina, costo unitario y descontado de mina, horas disponibles y usadas,
toneladas procesadas, costo descontado de procesamiento, tasas y costos por modo,
VAN por período y acumulado. Las columnas `Mode1`, `Mode2`, etc. siguen el orden
de las líneas `OperationalMode` de la configuración. Los resultados del C++ son
promedios sobre los escenarios usados por el optimizador.

`DiscountedValueNoMiningCost` ya resta el costo de procesamiento. Por tanto,
`PeriodTotal = DiscountedValueNoMiningCost − DiscountedMiningCost − DiscountedInvestment`;
no se debe restar otra vez `DiscountedProcessingCost`.

El validador Python usa los mismos parámetros por período y los muestra en su
reporte Excel para cada escenario. Las tasas y costos unitarios no se suman en
los totales. La factibilidad minera se contrasta con la capacidad del período.
El validador sigue resolviendo un LP exacto para un plan fijo; el procesamiento
C++ sigue siendo una matheurística, por lo que no se exige igualdad general.

## Búsqueda y límite del rajo

Con parámetros variables, los movimientos de adelanto consideran destinos
anteriores no adyacentes. Esto permite aprovechar períodos rentables aunque haya
períodos intermedios cerrados o no rentables. Se respetan capacidades y
precedencias; los grupos trasladados siguen perteneciendo a un único período
de origen. Los parámetros constantes conservan los movimientos adyacentes.
La búsqueda sigue siendo heurística y no garantiza el óptimo global.

El límite preliminar del rajo (MineFlow) utiliza el menor costo de mina del
horizonte y el menor costo de procesamiento entre modos y períodos, conservando
su valoración optimista. Esos mínimos se usan sólo en el filtro preliminar; la
evaluación del plan usa los costos reales de cada período. La inversión fija no
se distribuye entre bloques para calcular ese filtro.

## Pruebas

Desde la raíz del repositorio, con las dependencias del validador instaladas:

```text
python -m unittest discover -s validation -v
```

Pruebas C++ sin dependencia de MineFlow, con un compilador compatible:

```sh
mkdir -p build
g++ -std=c++17 -O2 -pthread -I. DataStructures.cpp LoadAndSaveData.cpp ProcessingOptimizer.cpp Optimization.cpp validation/test_period_parameters.cpp -o build/test-periods
./build/test-periods
```

Cubren listas y escalares, entradas inválidas, inversiones sin actividad,
descuento, capacidades, cambios de tasa y costo, movimientos hipotéticos y
ejecutados, y optimización con un último período cerrado.
