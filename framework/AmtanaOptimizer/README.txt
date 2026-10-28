###################################################

Actualización: inversión, capacidades y costos variables por período.
Ver configuration/PARAMETROS_POR_PERIODO.md para el formato de listas,
compatibilidad, fórmulas de VAN y reportes. Investment se resta una vez por
período, aun sin actividad; recuperaciones y mezclas se mantienen constantes.
##                                               ##
##           MINE PLANNING SIMULATOR             ##
##           Author: Aldo Quelopana              ##
##         Email: aldo.quelopana@ucn.cl          ##
##                                               ##
###################################################

Programa de optimización de planeamiento minero a largo plazo a
cielo abierto bajo incertidumbre geológica. Combina la metaheurística
Variable Neighborhood Descent (VND) paralelizada con programación
lineal resuelta mediante descomposición de Dantzig-Wolfe, integrando
en un único marco la decisión estratégica (qué bloques extraer y
cuándo) y la operación de la planta metalúrgica (modos operacionales).


============================================================
1. DESCRIPCIÓN GENERAL
============================================================

El programa implementa el algoritmo descrito en:

    Quelopana, A., Navarra, A. (2024).
    "Incorporating Operational Modes into long-Term Open-Pit Mine
     Planning Under Geological Uncertainty: An Optimization Combining
     Variable Neighborhood Descent with Linear Programming."
    Mining, Metallurgy & Exploration, 41, 2769-2782.
    https://doi.org/10.1007/s42461-024-01052-9

La optimización se realiza en dos etapas integradas:

  Primera etapa (estratégica):
    Determina, para cada período del horizonte, qué bloques se
    extraen, maximizando el VAN del proyecto y respetando las
    restricciones de precedencia (estabilidad de taludes) y
    capacidad máxima de extracción. Se utiliza la metaheurística
    VND con tres vecindarios: Exchange, Shift-After y Shift-Before.

  Segunda etapa (operacional):
    Para cada combinación período/escenario, decide cómo procesar
    los bloques seleccionados a través de los distintos modos
    operacionales de la planta. El problema (un knapsack
    fraccionario con restricciones de mezcla) se resuelve con la
    matheurística greedy descrita en:

        Leiva, M., Lespay, H., Quelopana, A., Navarra, A. (2025).
        "Optimization in Mineral Processing: A Novel Matheuristic
         for a Variant of the Knapsack Problem."
        Minerals, 15(4), 427.
        https://doi.org/10.3390/min15040427

    generalizada a N tipos de roca, M modos operacionales y K
    metales. Esta matheurística reemplaza a la implementación
    anterior basada en descomposición de Dantzig-Wolfe con
    reoptimización por Simplex Dual: en cada evaluación reconstruye
    y resuelve el problema del período afectado desde cero
    (O(n log n)), eliminando el estado matricial incremental.

El enfoque es de riesgo neutro: se genera un único plan minero
robusto frente a la incertidumbre geológica, evaluado sobre N
escenarios equiprobables.

Adicionalmente, antes de la optimización principal se calcula el
Ultimate Pit Limit (UPL) de cada escenario mediante el software
externo MineFlow, lo que reduce significativamente el espacio de
búsqueda del problema.


============================================================
2. REQUISITOS Y DEPENDENCIAS
============================================================

Software:
  - Compilador g++ con soporte para C++11 (o superior) y pthread.
  - Sistema operativo tipo Unix (probado en Linux).

Dependencia externa:
  - MineFlow: software de código abierto para el cálculo del
    Ultimate Pit Limit mediante el algoritmo pseudoflow.

      Deutsch, M., Dağdelen, K., Johnson, T. (2022).
      "An open-source program for efficiently computing ultimate
       pit limits: MineFlow."
      Natural Resources Research.

    El programa invoca MineFlow mediante una llamada al sistema
    con la siguiente ruta y formato (hardcodeado en el código):

        ../MineFlow/mineflow --regular MaxX MaxY MaxZ 45 \
            dataForUPL/scenario<N>.dat dataForUPL/UPLscenario<N>.txt

    Es decir, el binario "mineflow" debe estar ubicado en
    ../MineFlow/ relativo al directorio desde el cual se ejecuta
    mineplan. El ángulo de talud está fijado en 45 grados.


============================================================
3. ESTRUCTURA DE DIRECTORIOS ESPERADA
============================================================

El programa asume la siguiente estructura, relativa al directorio
desde el que se ejecuta:

    .
    ├── mineplan                      (binario compilado)
    ├── *.cpp, *.hpp                  (código fuente)
    ├── configuration/                (archivos de configuración)
    │   └── configuration.txt
    ├── mineblocks/                   (archivos de escenarios)
    │   ├── scenario_00.mod
    │   ├── scenario_01.mod
    │   └── ...
    ├── sensor/                       (salida del programa)
    │   └── result.txt
    └── dataForUPL/                   (archivos temporales de MineFlow)
        ├── scenario<N>.dat
        └── UPLscenario<N>.txt

    ../MineFlow/mineflow              (binario externo de MineFlow)

Las carpetas configuration/, mineblocks/, sensor/ y dataForUPL/
deben existir antes de ejecutar el programa. El nombre y ubicación
de los archivos de escenario es configurable (ver Sección 8).


============================================================
4. COMPILACIÓN
============================================================

Desde el directorio raíz del proyecto:

    g++ DataStructures.cpp LoadAndSaveData.cpp ProcessingOptimizer.cpp Optimization.cpp main.cpp -o mineplan -pthread -O3
    g++ -std=c++17 -O3 -pthread DataStructures.cpp LoadAndSaveData.cpp ProcessingOptimizer.cpp Optimization.cpp main.cpp -o mineplan

Esto genera el ejecutable "mineplan" en el directorio actual.


============================================================
5. EJECUCIÓN
============================================================

El programa recibe como único argumento la ruta al archivo de
configuración:

    ./mineplan configuration/configuration.txt

Si no se proporciona el argumento, el programa aborta con un
mensaje de error.


============================================================
6. ARCHIVO DE CONFIGURACIÓN
============================================================

El archivo de configuración define todos los parámetros del
problema. Desde la versión matheurística (06-2026) se lee con un
parser GENÉRICO de pares "Clave=Valor": el orden de las líneas NO
importa, con la única excepción de las líneas "OperationalMode="
(que se numeran según su orden de aparición). La línea se divide
en el PRIMER signo igual; los valores no pueden contener espacios.
Las líneas vacías y las que comienzan con '#' se ignoran
(comentarios). El programa valida la presencia de todas las claves
requeridas y la consistencia de los conteos (número de precios,
de tipos de roca, de modos, etc.) y aborta con un mensaje claro si
algo falta.

Las unidades de los parámetros numéricos las elige el usuario,
pero deben ser internamente consistentes. Ver la Sección 7 para
una explicación detallada de cómo se calcula el valor de un
bloque y de los grupos de unidades que deben coincidir.

Los parámetros se describen agrupados en seis bloques (la
agrupación es sólo expositiva; el orden real es libre):

------------------------------------------------------------
6.1 Performance
------------------------------------------------------------

  NumberOfThreads     Número de hilos de ejecución. Acepta un
                      entero positivo o el valor "all", que
                      utiliza todos los hilos disponibles del
                      hardware.
  WarmStart           "true" para cargar la solución inicial
                      desde sensor/result.txt; cualquier otro
                      valor (incluido "false") inicia desde la
                      solución vacía.

------------------------------------------------------------
6.2 File parameters
------------------------------------------------------------

  FileNameBase        Prefijo (incluyendo carpeta) de los archivos
                      de escenario.
  FileNameExtension   Extensión de los archivos de escenario,
                      incluyendo el punto (ej. ".mod").

------------------------------------------------------------
6.3 Orebody parameters
------------------------------------------------------------

  MinX, MaxX          Rango de coordenadas X del modelo de bloques.
  MinY, MaxY          Rango de coordenadas Y del modelo de bloques.
  MinZ, MaxZ          Rango de coordenadas Z del modelo de bloques.
                      Convención: Z=1 es el fondo del yacimiento,
                      Z=MaxZ es la superficie.
  DefaultRockTonnage  Cantidad de roca asignada por defecto a los
                      bloques no listados en los archivos de
                      escenario. Misma unidad que la columna
                      Tonelada de los archivos de escenario (ver
                      Sección 7).
  DefaultMetalTonnage Cantidad de metal asignada por defecto a los
                      bloques no listados en los archivos de
                      escenario (aplica tanto a Metal1 como a
                      Metal2; típicamente 0). Misma unidad que las
                      columnas Metal1 y Metal2, respectivamente.
  DefaultRockType     Tipo de roca asignado por defecto. Debe
                      coincidir con uno de los RockType<i>
                      declarados más abajo.
  NumberOfScenarios   Número de escenarios geológicos a procesar.

------------------------------------------------------------
6.4 Long-term parameters
------------------------------------------------------------

  NumberOfPeriods     Número de períodos del horizonte de
                      planificación. Cada período se asume de un
                      año de duración (ver Sección 7.3).
  AnnualDiscountRate  Tasa de descuento anual (decimal, no
                      porcentaje; ej. 0.08 representa 8%).
  MiningCapacity      Cantidad máxima de roca extraíble por
                      período. Misma unidad de masa que
                      RockTonnage y DefaultRockTonnage.
  MiningCost          Costo monetario de extracción por unidad de
                      masa de roca, expresado en $/[unidad de
                      roca]. Ver Sección 7 para consistencia de
                      unidades.

------------------------------------------------------------
6.5 Global Economic parameters
------------------------------------------------------------

  NumberOfMetals        Número K de metales considerados (K >= 1).
  EffectiveMetalPrices  Lista de K precios efectivos separados por
                        comas (sin espacios), uno por metal y en el
                        mismo orden que las columnas de metal de los
                        archivos de escenario. Cada precio se expresa
                        en $/[unidad del metal respectivo]; cada
                        metal puede usar su propia unidad, definida
                        por el usuario al construir los archivos de
                        escenario y consistente con su precio (ver
                        Sección 7). Ejemplo: EffectiveMetalPrices=1190,0

------------------------------------------------------------
6.6 Short-term parameters (planta de procesamiento)
------------------------------------------------------------

  ProcessingCapacity  Tiempo disponible de planta por período
                      (ej. 8075 horas/período). Misma unidad de
                      tiempo que el denominador de ProcessingRate
                      en cada modo operacional (ver Sección 7).
  NumberOfRockTypes   Número N de tipos de roca distintos.
  RockTypes           Lista de N nombres separados por comas (sin
                      espacios). El ORDEN de esta lista define el
                      orden en que se interpretan las listas "w" de
                      los modos operacionales. Los nombres no pueden
                      contener espacios ni comas.
                      Ejemplo: RockTypes=Diorite_porphyry,Silicified_breccia
  NumberOfOperationalModes  Número M de modos operacionales de la planta.
  OperationalMode     Una línea por modo (M líneas; se numeran según
                      su orden de aparición). Cada línea define los
                      campos del modo como pares Campo=Valor
                      separados por punto y coma (el orden de los
                      campos es libre):

      OperationalMode=Name=<n>;ProcessingRate=<r>;ProcessingCost=<c>;Recoveries=<lista>;w=<lista>

      donde:
        Name           - Etiqueta del modo (ej. "A", "B").
        ProcessingRate - Capacidad de procesamiento, expresada en
                         [unidad de roca]/[unidad de tiempo]. La
                         unidad de roca debe coincidir con
                         RockTonnage; la unidad de tiempo, con
                         ProcessingCapacity.
        ProcessingCost - Costo de procesamiento por unidad de masa
                         de roca, expresado en $/[unidad de roca].
        Recoveries     - Lista de K recuperaciones separadas por
                         comas (decimales entre 0 y 1), una por
                         metal, en el mismo orden que
                         EffectiveMetalPrices.
        w              - Lista de N fracciones de mezcla separadas
                         por comas, una por tipo de roca y en el
                         mismo orden que RockTypes. w[j] es la
                         fracción del tipo de roca j en la mezcla
                         alimentada al modo; usar 0 si el modo no
                         consume ese tipo de roca. Al menos un
                         valor debe ser positivo.

      Condición de habilitación ("por modo"): un modo es ejecutable
      en un período/escenario si TODOS sus tipos de roca con w > 0
      están presentes entre los bloques del período en ese
      escenario. Ya no se exige la presencia de todos los tipos de
      roca declarados: por ejemplo, un modo con w=1,0 puede operar
      en un período que sólo contiene roca del primer tipo.

------------------------------------------------------------
6.7 Ejemplo completo
------------------------------------------------------------

A continuación se presenta un archivo de configuración completo,
inspirado en el caso de estudio del paper de referencia (depósito
de oro de la franja de Maricunga, dos modos operacionales):

    NumberOfThreads=all
    ControlUPL=5
    WarmStart=false
    FileNameBase=mineblocks/scenario_
    FileNameExtension=.mod
    MinX=1
    MaxX=24
    MinY=1
    MaxY=24
    MinZ=1
    MaxZ=23
    DefaultRockTonnage=15375
    DefaultMetalTonnage=0
    DefaultRockType=Diorite_porphyry
    NumberOfScenarios=10
    NumberOfPeriods=6
    AnnualDiscountRate=0.08
    MiningCapacity=6500000
    MiningCost=20.5
    NumberOfMetals=1
    EffectiveMetalPrices=1190
    ProcessingCapacity=8075
    NumberOfRockTypes=2
    RockTypes=Diorite_porphyry,Silicified_breccia
    NumberOfOperationalModes=2
    OperationalMode=Name=A;ProcessingRate=250;ProcessingCost=21.4;Recoveries=0.83;w=0.65,0.35
    OperationalMode=Name=B;ProcessingRate=200;ProcessingCost=24.9;Recoveries=0.83;w=0.45,0.55

Las unidades implícitas en este ejemplo son:

    Roca               toneladas (RockTonnage en archivos de
                       escenario, DefaultRockTonnage=15375,
                       MiningCapacity=6.500.000 ton/período,
                       MiningCost=$20.5/ton, ProcessingCost en
                       cada modo en $/ton, ProcessingRate en
                       ton/hora).
    Tiempo de planta   horas (ProcessingRate en ton/hora,
                       ProcessingCapacity=8075 horas/período).
    Metal 1 (oro)      onzas (columna de metal en los archivos de
                       escenario expresada en oz,
                       EffectiveMetalPrices=$1190/oz). Con
                       NumberOfMetals=1, los archivos de escenario
                       tienen una sola columna de metal.

Nótese que la roca está en toneladas y el oro en onzas: son
unidades distintas pero internamente consistentes con sus
respectivos costos y precios. Ver Sección 7.


============================================================
7. CÁLCULO DEL VALOR DEL BLOQUE
============================================================

El aporte de un bloque al VAN del proyecto se compone de dos
términos: el costo de extracción (que aplica si se extrae el
bloque) y el valor del procesamiento (que aplica adicionalmente
si el bloque se procesa, y depende del modo operacional
escogido). Ambos se descuentan al período en que ocurren.

------------------------------------------------------------
7.1 Fórmulas
------------------------------------------------------------

Costo descontado de extracción del bloque en el período t:

    CostoExtracción(t) = (RockTonnage × MiningCost_t) / (1 + r)^t

Valor descontado de procesar el bloque en el modo m, período t:

    ValorProc(m, t) = ( MetalTonnage  × Recovery1_m × EffectiveMetalPrice1
                      + MetalTonnage2 × Recovery2_m × EffectiveMetalPrice2
                      - RockTonnage   × ProcessingCost_m,t
                      ) / (1 + r)^t

Aporte total del bloque al VAN, si se extrae y se procesa en el
modo m:

    AporteVAN(m, t) = ValorProc(m, t) - CostoExtracción(t)

Al VAN total de los bloques se resta SUM_t Investment_t / (1+r)^t.

donde:
    r                 = AnnualDiscountRate (de la configuración).
    t                 = período de extracción (0, 1, 2, ...,
                        NumberOfPeriods - 1).
    Recovery1_m       = recuperación de Metal 1 en el modo m
                        (índice 1 del OperationalMode<m>).
    Recovery2_m       = recuperación de Metal 2 en el modo m
                        (índice 2 del OperationalMode<m>).
    ProcessingCost_m  = costo de procesamiento por unidad de roca
                        en el modo m (índice 4 del
                        OperationalMode<m>).

La segunda etapa del algoritmo (programa lineal) determina, para
cada bloque y modo, qué fracción del bloque se procesa y con qué
modo, maximizando ValorProc sujeto a las restricciones de
capacidad de planta y de mezcla por tipo de roca.

------------------------------------------------------------
7.2 Consistencia de unidades
------------------------------------------------------------

Para que el resultado quede en unidades monetarias ($), las
unidades de los parámetros deben ser internamente consistentes.
El código no realiza ninguna conversión: la responsabilidad de
elegir unidades coherentes recae en el usuario.

Los parámetros se agrupan en cuatro conjuntos. DENTRO de cada
conjunto las unidades deben coincidir; ENTRE conjuntos pueden
ser distintas.

Conjunto Roca (todos en la misma unidad de masa, "MU"):

    RockTonnage              [MU]              archivos de escenario
    DefaultRockTonnage       [MU]              configuración
    MiningCapacity           [MU / período]    configuración
    MiningCost               [$ / MU]          configuración
    ProcessingCost (cada modo) [$ / MU]        OperationalMode índice 4
    ProcessingRate (cada modo) [MU / TU]       OperationalMode índice 3

Conjunto Tiempo de planta (la unidad de tiempo "TU" debe
coincidir entre estos dos parámetros):

    ProcessingRate (cada modo) [MU / TU]       OperationalMode índice 3
    ProcessingCapacity         [TU / período]  configuración

Conjunto Metal 1 (cualquier unidad "M1U", elegida por el usuario):

    MetalTonnage             [M1U]             archivos de escenario,
                                               columna 5
    EffectiveMetalPrice1     [$ / M1U]         configuración
    Recovery1 (cada modo)    adimensional      OperationalMode índice 1

Conjunto Metal 2 (cualquier unidad "M2U", independiente de M1U):

    MetalTonnage2            [M2U]             archivos de escenario,
                                               columna 6
    EffectiveMetalPrice2     [$ / M2U]         configuración
    Recovery2 (cada modo)    adimensional      OperationalMode índice 2

Ejemplos válidos de combinaciones de unidades:

  - Roca en toneladas, oro (Metal 1) en onzas, cobre (Metal 2)
    en libras: usar MiningCost en $/ton, EffectiveMetalPrice1 en
    $/oz y EffectiveMetalPrice2 en $/lb. Las columnas Metal1 y
    Metal2 de los archivos de escenario se llenan en oz y lb,
    respectivamente. El nombre "MetalTonnage" en el código es
    sólo etiqueta y no implica que el dato esté en toneladas.

  - Roca, Metal 1 y Metal 2 todos en toneladas: todos los precios
    y costos en $/ton.

------------------------------------------------------------
7.3 Supuesto sobre la duración del período
------------------------------------------------------------

El factor de descuento (1 + r)^t asume que t está expresado en
las mismas unidades de tiempo que la tasa r. Como la tasa se
declara como AnnualDiscountRate, el código asume implícitamente
que cada período dura UN AÑO.

No existe un parámetro PeriodLength en la configuración. Si se
desea trabajar con períodos de otra duración (semestres,
trimestres, etc.), el usuario debe ajustar manualmente la tasa
de descuento para que sea equivalente a la duración del período
elegida. Por ejemplo, para períodos semestrales con un descuento
anual del 8%, debe usarse AnnualDiscountRate=0.0392, equivalente
a (1.08)^0.5 - 1.

Análogamente, MiningCapacity y ProcessingCapacity deben
expresarse "por período" en la duración elegida (no
necesariamente "por año").


============================================================
8. ARCHIVOS DE ESCENARIO
============================================================

Cada archivo de escenario contiene los datos geológicos de un
escenario equiprobable.

Nomenclatura:
    El programa busca archivos cuyo nombre se construye como:

        <FileNameBase><NN><FileNameExtension>

    donde <NN> es el índice del escenario formateado a 2 dígitos
    para escenarios 0-9 (con cero a la izquierda) y como entero
    sin formato para escenarios 10 en adelante.

    Ejemplo: con FileNameBase="mineblocks/scenario_",
    FileNameExtension=".mod" y NumberOfScenarios=12, los archivos
    esperados son:

        mineblocks/scenario_00.mod
        mineblocks/scenario_01.mod
        ...
        mineblocks/scenario_09.mod
        mineblocks/scenario_10.mod
        mineblocks/scenario_11.mod

Formato:
    Archivo CSV (sin encabezado), una línea por bloque, con
    4 + K columnas obligatorias y una opcional, separadas por
    comas (,):

        X, Y, Z, Tonelada, Metal_1, ..., Metal_K[, Roca]

    donde:
        X, Y, Z   - Índices enteros del bloque. Deben estar
                    dentro de los rangos [MinX, MaxX], [MinY, MaxY],
                    [MinZ, MaxZ] declarados en la configuración.
                    Z=1 es el fondo, Z=MaxZ es la superficie.
        Tonelada  - Cantidad de roca del bloque, en la unidad
                    consistente con MiningCost, MiningCapacity y
                    ProcessingCost (ver Sección 7).
        Metal_j   - Cantidad del metal j contenido en el bloque
                    (K columnas, K=NumberOfMetals), en la unidad
                    consistente con el j-ésimo valor de
                    EffectiveMetalPrices y en el mismo orden.
        Roca      - (Opcional) Nombre del tipo de roca del bloque
                    EN ESTE ESCENARIO (la incertidumbre de dominios
                    geológicos permite que un mismo bloque tenga
                    tipos distintos en escenarios distintos). Debe
                    coincidir exactamente con uno de los nombres de
                    la lista RockTypes de la configuración. Si la
                    columna está ausente, el bloque recibe
                    DefaultRockType.

Bloques no listados:
    Si un bloque dentro del rango del modelo no aparece en el
    archivo de escenario, recibe los valores por defecto declarados
    en la configuración (DefaultRockTonnage, DefaultMetalTonnage,
    DefaultRockType).


============================================================
9. SALIDA DEL PROGRAMA
============================================================

Archivo de salida principal:
    sensor/result.txt

    Formato CSV con encabezado:

        Period,XIndex,YIndex,ZIndex
        0,3,5,7
        0,3,6,7
        ...

    Cada línea representa un bloque incluido en el plan minero
    optimizado. El campo Period indica en qué período se extraerá
    el bloque (valores entre 0 y NumberOfPeriods-1). Los bloques
    no incluidos en este archivo no son extraídos en el plan.

Salida por consola:
    Durante la ejecución, el programa imprime el progreso de cada
    fase (carga de configuración, carga de orebody, cálculo del
    UPL, vecindarios visitados durante el VND, etc.).

    Al finalizar, reporta:
        - Optimized Solution NPV: VAN del plan optimizado, en
          unidades monetarias ($), consistente con los costos y
          precios declarados en la configuración.
        - Computation Time: tiempo total de cómputo, en segundos.

Archivos temporales:
    Durante el cálculo del UPL, el programa genera archivos en
    dataForUPL/ (entradas y salidas de MineFlow). Estos pueden
    eliminarse después de la ejecución.


============================================================
10. NOTAS Y CONVENCIONES IMPORTANTES
============================================================

  - Convención de coordenadas Z: Z=1 corresponde al fondo del
    yacimiento; Z=MaxZ corresponde a la superficie. Esta
    convención coincide con la utilizada por MineFlow, por lo que
    los archivos de escenario se leen y escriben sin transformación.

  - El programa maneja un número arbitrario K de metales
    (NumberOfMetals). Las unidades de los metales son
    independientes entre sí (ver Sección 7); cada metal debe ser
    consistente con su precio en EffectiveMetalPrices y con su
    columna en los archivos de escenario.

  - WarmStart: si se activa (WarmStart=true), el archivo
    sensor/result.txt debe existir y contener una solución válida
    (típicamente, el resultado de una ejecución previa). Esto
    permite reanudar la optimización desde un punto avanzado.
    Si WarmStart=false (o no es exactamente "true"), la
    optimización parte desde la solución vacía: todos los bloques
    del UPL están inicialmente asignados al período ficticio
    NumberOfPeriods.

  - NumberOfThreads=all: el programa detecta automáticamente la
    cantidad de hilos de hardware disponibles. Si la detección
    falla (raro), se utiliza 1 hilo como respaldo.

  - El UPL se calcula con un ángulo de talud fijo de 45 grados.
    Modificar este valor requiere editar el código fuente
    (LoadAndSaveData.cpp).

  - Los nombres de tipos de roca deben coincidir EXACTAMENTE entre
    el archivo de configuración y los archivos de escenario
    (sensible a mayúsculas/minúsculas). El programa valida todos
    los tipos de roca del modelo al construir el optimizador y
    aborta con un mensaje que indica el bloque, el escenario y los
    nombres válidos si encuentra una discrepancia.

  - Estructuras eliminadas: las matrices del método simplex /
    Dantzig-Wolfe (GlobalBase, GlobalcB, GlobalColumnsA, GlobalA,
    Globalc, Globalb, GlobalB1, GlobalSubProblemA, GlobalSubProblemc,
    GlobalBlocksPendingToBeDeleted y el accesor GetB1b) ya no
    existen en DataStructures. Cualquier herramienta externa que
    dependiera de ellas (e.g. un validate.cpp antiguo) debe migrar
    a los acumuladores públicos del nuevo ProcessingOptimizer
    (ProcessingUnderMode, ProcessingPerRockAndMode, AmountOfMetal),
    que se llenan en cada Execute() y en la inicialización.

  - Corrección de signo (Exchange): la versión Dantzig-Wolfe
    anterior, en la rama Exchange de Test(), SUMABA la variación
    del costo minero en lugar de restarla (inconsistente con la
    rama Shift y con la actualización de ExpectedMiningCost de
    ExecuteExchange). La versión matheurística usa la fórmula
    -(CostoMineroEsperado + Delta) en ambas ramas, por lo que los
    valores objetivo durante los movimientos Exchange pueden
    diferir levemente de los de la versión anterior (la nueva
    contabilidad es la correcta y es verificada por
    test_consistency.cpp).

  - Solución inicial consistente: la pasada
    ProcessingOptimizer::InitializeGlobalProcessingState()
    (invocada desde main.cpp después de Validation) calcula el
    valor de procesamiento de TODOS los períodos de la solución
    inicial y deja NPV = -CostoMineroEsperado + GlobalOptimizedNPV.
    Esto corrige el comportamiento anterior con WarmStart, donde el
    NPV inicial sólo incluía los costos de minado y el valor de
    procesamiento de la solución precargada se ignoraba.

  - Los nombres de tipos de roca no pueden contener espacios. Se
    recomienda usar guiones bajos (ej. "Diorite_porphyry").

  - El código no realiza ninguna conversión de unidades. La
    consistencia entre las unidades de la configuración y de los
    archivos de escenario es responsabilidad del usuario (ver
    Sección 7).


============================================================
11. HISTORIAL DE CAMBIOS
============================================================

04-12-2020 -> En el algoritmo de VND se actualizan todos los lugares en donde aparece la variable "NumberOfPeriods", dado que en el programa original de Alessandro, a esa variable él le sumaba uno. Así que se le restó - 1 a este programa. No sólo se modificó el Optimizer para ello, sino que también el DonwstreamOptimizer. Se actualizó en varios lugares, pero principalmente en aquellos donde estaban las variables ImmediatePredecessors and Sucessors.
04-12-2020 -> Se agregan restricciones a los movimiento de VND, Shift-After y Shift-Before para que no excedan los límites del orebody entregados en el archivo de configuración.
07-01-2021 -> Se corrige la segunda parte del método "Evaluate" dentro del DownstreamOptimizer. Sucede que la variable "PortionOfMarginalRockThatIsProcessed" no está tomando el valor "0" cuando no se alcanza la capacidad máxima de procesamiento y no hay nada más que procesar porque el material restante se encuentra bajo la ley de corte.
25-01-2021 -> El algoritmo permite el uso de hilos (Threads). La cantidad de hilos se indica en el archivo de configuración (primer dato). Los valores van desde 1 hilo (ejecución normal del algoritmo) hacia N hilos.
25-01-2021 -> El algoritmo no necesita de una solución inicial. Se hizo una modificación para partir de la solución vacía.
11-03-2022 -> Se reemplaza el DownstreamOptimizer por el ProcessingOptimizer. Este segunda clase trabaja con Linear Programming.
22-04-2022 -> La clase ProcessingOptimizer realiza los cálculos utilizando la descomposición de Dantzig-Wolfe y Simplex Dual a fin de reoptimizar y no partir desde 0.
01-10-2022 -> Al momento de realizar las iteraciones del DZ y del dual, la condición de término es cambiada al multiplicar el error por 1000000. Esto evita que siga iterando con valores inferiores a 0.001.
03-11-2023 -> Se agrega el UPL al principio a fin de reducir el espacio de búsquedas. Para calcular el UPL se utiliza el software MineFlow.
29-04-2026 -> Cada bloque tiene su propio tipo de roca en cada escenario, lo que permite que incorporar incertidumbre en los dominios geológicos.
30-04-2026 -> Adaptaciones para hacer el programa más user friendly: Determinación automática de la cantidad de hilos, Activación de WarmStart, Z=1 es el fondo del yacimiento.
10-06-2026 -> Se reemplaza el ProcessingOptimizer basado en Dantzig-Wolfe + Simplex Dual por la matheurística greedy de Leiva et al. (2025, Minerals 15(427)), generalizada a N tipos de roca, M modos de operación y K metales. La interfaz Test/Execute del VND se conserva; cada llamada recalcula los períodos afectados desde cero. Se introduce la condición de habilitación "por modo" (un modo es ejecutable si todas sus rocas con w>0 están presentes). El archivo de configuración migra al formato generalizado de pares Clave=Valor con orden libre (NumberOfMetals, EffectiveMetalPrices, RockTypes, NumberOfOperationalModes y líneas OperationalMode con campos nombrados); los archivos de escenario pasan a tener K columnas de metal y columna de tipo de roca opcional. Se agrega la pasada InitializeGlobalProcessingState() que deja el NPV inicial consistente (corrige WarmStart) y se corrige el signo de la variación del costo minero en la rama Exchange de Test(). Se eliminan las estructuras simplex de DataStructures. Se agrega el arnés test_consistency.cpp.

