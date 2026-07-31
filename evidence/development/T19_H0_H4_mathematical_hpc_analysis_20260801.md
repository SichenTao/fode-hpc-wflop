# T19 H0-H4 mathematical and HPC analysis

## H0: authority and flexible-reproduction boundary

- The target is Dhoot et al., DOI `10.1016/j.energy.2021.120035`. The local 42-page primary PDF and text are hash-frozen. The 15-page author manuscript was visually checked, and the public predecessor thesis was searched for missing implementation constants.
- The paper explicitly links SRMP v1.01. Its GPL-3.0-or-later archive is pinned by SHA-256 and compiled as an optional GPL target. The paper's modified GEMPLP generator is not public, and the cited URL returns HTTP 403. The thesis does not recover beta, clusters, rounding or numerical WR-36.
- Every missing value, source/toolchain issue and deterministic completion is frozen in `shared/contracts/core99_t19_dhoot_2021.json` and repeated at the beginning of each implementation unit. Results are an academic flexible reproduction, not author numerical replay.

## H1: mathematical work decomposition

For `N` candidate cells and `S` wind states, Eq. (7) interaction construction performs `O(S*N^2)` independent directed wake calculations. Eq. (11) yields `N` binary unary terms and `N*(N-1)/2` pair factors. The 100/400-cell graphs add 5,000 consistency triplets; the 2,500-cell graph omits them as in the paper. Official TRW-S then performs ordered message sweeps. A posterior nonlinear Jensen/RSS evaluation of a decoded `K`-turbine layout costs `O(S*K^2)` and is the only event counted as one physical FES.

The absent beta is completed by `1.01*max_i sum_j(W_ij+W_ji)`, so a one-cell cardinality error costs more than the largest one-cell wake marginal. At 2,500 cells, all pairs closer than `5R=315 m` receive a prohibitive registered coefficient. The final decoder removes conflicts, reaches exactly `K`, and applies feasible one-swap improvement. Raw and repaired cardinalities remain separate evidence.

## H2: legal parallelism and memory

- Interaction rows, strongest-neighbor cluster scoring, and posterior wind-target power contributions are independent. A single persistent executor writes fixed slots; ordered reduction gives bit-identical one/all-worker science.
- Official TRW-S is sequential inside a role. Its forward/backward message order and lower-bound guarantee are the target algorithm; mechanical parallelization would define another algorithm. The 112 independent paper roles provide outer all-core parallelism with one inner worker per process.
- The no-triplet 2,500-cell graph uses the source shared-pair factor to avoid duplicating a four-entry table millions of times. Triplet roles use general source-native factors because SRMP v1.01 explicitly does not implement the shared factor's MPLP-message path.

## H3: pure-C++ implementation and source compatibility

The wrapper is C++20 with strict warnings, `-O3 -march=native -ffp-contract=off`, a fixed Jensen evaluator and official SRMP sources. The fixed archive receives one audited C++ safety patch: `FactorType` gains an empty virtual destructor because upstream deletes derived types through that base pointer. A project adapter forwards the legacy shared-factor initialization overload. Neither changes numerical arithmetic. Fixed insertion order (`sort_flag=-1`) is an official option and meets the paper appendix's fixed-order requirement; it avoids v1.01's unsafe equal-key quicksort on the dense triplet graph.

H5 checks an independent Eq. (7)/posterior-power oracle, paper-scale historical power, exact K, 5R feasibility, 2,500-cell no-triplet construction, one/all-worker bit identity, strict compilation, ASAN and UBSAN.

## H4: H6 and formal protocol

H6 fixes WR-36, 2,500 cells and `K=100`, with no triplets and one official TRW-S sweep. One and all visible Waffle cores must return the same layout, objective, AEP and hash. Stage receipts isolate interaction assembly, sequential TRW-S and posterior AEP; an end-to-end speedup is reported but not required to conceal the sequential solver fraction.

The formal campaign runs all 112 deterministic paper roles: four historical roles and both wind regimes for 10/25/19 turbine-count settings at 100/400/2,500 cells. It uses the paper's one-hour cutoff, official 10,000-iteration and `1e-8` convergence settings, 5,000 triplets for 100/400 and none for 2,500. Independent roles fill every Waffle core without nested oversubscription. Every result preserves source commit, binary hash, solver/matrix/AEP work, feasibility, raw/repaired cardinality, timing and scientific hash.
