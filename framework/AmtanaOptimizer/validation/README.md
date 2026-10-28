# Validación de planes con Python y Gurobi

La configuración admite inversión, capacidad y costo de mina, horas de planta,
y tasas y costos de cada modo por período. Consultar
[formato, descuento y ejemplos](../configuration/PARAMETROS_POR_PERIODO.md).
Un valor único se repite en todo el horizonte; una lista debe tener exactamente
`NumberOfPeriods` valores. La inversión omitida se considera cero. El reporte
incluye estos parámetros y descuenta la inversión incluso en períodos vacíos.

`ValidadorFramework2.py` evalúa un **plan minero fijo** del optimizador C++ en uno
o varios escenarios geológicos. Lee la configuración actual del repositorio y
resuelve con Gurobi un programa lineal de procesamiento para cada período y
escenario. No modifica el plan ni ejecuta MineFlow.

Esta guía permite instalar la herramienta, comprobarla con un ejemplo incluido
y evaluar un plan propio. No es necesario compilar el C++ para ejecutar el
validador si ya se dispone del plan, la configuración y los escenarios.

## Contenido

- [Instalación y PyCharm](#instalación-y-pycharm)
- [Primera ejecución: ejemplo incluido](#primera-ejecución-ejemplo-incluido)
- [Preparar los archivos de entrada](#preparar-los-archivos-de-entrada)
- [Uso con el optimizador](#uso-con-el-optimizador)
- [Argumentos del programa](#argumentos-del-programa)
- [Compatibilidad y modelo](#compatibilidad-y-modelo)
- [Reporte y factibilidad](#reporte-y-factibilidad)
- [Problemas frecuentes](#problemas-frecuentes)
- [Pruebas](#pruebas)

## Instalación y PyCharm

Recomendado: Python **3.12**, con un entorno virtual propio. Las dependencias
están fijadas a Gurobi 12.0.1 y openpyxl 3.1.5 para reproducir la versión probada.
El componente C++ conserva sus dependencias y su compilación habituales.

Después de clonar `https://github.com/pheplex/AmtanaOptimizer.git` en PyCharm:

1. Abrir la raíz del repositorio como proyecto.
2. En la configuración del intérprete Python, crear un entorno `.venv` con Python 3.12.
3. Instalar `validation/requirements.txt` en ese entorno.
4. Comprobar que Gurobi encuentra una licencia válida para el usuario/equipo.
5. Ejecutar el ejemplo pequeño de abajo antes de usar los datos completos.

También se puede preparar desde PowerShell, situado en la raíz del repositorio:

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r validation/requirements.txt
.\.venv\Scripts\python.exe validation/ValidadorFramework2.py validation/examples/configuration.txt 0 1 --plan validation/examples/plan.txt --quiet
```

Si el lanzador `py` no está disponible, crear el entorno desde PyCharm. En Linux
o macOS, los comandos completos son:

```bash
python3.12 -m venv .venv
.venv/bin/python -m pip install -r validation/requirements.txt
.venv/bin/python validation/ValidadorFramework2.py validation/examples/configuration.txt 0 1 --plan validation/examples/plan.txt --quiet
```

En el resto de esta guía, `python` representa **el intérprete del entorno en el
que se instalaron las dependencias**. Si no está activado, sustituirlo por
`.\.venv\Scripts\python.exe` en Windows o `.venv/bin/python` en Linux/macOS.
No hace falta activar el entorno cuando se usa directamente esa ruta.

Descargar desde GitHub no instala Python ni transfiere una licencia de Gurobi.
En el mismo equipo se puede reutilizar la licencia existente si corresponde al
usuario que ejecuta Python. Otro equipo requiere configurar su propia licencia
según el tipo disponible. No incluir archivos `.lic`, credenciales ni `.venv` en Git.

Referencias: [intérpretes de PyCharm](https://www.jetbrains.com/help/pycharm/configuring-python-interpreter.html)
y [instalación de Gurobi para Python](https://support.gurobi.com/hc/en-us/articles/360044290292-How-do-I-install-Gurobi-for-Python).

## Primera ejecución: ejemplo incluido

Desde la raíz del repositorio, después de instalar las dependencias:

```text
python validation/ValidadorFramework2.py validation/examples/configuration.txt 0 1 --plan validation/examples/plan.txt --quiet
```

El ejemplo usa un plan de tres bloques, tres metales, tres tipos de roca y tres
modos. No requiere preparar archivos propios. La consola debe mostrar:

```text
Scenario 00: NPV LP = 147.000000; incidencias = 0
Scenario 01: NPV LP = 87.000000; incidencias = 0
Excel: .../validation/results/validation_00-01.xlsx
```

Abrir `validation/results/validation_00-01.xlsx` y revisar la hoja **Resumen**.
Los NPV finales deben ser 147 y 87, y `PlanFeasible` debe ser verdadero en ambos
escenarios. La hoja **Cumulative NPV** contiene el gráfico. No es necesario
tener Microsoft Excel instalado para generar el archivo.

## Preparar los archivos de entrada

Para evaluar un plan propio se necesitan tres entradas relacionadas entre sí:

| Entrada | Ubicación habitual | Contenido |
| --- | --- | --- |
| Configuración | `configuration/configuration.txt` | El mismo archivo utilizado por el optimizador C++ actual. |
| Plan | `sensor/result.txt` | Bloques extraídos y período de extracción; lo genera el C++. |
| Escenarios | `mineblocks/escenario00.txt`, etc. | Tonelaje, contenidos metálicos y tipo de roca de cada bloque por escenario. |

La estructura habitual es:

```text
AmtanaOptimizer/
├── configuration/
│   └── configuration.txt
├── mineblocks/
│   ├── escenario00.txt
│   ├── escenario01.txt
│   └── ...
├── sensor/
│   └── result.txt
└── validation/
    ├── ValidadorFramework2.py
    ├── requirements.txt
    ├── examples/
    └── results/                 # Se crea al generar reportes
```

Los nombres de escenarios se construyen con la configuración y el índice:

```ini
FileNameBase=mineblocks/escenario
FileNameExtension=.txt
```

Así, el índice `0` corresponde a `mineblocks/escenario00.txt`, el `7` a
`mineblocks/escenario07.txt` y el `100` a `mineblocks/escenario100.txt`.

El plan tiene este formato, incluyendo el encabezado:

```csv
Period,XIndex,YIndex,ZIndex
0,1,1,1
0,2,1,1
```

Las coordenadas deben pertenecer al modelo y respetar las precedencias. Los
períodos van de `0` a `NumberOfPeriods - 1`; cada bloque aparece como máximo una vez.

Un escenario con **dos metales** puede contener esta línea, sin encabezado:

```csv
1,1,1,6300,15.2,0.8,2
```

Representa las coordenadas `(1,1,1)`, tonelaje de roca `6300`, contenidos de los
dos metales `15.2` y `0.8`, y tipo de roca `2`. Son contenidos metálicos, no leyes
que el programa convierta automáticamente. El número de columnas de metal debe
coincidir con `NumberOfMetals`, y el orden con `EffectiveMetalPrices` y
`Recoveries`. Usar punto decimal y unidades consistentes con la configuración.

Si se necesita un ejemplo completo de configuración, consultar
[la configuración del ejemplo](examples/configuration.txt) o
[la configuración del optimizador](../configuration/configuration.txt).

## Uso con el optimizador

El C++ produce `sensor/result.txt`. Con el entorno Python seleccionado:

```text
python validation/ValidadorFramework2.py configuration/configuration.txt 0 --quiet
python validation/ValidadorFramework2.py configuration/configuration.txt 0 19 --plan sensor/result.txt --out validation/results/plan_00-19.xlsx --quiet
```

El rango es inclusivo. Es posible evaluar escenarios distintos o adicionales a
los utilizados durante la optimización: `NumberOfScenarios` no limita el rango
solicitado. Todos los archivos solicitados deben existir; si falta uno, se aborta
sin generar un reporte parcial ni percentiles basados en una muestra incompleta.

Todas las rutas relativas —configuración, plan, `FileNameBase` y salida— se
resuelven desde la raíz del repositorio, **independientemente del directorio de
trabajo de PyCharm**. Para datos ubicados en otro proyecto, usar
`--project-root RUTA`; también se admiten rutas absolutas. El C++ interpreta sus
rutas desde su directorio de trabajo, por lo que debe ejecutarse desde la raíz
para compartir directamente esta configuración.

Configuración de ejecución en PyCharm:

- Script: `validation/ValidadorFramework2.py`.
- Parámetros: `configuration/configuration.txt 0 19 --quiet` (ajustar al rango disponible).
- Intérprete: el `.venv` creado en este repositorio.

La salida predeterminada es `validation/results/validation_NN-MM.xlsx`. Una
ejecución posterior con el mismo destino reemplaza ese reporte.

Por ejemplo, evaluar solo el escenario 7 genera `validation_07-07.xlsx`.
Para conservar resultados de distintos planes, asignar nombres diferentes
mediante `--out`. Los reportes en `validation/results/` están excluidos de Git;
permanecen en el equipo donde se ejecuta el programa.

## Argumentos del programa

```text
python validation/ValidadorFramework2.py CONFIGURACION INICIO [FIN] [--plan PLAN] [--out REPORTE.xlsx] [--project-root CARPETA] [--quiet]
```

Los corchetes indican argumentos opcionales; no se escriben en el comando.

| Argumento | Obligatorio | Significado / valor predeterminado |
| --- | --- | --- |
| `CONFIGURACION` | Sí | Ruta al archivo de configuración del C++. |
| `INICIO` | Sí | Índice entero de escenario, mayor o igual a cero. |
| `FIN` | No | Último índice, incluido. Si se omite, evalúa únicamente `INICIO`. |
| `--plan` | No | Plan que se evaluará; por defecto `sensor/result.txt`. |
| `--out` | No | Destino `.xlsx`; por defecto `validation/results/validation_NN-MM.xlsx`. |
| `--project-root` | No | Base para todas las rutas relativas de datos y salida; por defecto, raíz del repositorio que contiene el script. |
| `--quiet` | No | Oculta los mensajes internos de Gurobi; conserva el resumen por escenario. |
| `--help` | No | Muestra la ayuda, sin necesidad de iniciar Gurobi. |

Para trabajar con datos externos desde Windows, por ejemplo:

```powershell
python validation/ValidadorFramework2.py configuration/configuration.txt 50 59 --project-root "C:\Datos\Mi estudio" --plan sensor/plan_alternativo.txt --out resultados/validacion.xlsx --quiet
```

Ese comando lee configuración, escenarios y plan dentro de `C:\Datos\Mi estudio`
y escribe el reporte en `C:\Datos\Mi estudio\resultados\validacion.xlsx`.
La ubicación del script no cambia. Encerrar entre comillas las rutas con espacios.

## Compatibilidad y modelo

Se utiliza el formato actual `Clave=Valor`, con líneas vacías y comentarios `#`.
El orden de claves es libre; las líneas repetidas `OperationalMode=` conservan
su orden. Las configuraciones posicionales antiguas deben convertirse al formato
actual. El lector rechaza duplicados ambiguos y datos mal formados.

- `NumberOfMetals` y `EffectiveMetalPrices`: cualquier cantidad positiva de metales.
- `RockTypes`: nombres de roca en el orden de los pesos `w`.
- `OperationalMode`: nombre, tasa, costo, recuperaciones por metal y pesos por roca.
- Escenario: `X,Y,Z,RockTonnage,Metal_1,...,Metal_K[,RockType]`, sin encabezado.
- Plan: encabezado `Period,XIndex,YIndex,ZIndex`; períodos desde 0.
- Los bloques ausentes de un escenario usan los valores por defecto de la configuración.
- La columna de roca es opcional; si falta, se usa `DefaultRockType`.

Para cada bloque `b` y modo `m`, la variable continua `x[b,m]` es su fracción
procesada. La suma de sus fracciones no puede superar 1. La parte restante queda
sin procesar y su costo minero se paga igualmente.

El valor por bloque y modo es:

```text
(sum_j Metal[b,j] * Recovery[m,j] * Price[j]
 - RockTonnage[b] * ProcessingCost[m]) / (1 + AnnualDiscountRate)^period
```

La capacidad de planta suma `x[b,m] * DefaultRockTonnage / ProcessingRate[m]`.
Las cantidades procesadas por roca dentro de cada modo son proporcionales a sus
pesos `w`. Los pesos no necesitan sumar 1. Un peso cero impide procesar esa roca
bajo ese modo; si falta una roca de peso positivo, el modo queda inhabilitado.

Se mantienen las convenciones del C++ actual:

- Horas de planta y tonelaje procesado usan `DefaultRockTonnage`.
- Costos de minería y procesamiento usan el tonelaje real del bloque.
- El primer período tiene exponente de descuento 0.
- El metal reportado suma `fracción * contenido metálico`, antes de recuperación.
  La recuperación sí interviene en los ingresos del objetivo.
- NPV = valor óptimo de procesamiento menos costos mineros descontados.

**Interpretación:** Gurobi obtiene el óptimo del procesamiento del plan fijo;
el C++ utiliza una matheurística. No se exige igualdad de resultados entre los
dos métodos ni se certifica que el plan minero sea globalmente óptimo. Para el
mismo problema continuo y plan factible, el LP ofrece una referencia superior
del valor de procesamiento alcanzable por la matheurística. El C++ almacena
varios parámetros como `float`; Python usa doble precisión, por lo que también
pueden aparecer pequeñas diferencias numéricas. Con óptimos alternativos,
algunos indicadores pueden diferir aunque coincida el valor objetivo.

## Reporte y factibilidad

- **Resumen:** NPV y totales por escenario, P10/P50/P90 y `PlanFeasible`.
- **Resultados** (un escenario) o **Detalle** (varios): indicadores por período y totales.
- **Cumulative NPV:** curvas y gráfico nativo de Excel. P10/P50/P90 corresponden a
  escenarios seleccionados por NPV final, no a percentiles independientes en cada período.
- **Comprobaciones:** incidencias de precedencia y capacidad minera.
- **Metadatos:** entradas, rango, versiones y convenciones de cálculo.

P10/P50/P90 usan nearest-rank ascendente; P10 es el NPV bajo y P90 el alto.
Los empates se ordenan por índice y un escenario puede recibir varios percentiles.
El gráfico arranca en `(0, 0)` y su último punto coincide con el NPV final.

Las coordenadas duplicadas o fuera del modelo, períodos inválidos y entradas
mal formadas detienen la evaluación. Las precedencias verifican los nueve
vecinos de la capa superior dentro del modelo: deben extraerse antes o en el
mismo período. La capacidad minera se comprueba con el tonelaje **de cada
escenario evaluado**, con tolerancia relativa de `1e-6` (mínimo absoluto `1e-6`).
El C++ controla esa capacidad usando el tonelaje promedio de sus escenarios de
optimización; si los tonelajes varían, esta comprobación por escenario es más
exigente que la restricción promedio del C++.

Si hay incidencias, se genera el reporte para diagnóstico, `PlanFeasible` es
falso en los escenarios afectados y los NPV de esos escenarios no deben
interpretarse como resultados de planes factibles. `PlanFeasible` se limita a
las comprobaciones descritas, sin recalcular MineFlow/UPL.

Códigos de salida: `0` evaluación sin incidencias; `1` error de entrada,
dependencias, licencia o solver; `2` reporte generado con incidencias del plan.
Los errores de argumentos también devuelven `2`, pero muestran la ayuda o un
mensaje de uso y no generan un reporte. Revisar siempre el mensaje de consola.

## Problemas frecuentes

| Mensaje o situación | Qué revisar |
| --- | --- |
| `No module named gurobipy` o `openpyxl` | Instalar las dependencias con `python -m pip install -r validation/requirements.txt` usando el mismo intérprete con que se ejecutará el programa. |
| Funciona en la terminal, pero no en PyCharm | Seleccionar en la configuración de ejecución el intérprete `.venv` donde se instalaron las dependencias. |
| Error de licencia de Gurobi o `User name mismatch` | Verificar que la licencia corresponde al usuario y equipo que ejecutan Python; seguir la configuración oficial de Gurobi. Reinstalar el paquete no resuelve una licencia asociada a otro usuario. |
| Gurobi indica que el modelo supera el tamaño permitido por la licencia | Usar una licencia que admita el tamaño de los modelos reales. Que el ejemplo pequeño funcione no garantiza que la licencia permita cualquier tamaño. |
| No se encuentra la configuración o el plan | Revisar la ruta y la base definida por `--project-root`; los archivos de datos no se buscan automáticamente en la carpeta de trabajo de PyCharm. |
| `Faltan escenarios solicitados` | Comprobar `FileNameBase`, `FileNameExtension` y que existan todos los índices del rango. El programa no omite escenarios ausentes. |
| `Faltan claves del formato actual` | Utilizar la configuración del C++ actual; el formato posicional antiguo de `ValidacionUPL` no es compatible. |
| Encabezado incorrecto, coordenadas o roca inválidas | Comparar el archivo con los formatos de esta guía y con los límites y nombres de `configuration.txt`. |
| `Permission denied` al guardar el Excel | Cerrar el reporte si está abierto en Excel y comprobar permisos de escritura, o elegir otro destino con `--out`. |
| `incidencias` mayor que cero / `PlanFeasible` falso | Abrir **Comprobaciones** para ver la precedencia o capacidad incumplida. El reporte se genera para diagnóstico. |
| El NPV no coincide con el C++ | Comparar el mismo plan, configuración y escenarios. El C++ combina escenarios de optimización y usa una matheurística; este reporte evalúa cada escenario con un LP. Ver las convenciones de cálculo anteriores. |

Para comprobar el intérprete y las dependencias:

```text
python -c "import sys; print(sys.executable)"
python -m pip show gurobipy openpyxl
python validation/ValidadorFramework2.py --help
```

## Pruebas

```text
python -m unittest discover -s validation -v
```

Las pruebas del solver requieren las dependencias y una licencia de Gurobi.
Cubren tres metales/rocas/modos, mezcla de tres rocas, modos inhabilitados, pesos
cero, fracciones compartidas entre modos, descuento, masas no uniformes,
valores negativos, bloques por defecto, precedencias, capacidad, entradas
inválidas y reportes Excel.

El ejemplo incluido tiene soluciones analíticas: escenario 00, procesamiento
150 menos minería 3 = **NPV 147**; escenario 01, procesamiento 90 menos minería
3 = **NPV 87**. No necesita datos externos ni MineFlow.
