# T22 H0-H3 HPC analysis

Authority: Thomas et al., DOI `10.5194/wes-8-865-2023`, the paper-linked
archive DOI `10.5281/zenodo.7125349`, public source revision
`41d7290b8cc9bf3d90b25d844312f4790037806d`, and the declared completions in
`shared/contracts/core99_t22_iea37_cs4.json`.

- H0: one physical FES is one complete AEP calculation for a proposed layout:
  81 turbines, 360 direction bins, 20 conditional speed bins, pairwise
  simplified-Gaussian wake deficits, RSS wake combination, turbine power
  integration, and feasibility accounting. DEBO performs the paper's greedy
  candidate comparisons and local-neighborhood comparisons; every candidate
  AEP is counted once.
- H1: each fixed-layout evaluator has independent wind directions followed by
  a fixed-order sum. More importantly, every greedy or local-search candidate
  in one DEBO decision is independent until the ordered argmax commit. The
  latter is the coarser parallel level and avoids thousands of short nested
  direction teams.
- H2: numeric case data are compiled once into contiguous C++ arrays. Candidate
  batches use one persistent CPU team; each candidate uses stack-local
  downwind, crosswind, and deficit arrays. Candidate results are stored by
  index, so all tie breaking and layout updates remain deterministic.
- H3: production DEBO parallelizes the complete candidate set and commits one
  paper-ordered move at a time. Standalone fixed-layout validation instead
  parallelizes 360 directions. Both modes expose evaluator, algorithm, and
  end-to-end wall time separately. H6 compares one worker with all 20 Waffle
  logical CPUs using the same source, seed, termination, and scientific hash.

Scientific and provenance checks already completed before H6:

- independent Python equations agree with C++ within
  `4.66e-10 MWh` for the baseline and exactly for the public DEBO layout;
- one-worker and multi-worker C++ AEP values are bitwise identical;
- the public DEBO layout recalculates to `2913220.604170286 MWh`, agreeing with
  the paper's rounded `2913.221 GWh`;
- the archived DEBO YAML's embedded `2861182.50569 MWh` is stale and is
  retained as a source conflict, not used as an evaluator oracle;
- the rounded public baseline accumulates `1.130514 m` polygon residual,
  whereas the published DEBO result is feasible.
