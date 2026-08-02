# Y13 HPC analysis: scenario-consensus grid WFLO

## Paper pair

- Target: Du et al., *A Generic Acceleration Framework for Grid-Based Wind Farm Layout Optimization*, DOI `10.1109/TSTE.2025.3609006`.
- Problem: 36-direction Frandsen-Gaussian grid WFLO at 36, 100, 256 and 400 cells, with exactly half of the cells occupied.
- Method: scenario consensus, the p=2 box-sphere binary equivalence, ADMM and one mathematical-programming subproblem per wind scenario.
- Claim boundary: this is an equation-level flexible reproduction. The target source, Gurobi state, Danish arrays and warm-start assets are not public.

## Modeling audit

The printed formulation has two executable ambiguities. First, a maximization Lagrangian is printed with positive quadratic penalties and followed by an `arg min` update. The implementation uses the standard convex form: minimize negative scenario generation plus positive augmented quadratics. Second, the printed wake upper bound remains active when the target cell is uninstalled. A tight binary implication term relaxes that row at `x_i=0` and recovers the printed row exactly at `x_i=1`.

The cited Frandsen-Gaussian accepted manuscript defines the wake profile. Missing wind weights, power-curve revision, adaptive-rho rule, sphere-centre tie and warm start are recorded in the controlling contract. Optimized table results are never used as calibration targets.

## H0 complexity

For `K=36` scenarios and `n=s^2` cells:

- immutable wake preparation is `O(K n^2)` and stores `K n^2` doubles;
- each scenario subproblem has `3n` continuous columns and `O(n^2 + 32n)` sparse coefficients before zero removal;
- one ADMM iteration solves `K` independent subproblems, projects `2Kn` local variables and performs an ordered `O(Kn)` consensus reduction;
- only consensus and stopping data cross scenario boundaries.

The 20x20 case stores 5,760,000 pair entries, about 43.9 MiB as doubles. This is small enough for Waffle memory and large enough to expose scenario parallelism.

## H1-H4 implementation

- H1: precompute every scenario pair matrix once; use immutable contiguous scenario-major storage.
- H2: one persistent CPU team owns all 36 scenario slots; no per-iteration thread creation.
- H3: each pinned HiGHS solve is restricted to one thread, preventing nested oversubscription.
- H4: use fixed output slots, stable top-cardinality rounding and fixed scenario-order reductions.

Pinned HiGHS' active-set QP path returns a degeneracy `Solve error` for this constraint family despite a verified feasible probe. The admitted implementation therefore uses 33 uniform tangent cuts for each separable `x_i^2` epigraph. On `[0,1]`, the quadratic underestimation is at most `1/(4*32^2)`. The resulting LP preserves the paper constraints and linear scenario power term while providing a deterministic, auditable approximation to the augmented quadratic.

## H5 gates

1. Four paper grids have the exact cell and turbine counts, 630 m pitch and 36 scenarios.
2. Every layout preserves exact cardinality and has finite `0 < net AEP <= gross AEP`.
3. One/all-core runs return identical selected cells, AEP and scientific hash.
4. Every full run performs at most 10 ADMM iterations and 36 scenario solves per iteration.
5. Final rounding deviation is at most 5 percent for all four reconstructed cases.
6. The source header and discrepancy ledger disclose every unavailable or corrected field.

## H6 and formal protocol

H6 compares the same complete 20x20, 200-turbine, 36-scenario, 10-iteration workflow with one CPU worker and every available Waffle CPU core. Matrix, scenario-subproblem, algorithm and end-to-end time are reported relative to that explicit baseline. Formal evidence is one deterministic run for each paper case; no unreported 25-seed protocol is invented.

Local release evidence before the immutable Waffle run showed identical science and approximately 8.71x end-to-end acceleration from one to 20 CPU workers for the 20x20 case. This is a development admission result, not the final Waffle timing claim.
