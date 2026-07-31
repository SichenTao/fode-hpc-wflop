# T18 H0-H4 mathematical and HPC analysis

## H0: authority and identity

- Target: Reddy, DOI `10.1016/j.apenergy.2020.115090`; the local 13-page PDF hash is frozen in the controlling contract.
- The Apache-2.0 WindFLO repository is pinned at revision `97dd43784bffb1c0c8a4388d8e7929b337d496a5`. Its complete evaluator and data are source authority. A temporary compiler-port build reproduced all stored Example2 scalar outputs exactly.
- The target already declares master-slave parallel optimization, and the public PSO exposes an external parallel-evaluation callback. The available evaluator remains serial and target SOHO parallel source, scheduling rules and scaling data are absent. The contribution boundary is an auditable deterministic high-performance realization and measurement, not first parallelization.
- The repository has no target SOHO implementation, numeric 2019 cli-MATE wind table, target states, traces or optimized layouts. The FIU dissertation, DOI `10.25148/etd.FIDC008890`, is the supplemental authority for the three constitutive evolutionary kernels.
- Every source/paper conflict, missing field, modeling correction and deterministic completion is frozen in `shared/contracts/core99_t18_reddy_2020.json` and at the beginning of each implementation unit.

## H1: mathematical work decomposition

For `N=25` turbines and `S=16*7=112` direction-speed states, one complete layout evaluation has ordered wake-interaction work `O(S N^2)`. BP also evaluates `Q=64` deterministic equal-area rotor points per intersecting pair. Four optimized Table-4 roles each execute `P(G+1)` complete layout evaluations, where the formal population is `P=100` and generation count is `G=200`. The two reference roles add two evaluations. Tables 2 and 3 add 48 independent wind-tunnel validation roles.

The native result contract therefore contains 54 roles per seed: 48 source-profile validation roles and six corrected-profile Table-4 roles formed by two wake models and reference/Case-1/Case-2 designs. The source profile reproduces the public 1000-point Sobol uniform-radius integration used for Tables 2-3; the optimization profile uses 64 deterministic equal-area points. One physical function evaluation is one complete 25-turbine, 112-state expected-power and constraint evaluation.

## H2: legal parallelism and deterministic schedule

- Complete offspring read one immutable parent generation and write separate candidate slots. Both generation and evaluation are parallel.
- Evolutionary random events are keyed by seed, generation, kernel, child and coordinate. Stable ordered survivor selection and a single ordered relay decision commit the next generation.
- The 48 validation roles are independent and write fixed result indices.
- Terrain grids, wind states, turbine curves and rotor points are immutable. Within-layout wake reductions retain fixed order, so the one-worker and all-worker executions preserve exact scientific identity.
- A single persistent executor owns all parallel phases. Nested teams and schedule-dependent random streams are excluded.

## H3: pure-C++ realization

The implementation is C++20 with `-O3 -march=native -ffp-contract=off` and strict warnings. A 65 by 65 RBF or IDW terrain grid replaces repeated interpolation setup. Wind probabilities, trigonometric direction vectors and turbine tables are precomputed. The same binary accepts one or every visible CPU core; semantic IDs, physics, algorithm and work budget remain unchanged.

## H4: admission and formal protocol

H5 requires strict compilation, unit equations, 54-role JSON validation, both terrain/disk profiles and exact one/all-core scientific identity. The source validation profile must place at least 47 of 48 published velocities within 0.10 m/s and every role within 0.25 m/s; the wider registered bound isolates the unstable downstream Larsen-quadratic role. H6 uses the 25-turbine AWEC problem, 100 candidates, two generations and 64 optimization rotor points to measure one/all-core acceleration without spending the full research budget. After immutable Waffle H6 admission, the corrected optimization profile executes 25 seeds, population 100, 200 generations, 20-generation stagnation and 64 rotor points. Each run retains source commit, binary hash, physical work, timings, validation records, optimized layouts, kernel relay history and scientific hash.
