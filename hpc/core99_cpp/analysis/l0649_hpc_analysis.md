# L0649 HPC analysis: analytical FLOWERS AEP and gradient-based WFLO

## Paper pair and evidence boundary

- Target: LoCascio et al., *FLOWERS AEP: An Analytical Model for Wind Farm Layout Optimization*, DOI `10.1002/we.2954`.
- Target method: closed-form FLOWERS AEP, its analytical turbine-position gradient, and gradient-based optimization.
- Target problem: WR7, nine NREL 5 MW turbines, a square of side `14D`, a `3x3` initial layout with `4D` spacing, and ten Fourier modes.
- Public source oracle: `https://github.com/locascio-m/flowers`, revision `dcb729f7ea4ab9307344e45c329b6f50796e861b`. The revision has no license declaration, so it supplies semantic and numerical checks without source redistribution.

The paper uses proprietary SNOPT through pyOptSparse. The executable solver state and final coordinates are absent. The open reproduction uses projected limited-memory BFGS with the paper analytical gradient and its `1e-3` optimality tolerance. This solver replacement changes the iteration trajectory while retaining the same problem and stationary solution neighborhood.

## Modeling and source audit

The independently implemented ten-mode WR7 model returns `120870064988.85013 Wh` at the paper initial layout, matching the paper-linked source oracle. Three analytical-gradient components match that source within `2e-12` relative error, and an independent centered finite-difference check passes within `1e-5` relative error.

The paper states that randomized layouts use `(N+1)` nodes along x and six nodes along y. Paper-era `tools.py` used `(N+1)` in both dimensions; the repository's 2026 revision changes y to six. The paper definition and corrected source control the separate N=500 performance layout. The paper's unseeded 200-case randomized corpus and FLORIS baselines are outside the target-contribution completion gate.

## H0 complexity

For `N` turbines and `M` retained Fourier modes:

- FLOWERS AEP evaluates every ordered turbine pair in `O(N^2 M)` work and `O(N^2)` temporary storage;
- the analytical gradient reuses the same pair derivatives and is reduced in `O(N^2)` work;
- one optimization variable vector has `2N` entries;
- the paper-native problem has `N=9, M=10`; the paper scaling study reaches `N=500` at `M=10`.

At `N=500, M=10`, one evaluation performs 2,495,000 ordered pair-mode terms. This is the H6 workload because it exposes parallel pair physics while remaining exactly within the paper's reported farm-size and resolution range.

## H1-H4 implementation

- H1: precompute each ordered pair's integrated deficit and Cartesian derivatives once per layout.
- H2: partition immutable target-turbine rows across one persistent all-core C++ team.
- H3: reuse pair derivatives for both AEP and gradient; compute each moved-turbine gradient in an independent output slot.
- H4: retain fixed target-source-mode ordering, fixed reductions, projected square bounds and deterministic L-BFGS history.

The nine-turbine target contains only nine independent target rows, so full machine occupancy is not a valid requirement for each microstage. The N=500 H6 workload tests all-core utilization. The implementation reports requested and observed worker counts separately.

## H5 gates

1. The target geometry, turbine count, rotor diameter, WR7 and ten modes are exact.
2. Initial AEP and selected analytical-gradient components match the author source oracle.
3. Centered finite differences independently validate the analytical gradient.
4. One/all-core runs return identical AEP, gradients, layouts and scientific hashes.
5. The open optimizer reaches the paper's 13.8 percent FLOWERS objective-gain neighborhood while satisfying the `1e-3` projected-gradient tolerance.
6. The N=500 paper-scale evaluator is finite, deterministic and genuinely multicore.

## H6 and formal protocol

H6 compares one CPU worker with every available Waffle CPU worker on the same WR7, N=500, M=10 AEP-plus-gradient evaluation. Each configuration runs three times and reports median wall time. The formal protocol executes the paper's single deterministic nine-turbine optimization with the all-core backend. No 25-seed protocol is invented because neither the target optimization nor the open replacement is stochastic.
