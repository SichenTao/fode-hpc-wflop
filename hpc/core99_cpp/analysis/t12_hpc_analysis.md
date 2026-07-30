# T12 H0-H4 HPC analysis

Authority: Wilson et al., DOI `10.1016/j.renene.2018.03.052`; public WindFLO
revision `9e85a67bb2ca019768ea51dd0b634a46c8406ba2`; released 2015 competition
scenarios and top-four entry archives; and the explicit completions in
`shared/contracts/core99_t12_windflo_2015.json`.

## H0 — scientific work unit

One physical FES is one call to the released WindFLO mathematical evaluator
for a complete layout. It includes feasibility, pairwise Kusiak/Jensen wake
deficits, 24 directional Weibull integrations, the released turbine power
curve, wake-free ratio, individual turbine output, and the 2015 cost-of-energy
formula. Cached and surrogate-only calls do not consume physical FES. Each of
the five paper scenarios has a 2,000-FES budget, for 10,000 physical
evaluations across one complete five-scenario run.

## H1 — dependency and parallelism analysis

- Wind directions and turbine outputs are independent before fixed-order
  reduction.
- Candidate layouts in one DE/CMA-ES generation, one Goldman coordinate
  sweep, or one admission batch are independent before ordered selection.
- SSHH state transitions, CMA covariance updates, DE selection, and Goldman
  coordinate commits remain sequential across iterations because each depends
  on the preceding accepted state.
- 3s-MDE surrogate trajectories are independent across 200 initial individuals
  and ten offspring. Its translation-invariant pair surrogate can aggregate
  lattice neighbor terms analytically without constructing a full layout for
  each of millions of surrogate-only steps.

## H2 — data and kernel design

The five XML files are compiled unchanged into contiguous constant arrays.
Candidate and direction indices are flattened into one task space, so a
CMA-ES generation with only eight layouts still exposes `8 × 24` tasks to
Waffle. Every task owns its wake scratch state. Results are stored by fixed
layout and direction index, followed by deterministic paper-order reductions.
One persistent worker team serves both algorithms and the evaluator.

3s-MDE's pair measurements still use the exact WindFLO evaluator. The HPC
surrogate aggregates those measurements over integer lattice-neighbor vectors;
the exact decoded and spacing-repaired layout is reconstructed before every
physical WindFLO call. Because this analytic boundary approximation can shift
the absolute surrogate scale, the paper's 1.20 filter remains primary and a
documented deterministic top-ranked completion fills half a generation only
when fewer candidates pass. This prevents zero-FES generations without
claiming author-exact replay.

## H3 — selected pure-C++ execution

- fixed layout: parallelize 24 wind directions;
- multi-layout batch: flatten layout-by-direction tasks;
- 3s-MDE: parallel surrogate trajectories, batched physical candidates, exact
  ordered DE/local-search commits;
- geometric CMA-ES: batched offspring and ordered covariance adaptation;
- SSHH: direction-parallel evaluation and serial learned sequence commits;
- Goldman: parallel coordinate alternatives and deterministic
  best-improvement commit.

The production CLI records physical FES, feasibility, objective, evaluator
time, algorithm-only time, end-to-end time, observed workers, and scientific
hash.

## H4 — admission policy

H5 requires independent Python-vs-C++ equation agreement on all five
scenarios, exact one-worker/multi-worker scientific results, and smoke coverage
for all four algorithms. H6 uses Waffle's available full CPU resource for a
complete 2,000-FES scenario run of every method. A small one-worker C++ or
original-language timing is added only for acceleration evidence; formal
quality uses the admitted all-core mode. Formal completion is the full
four-method by five-scenario by 25-seed matrix.

## Source conflict retained

The public archive labelled CMA-ES contains no identifiable covariance-matrix
adaptation and instead implements a grid fill/refinement method. The benchmark
therefore uses the paper's explicit five-variable geometric decoder and
standard rank-mu covariance adaptation, while retaining the archive, revision,
and conflict in source headers and the T12 dossier. It does not silently call
the conflicting MATLAB file CMA-ES.
