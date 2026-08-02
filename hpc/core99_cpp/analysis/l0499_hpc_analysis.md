# L0499 uncertain-wind CVaR and fixed-count GA HPC analysis

## Scientific kernels

For `S` wind sectors, `N=50` turbines and `C` candidate points, a literal
Jensen/partial-overlap evaluation costs `O(SN²)` and repeats geometry,
circle-overlap and wake-expansion work for every GA individual. The
Dirichlet–Multinomial and normal-approximation contribution then evaluates
the expected AEP in `O(S)` and the variance in `O(S²)`.

The pure-C++ implementation precomputes

`deficit[direction, source_candidate, target_candidate]`

once in `O(SC²)`. A layout evaluation then performs only table reads,
RSS reductions, curve interpolation and the `O(S²)` covariance quadratic
form. This removes transcendental wake geometry from all 20,032 physical
evaluations.

## Algorithm-level parallelism

The paper's binary GA is generational: all parent fitness values are fixed
before parent selection, and every offspring belongs to the same generation.
Consequently, the 64 physical evaluations in one generation are independent
and safe to distribute across a persistent full-core team. Selection,
fixed-count crossover, mutation and deterministic `(mu+lambda)` survival use
counter-keyed random events and a stable total ordering, so worker count does
not change the scientific trajectory.

This differs from DEEM's active-layout sequential acceptance: L0499 has a
genuine population-evaluation parallel axis, not merely independent-seed
parallelism.

## Resource mapping

- Single optimization: all twenty Waffle cores evaluate one 64-member
  population; this is the H6 latency path.
- Paper campaign: one-worker optimizations are dispatched as twenty
  independent case/repeat tasks. This avoids nested oversubscription and is
  the expected highest-throughput mapping for 126 small problem/objective
  records.
- Both mappings use the same C++ evaluator and optimizer source and must
  produce the same scientific hash for the same case and seed.

## Candidate local H6 evidence

On the local development host, one versus twenty workers for the same
Case-B station-01 SO run, seed and 20,032 physical evaluations produced the
same scientific hash:

- evaluator: 0.656059 s to 0.059695 s, candidate speedup 10.990x;
- optimization end-to-end: candidate speedup 8.242x;
- the twenty-worker run observed all twenty participants.

These are development admission data, not Waffle formal results. The
deferred H6 runner repeats the comparison on Waffle before admitting the
formal campaign.

## H5 and H6 gates

H5 requires the exact fixture hash, all 126 case IDs, both grids, all 41
station records, normalized wind means, physical-scale power/AEP, independent
CVaR identity, common physical evaluator across TO/SO/RO, exact physical-FES
accounting, one/multicore scientific-hash parity and observed multicore work.

H6 requires one versus all twenty Waffle workers on the same full-budget
Case-B SO trajectory, all-core participation, exact hash parity and positive
evaluator/end-to-end speedup. Only then may the 126-case, 20-repeat formal
campaign start.
