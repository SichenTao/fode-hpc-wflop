# T24 H0--H4 mathematical and HPC analysis

## Identity and claim boundary

- Corpus: `T24`
- Paper: *Optimization of a Wind Farm Layout to Mitigate the Wind Power Intermittency*
- DOI: `10.1016/j.apenergy.2024.123383`
- Method semantic ID: `t24_nsga3_markov_intermittency_declared_reconstruction_v1`
- Problem semantic ID: `t24_kim_markov_intermittency_six_case_v1`
- Claim: academic flexible paper-equation and figure-constrained reconstruction of the target NSGA-III and all six real-scale optimization cases. It is not author code, original Marado data, private arrays, random states, or a numerical replay.

## H0: paper, source, and executable mathematical contract

The paper defines a 25-turbine continuous-layout problem on a square
`[0,20D] x [0,20D]`, with Vestas V112 turbines and pair spacing greater
than `3D`. Its two minimization objectives are reciprocal mean power and
the Markov-chain wind-power intermittency of Eq. (22). The six target
problems are uniform and real winds crossed with power-change thresholds
of 0%, 7%, and 15% of the 75 MW rated farm. The three-turbine model
problem is retained as a physics fixture rather than counted as a seventh
NSGA-III campaign.

Exact-title, DOI, author, preprint, and GitHub searches found no
paper-linked executable code, raw 2002--2022 hourly Marado wind data,
machine-readable 144-state wind rose or Markov matrix, optimized layouts,
or result-replay package. Section 3.1 says that hourly Korea
Meteorological Administration data were used, while the paper's data
availability statement says that no data were used. The implementation
headers preserve this internal provenance conflict.

The frozen completion uses the nine visible speed representatives and
figure-constrained probabilities, sixteen direction bins, and a
reversible local Markov kernel whose stationary distribution exactly
equals the declared wind rose. The periodic direction proposal has the
paper-reported 24.8-degree change scale. Uniform wind replaces only the
direction stationary distribution by `1/16`. This is a deterministic
reconstruction of Figs. 7--9, not the original Marado arrays.

The paper fixes population 92, 91 reference intervals, crossover
probability one, coordinate mutation probability `1/(2N)`, and a
hypervolume convergence statement after more than 1000 iterations. It
does not publish the hypervolume reference point, exact history rule, or
a non-convergence ceiling. The formal profile therefore freezes the
paper-visible 1000-generation work scale, yielding
`92 + 1000*92 = 92,092` complete-layout evaluations per run. It does not
claim to reproduce the missing dynamic stopping trajectory.

## H1: dominant mathematical work

For one layout, the evaluator computes power for 16 directions, nine
speeds, 25 turbines, eight rotor samples, and upstream turbine pairs.
Naively rebuilding direction geometry for every one of the 144 states
would duplicate ordering and coordinate projections nine times.
Eq. (22) also appears to require a dense `144 x 144` transition sum.

The implementation instead:

1. projects and sorts turbines once per direction and reuses the geometry
   over all nine speeds;
2. precomputes immutable V112 curves and equal-area rotor points;
3. stores only nonzero reversible transition weights and folds stationary
   probability into those weights;
4. keeps each downstream wake sweep serial because local inlet velocity
   is causal, while parallelizing independent layouts;
5. uses reciprocal power only in NSGA-III normalization and retains MW
   power in evidence receipts.

## H2: parallel decomposition

One persistent worker team owns the complete run. It parallelizes
population initialization and repair, offspring construction, complete
layout evaluation, dominance rows, and reference-line association.
There is no nested worker team and therefore no oversubscription.
Counter-keyed random events assign every crossover, mutation, repair,
and niching draw to a logical event rather than a thread. Stable sorting,
fixed index tie rules, and serial final reductions preserve the
scientific trajectory across worker counts.

## H3: equation and schedule equivalence

The independent Python oracle re-derived Eqs. (1)--(6) and (21)--(26),
the wind/transition completion, V112 interpolation, six reference
objectives, and six three-turbine Table 1 points without importing or
linking the C++ implementation.

- All six C++ reference objectives agree with the Python oracle to
  relative tolerance `2e-12`.
- All six three-turbine model powers agree with the independent oracle;
  each is within 6% of paper Table 1.
- The six reconstructed checkerboard reference objectives remain within
  20% for power and 25% for intermittency of paper Tables 2--3. This is a
  scale-consistency check, not numerical replay.
- A 40-individual, two-generation run has the identical scientific hash
  `900c0999450c4537` with one and twenty workers, and all twenty workers
  are observed.

## H4: Spark profile and paper-scale admission

The representative performance comparison used the same optimized C++
source, real-wind zero-threshold problem, seed, 92 individuals, 100
generations, and 9,292 complete-layout evaluations.

| Component | 1 worker C++ | 20 worker C++ | 20/1 speedup |
|---|---:|---:|---:|
| wind/Markov evaluation | 32.818018 s | 2.908372 s | 11.284x |
| NSGA-III orchestration | 0.092343 s | 0.058307 s | 1.584x |
| end to end | 32.910862 s | 2.970461 s | 11.079x |

Both worker counts produced scientific hash `7310a760b8ea254`.

One full 20-worker paper-scale run on the real-wind zero-threshold case
completed 92,092 layout evaluations in 28.661904 s:

- evaluator: 28.067248 s;
- algorithm orchestration: 0.554120 s;
- observed workers: 20;
- feasible first-front cardinality: 92;
- maximum front mean power: 39.719469 MW;
- minimum front intermittency: 7.133466 MW;
- scientific hash: `892a051de8df7b2d`.

The Waffle H6 gate will repeat the full 92,092-evaluation trajectory with
one and all twenty workers. Formal quality runs use only the admitted
20-worker configuration for six cases by 25 independent seeds.
