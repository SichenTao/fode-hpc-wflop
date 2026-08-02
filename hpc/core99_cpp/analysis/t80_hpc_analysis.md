# T80 AGA-MCTS high-performance analysis

T80 is Bai et al., Energy Conversion and Management 252 (2022), DOI
`10.1016/j.enconman.2021.115047`. No paper-linked author source was found.
The clean-room implementation follows the paper equations and algorithms;
the public T74 predecessor is used only for the disclosed GA lineage.

## Mathematical work and dependency graph

A complete grid-layout evaluation costs
\(O(SN^2)\), where \(S\) is the number of discrete wind states. Case I
uses \(N=60\) and 1, 4, 6 or 36 states; the New Jersey proxy uses \(N=99\)
and 64 figure-derived states. A paper-scale run evaluates 100 population
layouts for 200 generations. For each individual selected for exploitation,
200 SP-MCTS rollouts evaluate additional complete terminal layouts.

Population layouts and the MCTS trees belonging to different individuals
are independent inside one generation. Their wake computations and tree
work are the dominant safe parallel region. Selection, expansion, reward
backup and UCT statistics inside one tree depend on prior simulations and
remain serial. Population ranking and global-best reduction are small
deterministic serial reductions. External individual generation, crossover
and mutation are independent per offspring.

## HPC realization

One persistent C++ worker team covers the complete optimization and is
reused by all safe parallel regions. MCTS does not create nested teams:
the outer population distributes independent adaptive trees to workers.
Counter-keyed events assign every draw to a generation, phase, individual,
tree simulation and action, so one-worker and all-worker executions have
identical layouts, physical FES and scientific hashes.

Physical FES counts every complete wake evaluation: 100 population
evaluations per generation plus one evaluation for each MCTS terminal
rollout actually executed. This makes the extra reinforcement-search work
visible instead of hiding it as algorithm overhead.

## Acceptance and claim boundary

H5 checks all 13 paper problem registrations, the declared missing-field
completions, deterministic one/multicore replay, physical FES invariance,
feasibility and retained-best monotonicity. H6 compares the complete
Scenario-1 medium case using one and all twenty Waffle cores. Formal
production runs the 13 paper cases for the paper's ten repeats.

Published Tables 1-5 are scale and trend references rather than exact-value
gates. The New Jersey wind distribution is a documented figure-derived P3
proxy because the paper does not publish the numeric array.
