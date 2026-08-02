# T82 H0-H4 mathematical and HPC analysis

## Evidence and reproduction boundary

The controlling paper is Cao et al., *Applied Energy* 323 (2022) 119599,
DOI `10.1016/j.apenergy.2022.119599`, local PDF SHA-256
`a510f421af5a76142d4f0bb5588a7c329f3aaa4ce777a121f9da95bc325b0a68`.
The paper exposes Eqs. (1)-(27) and three optimization cases, but no author
source, numerical wind arrays, random states, complete NSGA-II controls, or
raw Pareto fronts.  The paper states that data are available upon reasonable
request.  Exact-title, DOI, author, and GitHub searches located no
paper-linked public implementation.

The implementation is therefore a paper-equation academic reconstruction.
Figure-derived arrays and every completion are frozen in
`shared/contracts/core99_t82_cao_2022.json`.  It is not an author-code or
author-number replay.

## H0: mathematical work graph

For one layout with `N` turbines, `S` discrete wind states and `Q=8`
equal-area rotor samples, the dominant coupled evaluator performs:

1. `O(SN log N)` stable downstream ordering;
2. `O(SN^2Q)` MTG velocity-deficit and added-turbulence propagation;
3. `O(SN)` probability-weighted power and comprehensive-turbulence
   reduction.

The paper problems have `(N,S)=(30,1)`, `(39,108)`, and `(72,96)`.
One physical FES is exactly one complete layout evaluation over every wind
state.  NSGA-II additionally requires `O(P^2)` dominance comparisons per
generation for `P=100`; this work is much smaller than the coupled evaluator
for Cases II and Zhuanghe.

The causal dependency inside a wind state is real: upstream turbine inflow
turbulence controls its wake expansion rate in Eqs. (7) and (13), so the
downstream sweep cannot be arbitrarily reordered.  Layouts, offspring random
events, and dominance rows are independent and are the safe parallel axes.

## H1: semantic and numerical reformulation

- Compute wind rotation once per state and layout, then use one stable
  upstream-to-downstream sweep for both power and turbulence.
- Use eight immutable equal-area rotor samples to implement the Eq. (15)
  area average and the turbine-level Eq. (11) quantity.
- Accumulate each upstream wake's rotor-mean squared velocity deficit before
  Katic root-sum-square superposition.
- Retain the maximum added turbulence from all upstream turbines at each
  rotor sample before ambient quadrature.
- Use fixed-order floating-point reductions and disable contraction so worker
  scheduling cannot alter the scientific trajectory.

## H2: parallel architecture

One optimization owns one persistent C++ worker team.  The team parallelizes:

- initial layout perturbation and constraint repair;
- SBX crossover and polynomial mutation for every offspring;
- complete-layout evaluation across the population;
- one nondomination-relation row per population member.

Each complete layout remains internally serial because its turbine sweep
contains the paper's turbulence-to-wake dependency.  This avoids nested
oversubscription.  Counter-keyed random events make all evolutionary draws
independent of worker scheduling.

## H3: implementation traceability

- Paper/model and optimizer API:
  `hpc/core99_cpp/include/core99/cao_t82.hpp`
- Pure-C++ model and NSGA-II:
  `hpc/core99_cpp/src/cao_t82.cpp`
- Machine-readable command:
  `hpc/core99_cpp/src/cao_t82_main.cpp`
- Structural and deterministic-parallel tests:
  `hpc/core99_cpp/tests/cao_t82_test.cpp`
- Independent equation oracle:
  `scripts/validate_core99_t82.py`
- Resumable H6/formal runner:
  `scripts/run_core99_t82_h6_formal.py`

The source headers explicitly record the paper's 2.3 MW versus 3.0 MW
conflict, missing assets, selected resolution, and claim boundary.

## H4: bounded performance and scientific-equivalence evidence

Spark candidate measurements used the same Release binary, seed
`2026082000`, ideal Case II, population 100, twenty generations, and 2,100
complete-layout evaluations:

| quantity | one worker | twenty workers | one-to-twenty speedup |
|---|---:|---:|---:|
| coupled evaluator | 59.122979 s | 4.672981 s | 12.652x |
| algorithm orchestration | 0.009142 s | 0.031985 s | 0.286x |
| end to end | 59.132300 s | 4.705702 s | 12.566x |

Both executions produced scientific hash `3547e4c5a9144754`.  The small
orchestration component is slower with twenty workers because persistent-team
barriers dominate its short regions; it contributes under one percent of the
parallel wall time.  The paper-added coupled evaluator dominates and
accelerates materially.

The full paper work on Spark took 4.71 seconds for ideal Case II and 14.38
seconds for Zhuanghe using twenty workers.  These are development-candidate
measurements, not Waffle H6 claims.  Waffle H6 must repeat the identical
one/all-twenty comparison before formal admission.

