# T64 H0--H4 mathematical and HPC analysis

## Identity and evidence boundary

- Corpus: `T64`
- Paper: *The Impact of Land Use Constraints in Multi-Objective
  Energy-Noise Wind Farm Layout Optimization*
- DOI: `10.1016/j.renene.2015.06.026`
- Method semantic ID:
  `t64_nsga2_three_penalties_declared_reconstruction_v1`
- Problem semantic ID:
  `t64_energy_noise_land13role_declared_reconstruction_v1`
- Claim: academic flexible reconstruction of the paper's static, dynamic,
  and death-penalty NSGA-II methods and all native problem roles. It does not
  claim author source, native arrays, random streams, Pareto fronts, or
  numerical replay.

## H0: scientific contract and authoritative facts

The decision vector contains the continuous `x,y` coordinates of every
turbine in a 3 km by 3 km domain. The two objectives maximize annual energy
production (AEP) and minimize the maximum expected receptor sound pressure
level (SPL). The constraints require at least five rotor diameters (385 m)
between turbines and prohibit turbines in randomly generated non-feasible
land polygons.

The paper defines nine main problems by crossing 70%, 80%, and 90% land
availability with 5, 10, and 15 turbines. It also studies four 80%-available,
10-turbine maps with different centered-discrepancy uniformity. It specifies
24 wind directions, 43 speeds, Jensen wakes, ISO-9613 acoustics, population
sizes 200/150/100, crossover 0.95, mutation 0.05, and an 80,000 complete
layout-evaluation limit.

The authors state that their in-house serial C++ program implements NSGA-II
with static, dynamic, and death penalties, but provide no source or native
polygon, receptor, wind, seed, or front arrays. The reconstruction therefore
reuses the independently validated same-lineage T72 evaluator and freezes
every completion in
`shared/contracts/core99_t64_sorkhabi_2016.json`.

## H1: dominant mathematical work

For population size `P`, turbines `N`, wind states `W=24*43`, and receptors
`R`, each generation exposes approximately

`O(P * (W*N^2 + W*R*N + N^2 + R*N))`

independent physical work. The first term is wake interaction and the second
is receptor-noise propagation. Pairwise NSGA-II dominance adds `O(P^2)`
algorithm work. Feasible initialization and death replacement add bounded
constraint-only candidate sampling and, for every accepted replacement, one
additional complete physical layout evaluation.

The paper's physical evaluator dominates the complete trajectory. This makes
population-parallel evaluation the principal HPC region; dominance,
offspring variation, initialization, and death replacement are secondary
but still parallelized when their granularity is sufficient.

## H2: high-performance decomposition

One persistent C++ worker team is created once per optimization and reused
for all stages:

1. independent feasible initial layouts are constructed in parallel;
2. complete AEP, SPL, and constraint evaluations are partitioned by
   population index;
3. independent tournament, SBX, and polynomial-mutation offspring are
   created in parallel;
4. infeasible death-penalty children are regenerated and re-evaluated in
   parallel;
5. pairwise dominance rows are partitioned in parallel;
6. environmental selection and generation commit remain stable and ordered.

Every stochastic draw is keyed by logical generation, phase, individual,
turbine, and draw index. Fixed-index output, stable rank/crowding ties, and
ordered commits make the scientific trajectory independent of thread
scheduling. Formal runs use one all-core process at a time, avoiding nested
run-level oversubscription.

## H3: semantic and numerical validation

The C++ regression suite checks all nine main cases, four uniformity roles,
all five constraint-mode parameterizations, exact physical-FES accounting,
finite Pareto objectives, worker activation, and one/four-worker scientific
identity. The independent CLI validator checks 13 registered roles (12
unique problem instances), measured land availability, feasible reference
layouts, distinct uniformity parameters, all constraint modes, and identical
one/four-worker fronts and hashes.

The existing T72 C++ and independent H5 tests were rerun after generalizing
its shared kernel. All four T64/T72 C++ and H5 tests passed, establishing
that the new penalty modes and parallel feasible initialization do not
change the default T72 trajectory.

## H4: Spark complete-work profile

The independent H5 oracle additionally re-derives the paper-specific
`0.3u^3 kW` power curve and constant `Lw=100 dB` source through the full
Jensen and ISO propagation equations. Its maximum absolute AEP/SPL error
against the C++ evaluator is `1.1724e-13`.

The representative profile uses the same optimized pure C++ source,
80%-available land, 10 turbines, map 0, dynamic `C1`, seed 2026046399, and
the paper's complete 80,000 physical layout evaluations. One and twenty
workers both execute 533 generations, return the same Pareto front and
scientific hash `8503f7649500921b`, and activate exactly the requested
workers.

| Component | 1 worker C++ | 20 worker C++ | 20/1 speedup |
|---|---:|---:|---:|
| wind-farm physical evaluator | 7.314070 s | 0.574420 s | 12.733x |
| NSGA-II and constraint orchestration | 0.294055 s | 0.156723 s | 1.876x |
| end to end | 7.608137 s | 0.731157 s | 10.406x |

The result passes the core scientific requirement: more CPU resources reduce
wall time while preserving the complete physical budget and bit-identical
scientific result. Immutable Waffle H6 will repeat this full-work one/all-core
comparison, then the resumable formal runner will execute 1,175 target runs:
900 main static/dynamic runs, 200 uniformity dynamic runs, and 75
death-preliminary runs, all with 25 independent platform seeds per
configuration.
