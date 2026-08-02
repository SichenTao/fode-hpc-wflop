# L0341 3-D Gaussian wake and MDPSO HPC analysis

## Scientific work units

One physical function evaluation is one complete wind-farm power evaluation
for one MDPSO particle. The uniform problems use populations of 1020, 1820 or
2420 for 15000 iterations. The nonuniform problems use populations of 1500,
2700 or 3600 for 20000 iterations. The ten paper cases therefore range from
15,301,020 to 72,003,600 complete farm evaluations per independent run.

Within one farm evaluation, work is the Cartesian product of wind direction,
turbine target, rotor point and possible upstream wake source. Scenario C has
36 directions and three speed states. Geometry and wake multipliers depend on
direction but not speed, so they are computed once and consumed by all three
speed states.

## Safe mathematical factorization

Every particle is independent after the generation-global best has been
frozen. Consequently both expensive parts of each generation are safe
parallel regions:

- update every particle position and velocity from immutable generation state;
- decode and evaluate every complete wind-farm candidate.

Personal-best and global-best selection remains a fixed-order reduction. That
small ordered section preserves exact one/all-core trajectories and avoids a
short parallel region whose synchronization would exceed its work. Random
events are keyed by generation, operation, particle and dimension rather than
drawn in execution order.

## Implemented backend

The pure-C++ backend creates one persistent worker team for the full
optimization. The same team executes particle motion and population
evaluation; no per-generation thread creation and no nested parallelism occur.
The evaluator reuses direction trigonometry, groups the three scenario-C speed
states beneath one geometry pass, and performs a compact three-point rotor
quadrature.

The local fixed-work probe used WFA, scenario C, the paper population of 1020,
ten iterations and 11,220 physical evaluations. One and four workers produced
the same scientific hash (`6689e51c3b1fc533`):

| component | one worker | four workers | speedup |
|---|---:|---:|---:|
| farm evaluation | 0.037760 s | 0.009942 s | 3.798x |
| MDPSO orchestration | 0.009000 s | 0.003240 s | 2.778x |
| end to end | 0.046760 s | 0.013181 s | 3.548x |

These are local admission measurements, not Waffle H6 or paper conclusions.

## Waffle H6 and formal policy

H6 runs one representative complete paper-scale optimization with one worker
and all twenty Waffle workers and requires identical scientific hashes,
physical FEs and final objective. It reports evaluator, algorithm and
end-to-end speedups separately. The formal campaign runs all ten named paper
cases for 25 independent seeds. Twenty independent one-worker case/seed
processes maximize aggregate throughput and never oversubscribe the
twenty-core allocation.

## Claim boundary

The implementation reproduces the published problem classes, equations,
MDPSO parameters, case matrix and paper-scale iteration budgets. Missing
Figure 10/11 arrays, wake calibration, initialization, constraint details and
variable-cardinality encoding are isolated, versioned completions. Table 3 is
used as a physical scale and directional-response check, not a promise of
author numerical replay.
