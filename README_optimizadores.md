# Optimizadores de secuencia de capacidad

Este documento cubre lo agregado sobre `capacity_problem.py` / los
`optimizer_*.py` existentes: infraestructura de validez por **valor** (no
por posición ni por el número del ID), dos métodos nuevos (BO-GP y COMBO),
versiones de SMAC y TPE que solo proponen secuencias válidas, las pruebas
automáticas, y el estudio piloto.

Ningún archivo existente (`optimizer_random_search.py`,
`optimizer_smac.py`, `optimizer_tpe.py`, `optimizer_mcs.py`, ni las
funciones `ids_pool`/`is_monotonic`/`repair_monotonic` de
`capacity_problem.py`) fue modificado. Todo lo de aquí es código nuevo,
agregado al final de `capacity_problem.py` o en archivos nuevos.

## Regla de validez (todo lo nuevo usa esta misma regla)

Una secuencia `x[0..T-1]` de IDs es válida si:

- `h(x[0]) >= h(h_base)` (el ID base, por defecto `ID3`, buscado por
  nombre), y
- `h(x[t]) >= h(x[t-1])` para `t = 1..T-1`,

comparando siempre por el **valor** de una columna `h_column` de
`Ejemplo.csv` (por defecto `"Capacidad Mina"`), nunca por la posición de
la fila ni por el número del ID. Si dos IDs comparten el mismo valor de
`h`, ambos órdenes entre ellos son válidos (la decisión es el ID, no el
valor de `h`). `--h-column` y `--base-id` son configurables en todos los
scripts nuevos.

Con la tabla real (`Ejemplo.csv`, `"Capacidad Mina"`, `h_base="ID3"`, 9
IDs elegibles con `h` todos distintos) y `T=5`, hay exactamente
`C(9+5-1, 5) = C(13, 5) = 1287` secuencias válidas.

## Infraestructura (Fase 1, en `capacity_problem.py`)

- `secuencias_validas(tabla, T, h_column, h_base)`: todas las secuencias
  válidas, por DFS en orden canónico.
- `vecinos_validos(x, tabla, h_column, h_base)`: secuencias válidas a
  exactamente una posición de `x`.
- `CachedEvaluator`: caché genérico (regla 4: ninguna secuencia se evalúa
  dos veces) sobre una función objetivo inyectable.
- `mineplan_objective(problem)`: la función objetivo real (corre
  mineplan), separada del algoritmo puro de cada optimizador.
- `diseno_inicial(secuencias, n0, seed)`: diseño inicial compartido por
  BO-GP y COMBO (n0 secuencias válidas al azar con primeros IDs
  distintos).
- `load_raw_table()`, `canonical_order()`, `h_values()`, `eligible_pool()`:
  lectura genérica de cualquier columna `h`, con orden canónico (por
  nombre del ID como texto) para que permutar las filas de `Ejemplo.csv`
  no cambie ningún resultado.

## Cómo correr cada optimizador

Todos aceptan `--trials`, `--seed`, `--scenarios`, `--h-column`,
`--base-id` (BO-GP/COMBO además `--n0`; COMBO además `--n-samples`,
`--warmup`, `--acq-mode`, `--n-starts`). Cada uno escribe su propio
`resultados_<nombre>.csv` (mismo formato que los optimizadores
existentes: `trial,ids,investment_per_period,number_of_scenarios,npv,computation_time`).

```bash
# Método propuesto: Optimización Bayesiana con Proceso Gaussiano
./run_test6_bo_gp.sh [TRIALS=20] [SEED=0] [N0=5]
python3 optimizer_bo_gp.py --trials 20 --seed 0 --n0 5

# COMBO (Oh et al. 2019), implementación propia (ver "Decisión COMBO" abajo)
./run_test7_combo.sh [TRIALS=20] [SEED=0] [N0=5] [ACQ_MODE=local]
python3 optimizer_combo.py --trials 20 --acq-mode exhaustive
# Ademas de resultados_combo.csv, guarda resultados_combo_betas.csv con la
# media posterior de cada beta_t por iteracion (que periodos influyen mas).

# SMAC y TPE que solo proponen secuencias validas (sin reparar ni descartar)
./run_test8_smac_factible.sh [TRIALS=20] [SEED=0]
./run_test9_tpe_factible.sh [TRIALS=20] [SEED=0]
```

## Decisión COMBO: implementación propia, no la oficial

Se revisó el repositorio oficial
([QUVA-Lab/COMBO](https://github.com/QUVA-Lab/COMBO)) antes de
implementar: fija `torch==1.6.0` + Python 3.7 (2019) en su
`requirements.txt`, y está estructurado como un *runner* de experimentos
propio (benchmarks fijos tipo branin/ising/contamination en su `main.py`),
no como una librería con una API para inyectar una función objetivo
externa. Este proyecto no usa PyTorch en ningún otro lado (solo
sklearn/numpy/scipy) y `torch==1.6.0` no instala en el Python moderno de
este entorno. Por dependencias desactualizadas/incompatibles y por no
tener una API enchufable, se implementó COMBO desde cero siguiendo el
paper (`optimizer_combo.py`).

Los puntos que el cuerpo principal del paper no deja explícitos (la
parametrización numérica exacta del horseshoe sobre los `beta_t`, y si la
amplitud `sigma_f^2`/el ruido `sigma_n^2` del GP también se infieren por
slice sampling) se resolvieron explícitamente con el usuario antes de
implementar — ver la docstring de `optimizer_combo.py` para el detalle
completo de esas decisiones y su justificación.

## SMAC y TPE factibles: cómo se cumple la regla 5 sin reparar ni descartar

- **SMAC** (`optimizer_smac_factible.py`): usa *forbidden clauses* de
  ConfigSpace (`ForbiddenEqualsClause` + `ForbiddenAndConjunction`) entre
  cada par de periodos **adyacentes**, prohibiendo por valor toda
  combinación que decrezca (basta con los pares adyacentes porque "no
  decreciente" es una propiedad transitiva). `ConfigurationSpace` muestrea
  por rechazo interno respetando esas cláusulas — en el diseño inicial, el
  modelo *surrogate* y la búsqueda de vecinos de SMAC — así que
  `smac.ask()` nunca entrega una secuencia inválida.
- **TPE/Optuna** (`optimizer_tpe_factible.py`): usa el espacio de búsqueda
  *define-by-run* de Optuna: las opciones de `trial.suggest_categorical()`
  para el periodo `t` se calculan en el momento, a partir del ID ya
  elegido en `t-1` dentro del mismo trial. Cada trial es válido por
  construcción, sin podar nada.

## Pruebas automáticas (Fase 5)

**No se ejecutaron en esta sesión** (a pedido explícito del usuario: solo
escribir código; correrlas y corregir lo que falle queda para quien las
ejecute). Usan funciones objetivo sintéticas, ninguna corre mineplan.

```bash
pytest test_capacity_problem.py test_bo_gp.py test_combo.py test_smac_tpe_factible.py -v
```

- `test_capacity_problem.py`: conteo de secuencias válidas (1287 en el
  caso real, 56 en una tabla chica de prueba), la restricción revisada por
  valor contra una enumeración por fuerza bruta independiente (tabla con
  `h` fuera del orden de los IDs y con empates), invarianza a permutar
  `Ejemplo.csv`, `vecinos_validos`, `diseno_inicial`, `CachedEvaluator`.
- `test_bo_gp.py` / `test_combo.py` / `test_smac_tpe_factible.py`:
  secuencias propuestas siempre válidas y sin repetir, invarianza a
  permutar `Ejemplo.csv` con la misma semilla, y que BO-GP y COMBO
  encuentran el óptimo de una función sintética (distancia de Hamming a un
  objetivo fijo) con menos evaluaciones en promedio que una búsqueda al
  azar, sobre varias semillas.
- `test_combo.py` además verifica que `dense_diffusion_kernel()` (forma
  cerrada para grafos completos) coincide con
  `scipy.linalg.expm(-beta*L)`, con `L` el laplaciano del grafo completo.

Si algo falla, revisar primero la causa más probable: versión de
scikit-learn/ConfigSpace/Optuna distinta a la usada al escribir el código
(ver versiones usadas en el entorno de desarrollo en el historial de
commits), o que el test de "encuentra el óptimo con menos evaluaciones"
sea ocasionalmente sensible al azar (es una comparación estadística sobre
pocas semillas: si falla una vez sin motivo aparente, subir
`seeds`/`budget` dentro del test antes de sospechar un bug).

## Estudio piloto (Fase 6)

Tres piezas, pensadas para NO depender de mineplan durante la comparación
en sí (que sí depende, indirectamente, de haber evaluado antes todo el
caso de prueba con mineplan):

1. **`evaluar_todas_las_secuencias.py`**: evalúa con mineplan las 1287
   secuencias válidas del caso de prueba, una fila de CSV por corrida,
   reanudable si se interrumpe (no repite una secuencia ya presente en el
   CSV de salida). **Advertencia: puede tomar muchas horas** (hasta 20 min
   por corrida × 1287). Probar primero con `--limit 2` o `--limit 3`.

   ```bash
   python3 evaluar_todas_las_secuencias.py --limit 3   # prueba rapida
   python3 evaluar_todas_las_secuencias.py             # corrida completa (horas); reanuda si se corta
   ```

2. **`tabla_objective.py`**: `TablaObjective.desde_csv(...)` carga ese CSV
   y se comporta como `objective_fn(secuencia) -> float`, consultando la
   tabla en vez de correr mineplan. Lanza `KeyError` si falta alguna
   secuencia (la tabla debe estar completa).

3. **`piloto_optimizadores.py`**: corre BO-GP, COMBO (local y exhaustive),
   SMAC factible, TPE factible y una línea base de búsqueda al azar (sobre
   `secuencias_validas()`, no es `optimizer_random_search.py`: ese no se
   toca y usa la semántica posicional antigua) con `--n-seeds` semillas
   (default 30) y `--budget` evaluaciones por corrida (default 20), todo
   en modo por tabla. Para cada semilla, BO-GP y COMBO llaman a
   `cp.diseno_inicial()` con los mismos argumentos — mismo diseño inicial
   garantizado automáticamente, verificado con un `assert` por semilla.

   Genera:
   - `piloto_distancias.csv`: `metodo,seed,mejor_valor,distancia_al_optimo,n_unicas`.
   - `piloto_curvas_convergencia.csv`: mejor valor visto vs. número de
     evaluación, media y desviación estándar sobre las semillas.
   - `piloto_wilcoxon.csv`: p-valores de Wilcoxon por cada par de métodos.
   - Por stdout: también la prueba de Friedman entre todos los métodos.

   ```bash
   python3 piloto_optimizadores.py --n-seeds 2 --budget 8      # prueba rapida
   python3 piloto_optimizadores.py --n-seeds 30 --budget 20    # piloto completo
   ```

**No se ejecutó nada de esto en esta sesión** (ni siquiera la versión
corta de prueba): depende de haber generado antes la tabla exhaustiva con
mineplan, y el usuario pidió explícitamente solo escribir código, no
correrlo.
