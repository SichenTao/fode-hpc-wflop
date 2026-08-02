# T08 H0-H4 mathematical and HPC analysis

Paper: Guirguis, Romero and Amon (2016), DOI `10.1016/j.apenergy.2016.06.101`.

This is development/admission evidence. Immutable Waffle H6 and the 211-run formal campaign remain separate.

## H0 — scientific identity

The target is the paper's continuous-coordinate, fixed-count WFLOP solved by an exact-gradient multi-start interior-point method. One physical FES is one complete probability-weighted wind-resource objective and its simultaneous exact coordinate gradient. GA, HGA and finite-difference runs are comparison or diagnostic evidence and are not target-method packages.

The paper defines 20 unique problem instances and 49 proposed-method roles: 27 classical wind/count/initialization roles, 18 Horns-Rev scaling roles, and four land roles. Ten independent repeats of each classical 5S/20S role produce 211 formal optimization receipts.

## H1 — mathematical work

For `N` turbines and `S` wind states, one objective-gradient evaluation performs `O(S*N^2)` directed wake interactions and `O(S*N)` deterministic reductions. Exact derivatives reuse each wake term and therefore avoid the `2N+1` objective calls required by central finite differences. Pair-spacing work is `N(N-1)/2`; land and bound work is `O(N)`.

The paper's Gaussian modulation is continuous at the angular wake boundary because both its value and first angular derivative vanish there. Upstream ordering remains piecewise smooth at equal projected coordinates; deterministic initialization perturbation and line search avoid treating that switching surface as globally differentiable.

The feasible log-barrier objective is the negative efficiency plus the average of `-mu*log(-c_i)`. Limited-memory BFGS and Armijo backtracking operate on normalized coordinates. The barrier weight decreases geometrically. This is a declared open interior-point reconstruction because the MATLAB/fmincon trajectory is unavailable.

## H2 — legal parallel axes

- Fixed `wind-state × downstream-turbine` tasks are independent. Each task accumulates its own fixed-chunk scalar and `2N` gradient block; the final chunk-order reduction is deterministic.
- Pair constraints are independent, but the paper range ends at 4,950 pairs. Measurement showed persistent-team dispatch is slower than direct evaluation at this size. The production fast path is serial below 20,000 pairs; the same deterministic parallel kernel remains available for future larger cases.
- Independent local starts are embarrassingly parallel. The worker-group scheduler allocates `floor(W/G)` evaluator workers to each of `G=min(starts,W)` concurrent solves, so 1S uses all cores inside evaluation, 5S uses five four-core groups on Waffle, and 20S uses twenty one-core groups.
- Barrier phases, L-BFGS history, line-search trials and accepted iterates are sequential within one local solve and are not reordered.

## H3 — memory and numerical design

At most 512 deterministic objective chunks are allocated. Each chunk owns one scalar and one `2N` gradient vector; the largest T08 case therefore uses less than one MiB for reduction state. This measured granularity lets all 20 persistent workers participate in the heaviest paper-native wind rose while retaining useful speedup. Per-task wake activity uses fixed 100-entry stack storage, eliminating dynamic allocation. Wind rotations are precomputed once per evaluation.

All reductions preserve fixed chunk order and compile with `-ffp-contract=off`. Single/all-worker equality is therefore required bit-for-bit for objective, exact gradient, optimizer layout and scientific hash. Independent Python equations and central finite differences provide separate semantic oracles.

## H4 — selected implementation and admission gates

The selected backend is pure C++20 with the shared persistent executor. Production uses every Waffle core. Admission requires:

1. all 20 problem instances construct USL and LHS-lattice layouts with strict paper spacing and land feasibility;
2. independent Python Eq. (3), (5)-(7), (11)-(13) efficiency agreement;
3. analytical-gradient agreement with central finite differences;
4. exact one/all-worker evaluator and optimizer identity;
5. strict warning build plus ASAN/UBSAN;
6. measured evaluator and 20-start speedups, with no claim that lightweight constraint arithmetic itself accelerates;
7. immutable-source Waffle H6 before all 49 roles and 211 optimization receipts.

## Declared evidence boundary

The implementation reproduces the published equations, problems and target method flexibly. It does not claim the unavailable author MATLAB code, fmincon state, numerical wind/land arrays, polygon partition, random stream, layout, convergence history, timing or exact table values. Every completion and reason is registered in the controlling contract and discrepancy ledger.
