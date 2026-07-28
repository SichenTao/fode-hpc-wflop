# FODE solving FODE-E0-L: HPC analysis freeze

The authoritative derivation is
[`../docs/mathematical_parallel_design.md`](../docs/mathematical_parallel_design.md).
This file freezes the implementation decision used by `FODE_CPP_HPC_FULL`.

The archived MATLAB evaluator performs the same direction-dependent wake
calculation inside each of 13 wind-speed iterations. Because the Jensen/Park
wake deficiency contains layout geometry and wind direction but no wind-speed
variable, the C++ evaluator computes it once per layout-direction pair. It then
applies all 13 velocity/probability terms. The work changes from
\(\Theta(BN_dN_vN_t^2)\) to
\(\Theta(BN_d(N_t^2+N_vN_t))\) without changing the objective.

The 17 stages are setup/initial population, initial evaluation,
ranking/snapshot, parameter sampling, fractional mutation/crossover, repair,
population evaluation, best update, selection flags, archive update,
selection/memory adaptation, history shift, population reduction, progress
ledger, local generation/repair, local evaluation, and finalization.

Parallel stages use the entire Waffle affinity domain: individuals;
individual-coordinate pairs; layout-direction pairs; and
layout-direction-downstream-turbine triples. Ordered control remains for stable
ranking, archive identity, SHADE memory reductions, LPSR and physical-FES
accounting. Counter-keyed draws make candidates independent of worker
scheduling.

The numerical gate is three frozen MATLAB fixed-layout anchors; the current
maximum absolute objective error is 0 kW. The resource gate is an observed
20-worker OpenMP team on affinity `0-19`. `--profile-phases` is diagnostic;
headline production timing runs without instrumentation.
