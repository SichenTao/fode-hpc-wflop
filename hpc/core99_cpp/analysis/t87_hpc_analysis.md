# T87 IGA-PSO HPC analysis

## Scientific execution graph

The paper-native run is:

`figure-derived resource field -> IGA binary layouts -> unique fitness
evaluation -> best grid layout -> PSO continuous refinement -> four-case
fitness receipt`.

The two algorithms share one evaluator. The IGA changes a 522-bit candidate
mask and the PSO changes continuous `x/D,y/D` coordinates for the turbine
count selected by IGA.

## Dominant arithmetic

For `N` turbines and `S=49` nonzero wind states, one layout evaluation is
`O(SN^2)` wake work plus `O(N^2)` spacing work. The paper run proposes
`300 + 1000*300 = 300300` IGA layouts and
`100 + 200*100 = 20100` PSO particles.

The IGA paper explicitly observes repeated individuals. Its scientifically
valid work count therefore has two columns:

- proposed FES: every algorithm-requested candidate;
- physical unique FES: each unique IGA bit string evaluated once per
  generation, plus every PSO continuous particle evaluation.

These counts must not be conflated.

## High-performance transformations

1. The Fig. 2/5/9 fixture is parsed once.
2. Candidate speed multipliers are solved once so every no-wake AEH closes
   against the image-derived proxy.
3. For the selected wake model, every
   `(wind state, source candidate, target candidate)` rotor-averaged deficit
   ratio is precomputed once in a full-core persistent team. The hot IGA
   evaluator then performs indexed loads and fixed-order RSS additions rather
   than transcendental wake formulas.
4. Each IGA population is hashed and deduplicated. Only unique masks enter
   the evaluator, and those independent evaluations use the persistent team.
5. PSO positions cannot reuse the candidate-pair table. Particle state
   updates and continuous evaluations are nevertheless parallelized across
   particles. Every particle owns its temporary reduction state; no atomic
   floating reduction is placed in a hot loop.
6. Counter-keyed random events make selection, crossover, mutation, velocity
   and position draws independent of worker scheduling. Fixed per-layout
   state/source/target order provides one-core/multicore scientific replay.
7. Population and particle buffers are allocated once and swapped or reused.
   The implementation creates no short-lived thread team per generation.

## Why not parallelize inside one light layout

The paper-scale IGA exposes up to 300 independent layouts and the PSO exposes
100 independent particles. Parallelizing that outer axis preserves a
substantial amount of work per task and avoids barriers inside every
49-state evaluation. A single-layout state-parallel fallback would add nested
coordination and is not used while population-level parallelism saturates all
20 Waffle cores.

## Admission evidence

H5 verifies:

- 522/7585 candidate closure to the published 6.9%;
- a deterministic 4D packing of 16 turbines, within Table 2's 15–18 scale;
- all four wake/cost cases and physical bounds;
- proposed-versus-unique FES accounting;
- fixed-seed one-worker/four-worker/replay scientific identity;
- actual persistent-team participation;
- PSO retention of the feasible IGA starting point.

A full paper-budget development pilot on 20 cores completed all 320400
proposals in 0.984914 s, of which 90659 were physical unique evaluations.
The evaluator used 0.916199 s, algorithm work used 0.068715 s, all 20 workers
participated, and the feasible IGA/PSO results were 122904.58/126805.58 MWh.
This is a Spark-side development admission result, not Waffle H6 or a formal
publication result.

## Formal execution policy

H6 and formal paper runs use the highest admitted Waffle configuration:
20 CPU cores inside one optimization. The target paper does not state an
independent-repeat count; the platform will use ten declared independent
seeds for stochastic uncertainty while keeping the paper's algorithm sizes
unchanged. No author-number claim is made from the figure proxy.
