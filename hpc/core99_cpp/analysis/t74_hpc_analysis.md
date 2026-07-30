# T74 SIGA high-performance analysis

Paper identity: Ju and Liu, Applied Energy 248 (2019), DOI
`10.1016/j.apenergy.2019.04.084`. The clean-room C++ implementation is
cross-checked against the MIT-licensed public source pinned at
`b1fc0d602928ee3f1fed5f8dc0de0a4a37a06bff`.

## Mathematical work decomposition

For one layout, one wind state costs \(O(N^2)\) wake-overlap operations and
\(O(N)\) power-curve operations. T74 fixes \(N=60\), evaluates 10,000
Monte-Carlo layouts to train one wind-distribution surface per problem case,
and then evaluates a population of 100 layouts for 200 generations. The
15 paper cases contain between one and 36 wind states.

The Monte-Carlo layouts are independent. Population layouts are independent
inside a generation. Worst-turbine relocation, crossover and mutation use
only event-keyed state belonging to one individual. These are safe parallel
regions. Ranking, parent-index construction, MARS coefficient fitting and
best-so-far reduction are small deterministic reductions and remain serial.

## HPC realization

One `fode::PersistentExecutor` owns all CPU workers for the complete run.
The calling thread participates, worker threads remain alive, and no nested
team is created. Monte-Carlo wake evaluations and all per-individual
algorithm stages consume that same team. Random events are keyed by
generation, phase, individual and draw, so one-worker and full-core runs
must produce the same scientific hash.

The surrogate uses paper grid knots and truncated-linear hinge products.
Forward selection stops at 100 bases or relative squared-error improvement
below \(10^{-3}\). The 10,000-layout artifact is built once per paper problem
case and reused across its 30 independent optimization repeats, matching the
public source lifecycle.

## Claim boundary and acceptance

H5 requires all 15 case contracts, physical FES accounting, both documented
paper/source probability variants, feasible monotonically retained best
solutions, observed multicore participation and identical one/multicore
scientific hashes. H6 measures a complete paper-scale representative case
with one and all Waffle cores. Formal production runs the primary
paper-probability variant for 15 cases and 30 repeats; published Table 4
means are comparison evidence, not exact-value gates.
