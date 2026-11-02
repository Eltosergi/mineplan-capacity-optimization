#!/usr/bin/env python3
"""COMBO: Combinatorial Bayesian Optimization usando el producto cartesiano
de grafos (Oh, Tomczak, Gavves & Welling, 2019, "Combinatorial Bayesian
Optimization using the Graph Cartesian Product", NeurIPS 32), sobre la
secuencia de capacidad.

Implementacion oficial vs. propia
----------------------------------
Se reviso el repositorio oficial de los autores
(https://github.com/QUVA-Lab/COMBO) antes de implementar: fija
torch==1.6.0 y Python 3.7 en su requirements.txt (2019), y esta
estructurado como un runner de experimentos propio (benchmarks fijos tipo
branin/ising/contamination en su main.py), no como una libreria con una
API para inyectar una funcion objetivo externa. Este repositorio no usa
PyTorch en ningun otro lado (solo sklearn/numpy/scipy, ver
optimizer_bo_gp.py), y torch 1.6.0 no instala en el Python moderno de este
entorno. Por esas dos razones (dependencias desactualizadas e
incompatibles, y una API no pensada para enchufar un objetivo propio) se
opto por implementar COMBO desde cero siguiendo el paper, en vez de
adaptar el codigo oficial.

Kernel (Secc. 2.2-2.3 del paper)
---------------------------------
Cada periodo t es un grafo completo K_m de los m IDs elegibles (orden
canonico de capacity_problem.canonical_order). El kernel de difusion en
forma cerrada para K_m (laplaciano L = m*I - J, con J la matriz de unos;
ver dense_diffusion_kernel() y su test en test_combo.py contra
scipy.linalg.expm(-beta*L)) es

    k_t(a,a) = [1 + (m-1)*exp(-beta_t*m)] / m
    k_t(a,b) = [1 - exp(-beta_t*m)] / m        (a != b)

y el kernel completo sobre el producto cartesiano de los T grafos es

    K(x, x') = sigma_f^2 * prod_{t=1}^{T} k_t(x[t], x'[t])

(Hadamard/producto de kernels PSD -> PSD por el teorema de Schur; ver
kernel_block()).

Hiperparametros (tratamiento bayesiano, decidido con el usuario)
------------------------------------------------------------------
El paper usa un prior Horseshoe sobre cada beta_t (Secc. 2.3) e inferencia
por slice sampling (Algoritmo 1, linea 8: "Fit GP ... with slice
sampling"), con 100 pasos de calentamiento y 10 muestras posteriores sin
thinning (texto bajo el Algoritmo 1). El cuerpo principal del paper remite
la parametrizacion numerica exacta del Horseshoe a Carvalho/Polson/Scott
(2009, 2010) sin darla explicitamente, y no detalla si la amplitud
sigma_f^2 y el ruido sigma_n^2 del GP tambien se infieren por slice
sampling. Como el material suplementario no estaba disponible, estos dos
puntos se resolvieron explicitamente con el usuario antes de implementar:

  - beta_t | lambda_t, tau ~ HalfNormal(0, (lambda_t*tau)^2)
    lambda_t ~ HalfCauchy(0, 1)                 (escala local)
    tau      ~ HalfCauchy(0, 1)                 (escala global, b0=1)
    (Horseshoe estandar truncado a positivo: beta_t >= 0 por construccion.)
  - sigma_f (amplitud) se infiere junto a los beta_t por slice sampling,
    con prior log-normal debilmente informativo: log(sigma_f) ~ N(0, 1)
    (el NPV ya esta normalizado a varianza 1 antes de ajustar, igual que
    en optimizer_bo_gp.py, asi que sigma_f de orden 1 es un prior
    razonable).
  - sigma_n^2 (ruido de observacion del GP) se fija chico (1e-6), NO se
    infiere: es solo un termino de estabilidad numerica para el Cholesky,
    no ruido real, porque la evaluacion de mineplan es determinista a
    NumberOfScenarios fijo (igual criterio que el WhiteKernel fijo de
    optimizer_bo_gp.py).

La inferencia se hace por slice sampling univariado (Neal, 2003, "Slice
Sampling", Annals of Statistics: procedimiento de "stepping out" +
"shrinkage"), barriendo cada hiperparametro por turno (beta_1..beta_T,
lambda_1..lambda_T, tau, sigma_f) en espacio logaritmico (todos son
positivos). La cadena es persistente entre iteraciones BO (no se reinicia
desde cero): cada iteracion hace `warmup` sweeps de recalentamiento sobre
los datos ya actualizados y despues guarda `n_samples` sweeps sin
thinning, tal como describe el paper.

Adquisicion
-----------
Expected Improvement promediado sobre las L muestras posteriores de
hiperparametros (se reusa expected_improvement() de optimizer_bo_gp.py).
Dos modos de optimizar la adquisicion (--acq-mode):
  - exhaustive: EI sobre TODAS las secuencias validas no evaluadas, igual
    que optimizer_bo_gp.py. Sirve para aislar el efecto del modelo
    (COMBO vs BO-GP) del efecto de la busqueda (exhaustiva vs local).
  - local (default, como el BFLS del paper): busqueda local por ascenso
    greedy, usando solo vecinos_validos(), partiendo de la mejor secuencia
    evaluada hasta ahora mas --n-starts secuencias validas no evaluadas al
    azar (el paper usa 20000 puntos aleatorios y toma los 20 mejores como
    arranques; aqui, al ser el espacio mucho mas chico y finito, se usan
    directamente --n-starts arranques aleatorios, default 20).

Mismo diseno inicial que optimizer_bo_gp.py (capacity_problem.diseno_inicial,
misma semilla). run_combo() es el algoritmo puro (no sabe nada de
mineplan/Decimal/CSV, solo ve objective_fn y, en modo local, vecinos_fn);
_cli_main() separa la parte que llama a mineplan, igual que run_mcs() en
optimizer_mcs.py y run_bo_gp() en optimizer_bo_gp.py.

Uso:
    python3 optimizer_combo.py --trials 20 --seed 0 --n0 5
    python3 optimizer_combo.py --trials 20 --acq-mode exhaustive
"""
import argparse
import csv
import sys
from dataclasses import dataclass
from typing import Callable, Sequence

import numpy as np

import capacity_problem as cp
from optimizer_bo_gp import _encoder, expected_improvement

NOISE_VARIANCE = 1e-6  # sigma_n^2 fijo: estabilidad numerica, no ruido real.
LOCAL_SCALE = 1.0  # escala b0 del Horseshoe (Half-Cauchy(0,1) para lambda_t y tau).
SIGMA_F_PRIOR_SCALE = 1.0  # s0 del prior log-normal de sigma_f.
SLICE_WIDTH = 2.0
SLICE_MAX_STEPS = 50


# ============================================================================
# Kernel de difusion en forma cerrada (grafo completo) y Gram matrix.
# ============================================================================

def dense_diffusion_kernel(m: int, beta: float) -> "np.ndarray":
    """Kernel de difusion exp(-beta*L) en forma cerrada para el grafo
    completo K_m, con L = m*I - J el laplaciano de K_m (J = matriz de
    unos). Derivacion: los autovalores de L son 0 (autovector 1/sqrt(m)*1,
    multiplicidad 1) y m (multiplicidad m-1, complemento ortogonal), asi
    que exp(-beta*L) = exp(-beta*m)*I + (1-exp(-beta*m))/m * J, lo que da

        k(a,a) = [1 + (m-1)*exp(-beta*m)] / m
        k(a,b) = [1 - exp(-beta*m)] / m        (a != b)

    Coincide con scipy.linalg.expm(-beta*L) (ver test_combo.py)."""
    fuera = (1.0 - np.exp(-beta * m)) / m
    K = np.full((m, m), fuera)
    np.fill_diagonal(K, fuera + np.exp(-beta * m))
    return K


def _diag_value(m: int, beta: float) -> float:
    """k(a,a) para el grafo completo K_m (igual para todo vertice a, por
    simetria de K_m): usado como K(x*,x*) en la prediccion del GP."""
    return (1.0 + (m - 1) * np.exp(-beta * m)) / m


def encode_indices(secuencias: Sequence[Sequence[str]], indice_de: "dict[str, int]", T: int) -> "np.ndarray":
    """Codifica cada secuencia de T IDs como un vector de T indices
    (posicion de cada ID dentro del orden canonico de elegibles)."""
    return np.array([[indice_de[s[t]] for t in range(T)] for s in secuencias], dtype=int)


def kernel_block(idx_a: "np.ndarray", idx_b: "np.ndarray", m: int, T: int, betas: "np.ndarray", sigma_f2: float) -> "np.ndarray":
    """Bloque K(a,b) = sigma_f2 * prod_t k_t(a[t],b[t];beta_t), para
    arrays de indices (n_a,T) y (n_b,T) ya codificados con
    encode_indices(). Caso cuadrado: idx_a is idx_b."""
    n_a, n_b = idx_a.shape[0], idx_b.shape[0]
    K = np.ones((n_a, n_b))
    for t in range(T):
        Kt = dense_diffusion_kernel(m, float(betas[t]))
        K *= Kt[np.ix_(idx_a[:, t], idx_b[:, t])]
    return sigma_f2 * K


def kernel_matrix(
    secuencias_a: Sequence[Sequence[str]],
    secuencias_b: "Sequence[Sequence[str]] | None",
    indice_de: "dict[str, int]",
    m: int,
    T: int,
    betas: "np.ndarray",
    sigma_f2: float,
) -> "np.ndarray":
    """API publica (testeable) sobre secuencias de IDs: K(x_i,x_j) =
    sigma_f2 * prod_t k_t(x_i[t],x_j[t];beta_t). Si secuencias_b es None,
    usa secuencias_a para ambos lados (Gram cuadrada)."""
    idx_a = encode_indices(secuencias_a, indice_de, T)
    idx_b = idx_a if secuencias_b is None else encode_indices(secuencias_b, indice_de, T)
    return kernel_block(idx_a, idx_b, m, T, betas, sigma_f2)


def _product_except(idx: "np.ndarray", m: int, betas: "np.ndarray", T: int, skip_t: int) -> "np.ndarray":
    """prod_{s != skip_t} K_s(idx,idx), sin el factor sigma_f2 (se aplica
    aparte). skip_t=-1 no excluye ningun periodo (producto completo)."""
    n = idx.shape[0]
    producto = np.ones((n, n))
    for s in range(T):
        if s == skip_t:
            continue
        Kt = dense_diffusion_kernel(m, float(betas[s]))
        producto *= Kt[np.ix_(idx[:, s], idx[:, s])]
    return producto


def _log_marginal_likelihood(K: "np.ndarray", y: "np.ndarray") -> float:
    """log N(y; 0, K + sigma_n^2*I), con reintentos de jitter creciente si
    el Cholesky falla (K puede quedar casi singular para betas extremos)."""
    n = K.shape[0]
    jitter = NOISE_VARIANCE
    L_chol = None
    for _ in range(5):
        try:
            L_chol = np.linalg.cholesky(K + jitter * np.eye(n))
            break
        except np.linalg.LinAlgError:
            jitter *= 10.0
    if L_chol is None:
        return -np.inf

    alpha = np.linalg.solve(L_chol.T, np.linalg.solve(L_chol, y))
    log_det = 2.0 * np.sum(np.log(np.diag(L_chol)))
    return float(-0.5 * y @ alpha - 0.5 * log_det - 0.5 * n * np.log(2 * np.pi))


# ============================================================================
# Priors (log-densidad hasta constante aditiva) y slice sampling.
# ============================================================================

def _log_half_cauchy(x: float, scale: float) -> float:
    return float(np.log(2.0 / np.pi) - np.log(scale) - np.log1p((x / scale) ** 2))


def _log_half_normal(x: float, scale: float) -> float:
    return float(0.5 * np.log(2.0 / np.pi) - np.log(scale) - (x ** 2) / (2.0 * scale ** 2))


def _log_lognormal(x: float, s0: float) -> float:
    logx = np.log(x)
    return float(-np.log(x) - np.log(s0) - 0.5 * np.log(2.0 * np.pi) - (logx ** 2) / (2.0 * s0 ** 2))


def _slice_sample_1d(u0: float, log_target: Callable[[float], float], rng: "np.random.Generator", w: float = SLICE_WIDTH, max_steps: int = SLICE_MAX_STEPS) -> float:
    """Slice sampling univariado (Neal, 2003): 'stepping out' (Fig. 3) +
    'shrinkage' (Fig. 5), sobre una variable u en espacio logaritmico de un
    parametro positivo (log_target ya debe incluir el jacobiano: para
    x=exp(u), log p(x)dx = log p(x) + u, en du)."""
    g0 = log_target(u0)
    logy = g0 + np.log(max(rng.uniform(0.0, 1.0), 1e-300))

    u = rng.uniform(0.0, w)
    L = u0 - u
    R = L + w

    j = int(np.floor(max_steps * rng.uniform(0.0, 1.0)))
    k = (max_steps - 1) - j
    while j > 0 and log_target(L) > logy:
        L -= w
        j -= 1
    while k > 0 and log_target(R) > logy:
        R += w
        k -= 1

    intentos = 0
    while True:
        intentos += 1
        u1 = L + rng.uniform(0.0, 1.0) * (R - L)
        if log_target(u1) >= logy or intentos > 200:
            return u1
        if u1 < u0:
            L = u1
        else:
            R = u1


@dataclass
class ComboState:
    betas: "np.ndarray"  # (T,)
    lams: "np.ndarray"  # (T,)
    tau: float
    sigma_f: float


def _gibbs_sweep(state: ComboState, idx: "np.ndarray", m: int, T: int, y: "np.ndarray", rng: "np.random.Generator") -> ComboState:
    """Un barrido de slice sampling sobre cada hiperparametro, en el orden
    beta_1..beta_T, lambda_1..lambda_T, tau, sigma_f, condicionando siempre
    en el valor mas reciente de los demas (Gibbs de barrido sistematico)."""
    betas = state.betas.copy()
    lams = state.lams.copy()
    tau = state.tau
    sigma_f = state.sigma_f

    for t in range(T):
        otros = _product_except(idx, m, betas, T, skip_t=t)

        def log_target_beta(u, t=t, otros=otros, lams=lams, tau=tau, sigma_f=sigma_f):
            beta_t = np.exp(u)
            Kt = dense_diffusion_kernel(m, beta_t)[np.ix_(idx[:, t], idx[:, t])]
            K = (sigma_f ** 2) * otros * Kt
            ll = _log_marginal_likelihood(K, y)
            lp = _log_half_normal(beta_t, scale=max(lams[t] * tau, 1e-12))
            return ll + lp + u

        u0 = np.log(max(betas[t], 1e-12))
        betas[t] = np.exp(_slice_sample_1d(u0, log_target_beta, rng))

    for t in range(T):
        def log_target_lam(u, t=t, betas=betas, tau=tau):
            lam_t = np.exp(u)
            lp = _log_half_normal(betas[t], scale=max(lam_t * tau, 1e-12)) + _log_half_cauchy(lam_t, scale=LOCAL_SCALE)
            return lp + u

        u0 = np.log(max(lams[t], 1e-12))
        lams[t] = np.exp(_slice_sample_1d(u0, log_target_lam, rng))

    def log_target_tau(u, betas=betas, lams=lams):
        tau_val = np.exp(u)
        lp = sum(_log_half_normal(betas[s], scale=max(lams[s] * tau_val, 1e-12)) for s in range(T))
        lp += _log_half_cauchy(tau_val, scale=LOCAL_SCALE)
        return lp + u

    u0 = np.log(max(tau, 1e-12))
    tau = float(np.exp(_slice_sample_1d(u0, log_target_tau, rng)))

    producto_total = _product_except(idx, m, betas, T, skip_t=-1)

    def log_target_sigma_f(u, producto_total=producto_total):
        sigma_f_val = np.exp(u)
        K = (sigma_f_val ** 2) * producto_total
        ll = _log_marginal_likelihood(K, y)
        lp = _log_lognormal(sigma_f_val, SIGMA_F_PRIOR_SCALE)
        return ll + lp + u

    u0 = np.log(max(sigma_f, 1e-12))
    sigma_f = float(np.exp(_slice_sample_1d(u0, log_target_sigma_f, rng)))

    return ComboState(betas=betas, lams=lams, tau=tau, sigma_f=sigma_f)


def _sample_posterior(
    state: ComboState, idx: "np.ndarray", m: int, T: int, y: "np.ndarray",
    rng: "np.random.Generator", warmup: int, n_samples: int,
) -> "tuple[ComboState, list[ComboState]]":
    """Continua la cadena persistente `state`: `warmup` sweeps descartados
    (recalentamiento sobre los datos ya actualizados) y despues
    `n_samples` sweeps guardados, sin thinning."""
    for _ in range(warmup):
        state = _gibbs_sweep(state, idx, m, T, y, rng)
    muestras = []
    for _ in range(n_samples):
        state = _gibbs_sweep(state, idx, m, T, y, rng)
        muestras.append(state)
    return state, muestras


# ============================================================================
# Prediccion del GP y adquisicion (EI promediada sobre muestras posteriores).
# ============================================================================

def _gp_predict_block(idx_train: "np.ndarray", idx_cand: "np.ndarray", m: int, T: int, state: ComboState, y: "np.ndarray") -> "tuple[np.ndarray, np.ndarray]":
    K_DD = kernel_block(idx_train, idx_train, m, T, state.betas, state.sigma_f ** 2)
    n = K_DD.shape[0]
    jitter = NOISE_VARIANCE
    L_chol = None
    for _ in range(5):
        try:
            L_chol = np.linalg.cholesky(K_DD + jitter * np.eye(n))
            break
        except np.linalg.LinAlgError:
            jitter *= 10.0
    if L_chol is None:
        n_cand = idx_cand.shape[0]
        return np.zeros(n_cand), np.zeros(n_cand)

    alpha = np.linalg.solve(L_chol.T, np.linalg.solve(L_chol, y))
    K_star = kernel_block(idx_train, idx_cand, m, T, state.betas, state.sigma_f ** 2)
    mu = K_star.T @ alpha

    v = np.linalg.solve(L_chol, K_star)
    k_star_star = (state.sigma_f ** 2) * float(np.prod([_diag_value(m, float(b)) for b in state.betas]))
    var = np.maximum(k_star_star - np.sum(v ** 2, axis=0), 1e-12)
    return mu, np.sqrt(var)


def _ei_promedio(muestras: "list[ComboState]", idx_train: "np.ndarray", idx_cand: "np.ndarray", m: int, T: int, y: "np.ndarray", f_best: float) -> "np.ndarray":
    """Expected Improvement promediada sobre las L muestras posteriores de
    hiperparametros (cada muestra aporta su propia media/varianza del GP)."""
    ei_total = np.zeros(idx_cand.shape[0])
    for state in muestras:
        mu, sigma = _gp_predict_block(idx_train, idx_cand, m, T, state, y)
        ei_total += expected_improvement(mu, sigma, f_best)
    return ei_total / len(muestras)


# ============================================================================
# Optimizacion de la adquisicion: exhaustiva o busqueda local (BFLS).
# ============================================================================

def _local_search_from(
    x0: "tuple[str, ...]", evaluadas_set: set, vecinos_fn: Callable[[Sequence[str]], "list[tuple[str, ...]]"],
    ei_fn: Callable[["list[tuple[str, ...]]"], "np.ndarray"],
) -> "tuple[tuple[str, ...], float]":
    """Ascenso greedy por vecinos_validos(): desde x0, se mueve al vecino
    no evaluado de mayor EI mientras eso mejore la EI del punto actual;
    para hasta cuando ningun vecino no evaluado supera al actual (BFLS del
    paper, adaptado a este espacio finito)."""
    actual = x0
    ei_actual = float(ei_fn([x0])[0]) if x0 not in evaluadas_set else -np.inf
    visitados = {x0}
    while True:
        candidatos = [v for v in vecinos_fn(actual) if v not in evaluadas_set and v not in visitados]
        if not candidatos:
            break
        eis = ei_fn(candidatos)
        j = int(np.argmax(eis))
        if eis[j] <= ei_actual:
            break
        actual = candidatos[j]
        ei_actual = float(eis[j])
        visitados.add(actual)
    return actual, ei_actual


def _local_search_acquisition(
    evaluadas_set: set,
    valid_sequences: "list[tuple[str, ...]]",
    vecinos_fn: Callable[[Sequence[str]], "list[tuple[str, ...]]"],
    indice_de: "dict[str, int]",
    m: int,
    T: int,
    idx_train: "np.ndarray",
    muestras: "list[ComboState]",
    y_norm: "np.ndarray",
    f_best: float,
    n_starts: int,
    mejor_evaluada: "tuple[str, ...]",
    rng: "np.random.Generator",
) -> "tuple[str, ...] | None":
    def ei_fn(secuencias):
        idx_cand = encode_indices(secuencias, indice_de, T)
        return _ei_promedio(muestras, idx_train, idx_cand, m, T, y_norm, f_best)

    no_evaluadas = [s for s in valid_sequences if s not in evaluadas_set]
    if not no_evaluadas:
        return None

    n_random = min(n_starts, len(no_evaluadas))
    indices_aleatorios = rng.choice(len(no_evaluadas), size=n_random, replace=False)
    inicios = [mejor_evaluada] + [no_evaluadas[i] for i in indices_aleatorios]

    mejor_candidato, mejor_ei = None, -np.inf
    for x0 in inicios:
        candidato, ei_candidato = _local_search_from(x0, evaluadas_set, vecinos_fn, ei_fn)
        if candidato not in evaluadas_set and ei_candidato > mejor_ei:
            mejor_candidato, mejor_ei = candidato, ei_candidato

    if mejor_candidato is None:
        # Ningun arranque encontro una secuencia no evaluada con EI valida
        # (todos sus vecinos ya estaban evaluados o visitados): respaldo
        # exhaustivo sobre las no evaluadas, para no bloquear la busqueda.
        ei_todas = ei_fn(no_evaluadas)
        mejor_candidato = no_evaluadas[int(np.argmax(ei_todas))]

    return mejor_candidato


# ============================================================================
# Algoritmo COMBO puro.
# ============================================================================

def run_combo(
    valid_sequences: Sequence[Sequence[str]],
    elegibles: Sequence[str],
    T: int,
    objective_fn: Callable[[Sequence[str]], float],
    n0: int,
    seed: int,
    budget: int,
    vecinos_fn: "Callable[[Sequence[str]], list[tuple[str, ...]]] | None" = None,
    n_samples: int = 10,
    warmup: int = 100,
    acq_mode: str = "local",
    n_starts: int = 20,
    on_beta_sample: "Callable[[int, np.ndarray], None] | None" = None,
) -> "tuple[list[tuple[str, ...]], list[float], tuple[str, ...], float]":
    """Algoritmo COMBO puro: no sabe nada de mineplan, Decimal, CSV ni de
    Ejemplo.csv; solo ve objective_fn(secuencia) -> float a MAXIMIZAR y, en
    --acq-mode local, vecinos_fn(secuencia) -> vecinos validos. Mismo
    diseno inicial que run_bo_gp() (cp.diseno_inicial, misma semilla).
    on_beta_sample(iteracion, beta_medio), si no es None, se llama en cada
    iteracion BO con la media posterior de cada beta_t (para loguear que
    periodos influyen mas en el NPV).

    Devuelve (secuencias_evaluadas_en_orden, valores, mejor_secuencia,
    mejor_valor)."""
    if acq_mode not in ("local", "exhaustive"):
        raise ValueError(f"acq_mode debe ser 'local' o 'exhaustive', no {acq_mode!r}")
    if acq_mode == "local" and vecinos_fn is None:
        raise ValueError("acq_mode='local' requiere vecinos_fn")

    valid_sequences = [tuple(s) for s in valid_sequences]
    indice_de, m = _encoder(elegibles)
    rng = np.random.default_rng(seed)

    evaluator = cp.CachedEvaluator(objective_fn)

    diseno = cp.diseno_inicial(valid_sequences, n0, seed)
    for s in diseno:
        if evaluator.n_evaluaciones_reales >= budget:
            break
        evaluator.evaluate(s)

    state = ComboState(betas=np.ones(T), lams=np.ones(T), tau=1.0, sigma_f=1.0)
    iteracion = 0

    while evaluator.n_evaluaciones_reales < budget:
        evaluadas = evaluator.evaluadas()
        y = np.array([evaluator.cache[s] for s in evaluadas], dtype=float)
        y_mean, y_std = y.mean(), y.std()
        if y_std < 1e-12:
            y_std = 1.0
        y_norm = (y - y_mean) / y_std

        idx_train = encode_indices(evaluadas, indice_de, T)

        state, muestras = _sample_posterior(state, idx_train, m, T, y_norm, rng, warmup, n_samples)
        iteracion += 1
        if on_beta_sample is not None:
            beta_medio = np.mean([s.betas for s in muestras], axis=0)
            on_beta_sample(iteracion, beta_medio)

        f_best = float(y_norm.max())
        evaluadas_set = set(evaluadas)

        if acq_mode == "exhaustive":
            pendientes = [s for s in valid_sequences if s not in evaluadas_set]
            if not pendientes:
                break
            idx_cand = encode_indices(pendientes, indice_de, T)
            ei = _ei_promedio(muestras, idx_train, idx_cand, m, T, y_norm, f_best)
            siguiente = pendientes[int(np.argmax(ei))]
        else:
            mejor_evaluada = evaluadas[int(np.argmax(y))]
            siguiente = _local_search_acquisition(
                evaluadas_set=evaluadas_set, valid_sequences=valid_sequences, vecinos_fn=vecinos_fn,
                indice_de=indice_de, m=m, T=T, idx_train=idx_train, muestras=muestras, y_norm=y_norm,
                f_best=f_best, n_starts=n_starts, mejor_evaluada=mejor_evaluada, rng=rng,
            )
            if siguiente is None:
                break

        evaluator.evaluate(siguiente)

    evaluadas = evaluator.evaluadas()
    valores = [evaluator.cache[s] for s in evaluadas]
    mejor_idx = max(range(len(valores)), key=lambda i: valores[i])
    return evaluadas, valores, evaluadas[mejor_idx], valores[mejor_idx]


# ============================================================================
# CLI: llamada real a mineplan.
# ============================================================================

def _cli_main() -> int:
    parser = argparse.ArgumentParser(
        description="COMBO (Optimizacion Bayesiana combinatoria, Oh et al. 2019) sobre la secuencia de capacidad."
    )
    parser.add_argument("--trials", type=int, default=20, help="Numero de corridas reales de mineplan, incluyendo el diseno inicial (default: 20).")
    parser.add_argument("--seed", type=int, default=0, help="Semilla para reproducibilidad (default: 0).")
    parser.add_argument("--scenarios", type=int, default=1, help="NumberOfScenarios a usar durante la busqueda (default: 1).")
    parser.add_argument("--n0", type=int, default=5, help="Tamano del diseno inicial, igual al de BO-GP (default: 5).")
    parser.add_argument(
        "--h-column", type=str, default=cp.DEFAULT_H_COLUMN,
        help=f"Columna de Ejemplo.csv usada como h para la restriccion de monotonia (default: {cp.DEFAULT_H_COLUMN!r}).",
    )
    parser.add_argument(
        "--base-id", type=str, default=cp.BASELINE_ID,
        help=f"ID base: h(x[1]) >= h(base_id) (default: {cp.BASELINE_ID!r}).",
    )
    parser.add_argument("--n-samples", type=int, default=10, help="Muestras posteriores L de hiperparametros tras el calentamiento, sin thinning (default: 10, igual que el paper).")
    parser.add_argument("--warmup", type=int, default=100, help="Pasos de calentamiento del slice sampling antes de cada tanda de muestras (default: 100, igual que el paper).")
    parser.add_argument(
        "--acq-mode", choices=["local", "exhaustive"], default="local",
        help="Optimizacion de la adquisicion: 'local' (busqueda local tipo BFLS, default) o 'exhaustive' (EI sobre todas las secuencias validas no evaluadas, igual que BO-GP).",
    )
    parser.add_argument("--n-starts", type=int, default=20, help="Arranques aleatorios adicionales para --acq-mode local, ademas de la mejor secuencia evaluada (default: 20, como en el paper).")
    args = parser.parse_args()

    if args.n0 > args.trials:
        print(f"Error: --n0 ({args.n0}) no puede ser mayor que --trials ({args.trials})", file=sys.stderr)
        return 1

    tabla_raw = cp.load_raw_table()
    csv_path = cp.PROYECTO_DIR / "resultados_combo.csv"
    betas_csv_path = cp.PROYECTO_DIR / "resultados_combo_betas.csv"
    csv_file, writer = cp.open_results_csv(csv_path)
    betas_file = betas_csv_path.open("w", newline="")
    betas_writer = csv.writer(betas_file)
    trial_counter = {"n": 0}

    try:
        with cp.CapacitySearchProblem(number_of_scenarios=args.scenarios) as problem:
            T = problem.number_of_periods
            elegibles, _valores, _h_base_valor = cp.eligible_pool(tabla_raw, args.h_column, args.base_id)
            print(f"IDs elegibles (h={args.h_column!r} >= h({args.base_id!r})): {elegibles}")
            print(f"Periodos (NumberOfPeriods): {T}")

            betas_writer.writerow(["iteracion"] + [f"beta_period_{t}" for t in range(T)])

            valid_sequences = cp.secuencias_validas(tabla_raw, T, args.h_column, args.base_id)
            print(f"Secuencias validas totales: {len(valid_sequences)}")

            def vecinos_fn(secuencia):
                return cp.vecinos_validos(secuencia, tabla_raw, args.h_column, args.base_id)

            mineplan_fn = cp.mineplan_objective(problem)

            def objective_fn(secuencia):
                trial_counter["n"] += 1
                print(f"[{trial_counter['n']}/{args.trials}] Probando {secuencia} ...", flush=True)
                npv, computation_time = mineplan_fn(secuencia)
                cp.log_trial(
                    writer, trial_counter["n"], secuencia, problem.tabla,
                    problem.number_of_scenarios, npv, computation_time,
                )
                csv_file.flush()
                print(f"    NPV={npv} ComputationTime={computation_time}s", flush=True)
                return float(npv)

            def on_beta_sample(iteracion, beta_medio):
                betas_writer.writerow([iteracion] + [f"{b:.6f}" for b in beta_medio])
                betas_file.flush()

            evaluadas, valores, mejor_secuencia, mejor_npv = run_combo(
                valid_sequences=valid_sequences,
                elegibles=elegibles,
                T=T,
                objective_fn=objective_fn,
                n0=args.n0,
                seed=args.seed,
                budget=args.trials,
                vecinos_fn=vecinos_fn,
                n_samples=args.n_samples,
                warmup=args.warmup,
                acq_mode=args.acq_mode,
                n_starts=args.n_starts,
                on_beta_sample=on_beta_sample,
            )
    finally:
        csv_file.close()
        betas_file.close()

    print("\n=== Mejor resultado (COMBO) ===")
    print(f"Secuencia: {mejor_secuencia}")
    print(f"NPV: {mejor_npv}")
    print(f"Secuencias evaluadas: {len(evaluadas)}")
    print(f"Resultados guardados en {csv_path}")
    print(f"Media posterior de beta_t por iteracion guardada en {betas_csv_path}")
    return 0


if __name__ == "__main__":
    sys.exit(_cli_main())
