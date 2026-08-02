# T01/T02 historical-grid H0-H3 analysis

## H0 — work model

A binary layout contains 100 candidate cells and a variable number `N` of
turbines. One physical FES evaluates every wind state. Cases A, B, and C have
1, 36, and 108 wind states. Direct wake evaluation costs
`O(S*N^2)`, where `S` is the wind-state count. The paper-profile GA evaluates
200 or 600 independent layouts per generation.

## H1 — authority and completion

The wake, cost, grid, turbine, wind, and GA contracts come from T01 and T02.
The registered completion decisions are in
`shared/contracts/core99_mosetti_grady_cases.json`. The later WFLOPG revision
`05a3a6cd2e767f956dcc4a15256f7854e923624a` is an independent interpretation
only; it has no visible license and is not copied into production.

## H2 — dependency graph

Wind-state geometry depends only on the case and is immutable. Layout
evaluations within a generation are independent. Selection depends on the
complete current-generation objective vector. Each island then generates its
offspring independently, followed by the next population-wide evaluation
barrier. This preserves generational and island semantics.

## H3 — production design

- Precompute 36 x 100 x 100 rotor-overlap-adjusted wake coefficients once.
- Encode each layout in two 64-bit words and materialize selected cells in a
  fixed stack array.
- Reuse one persistent C++ worker team across initialization, island offspring
  generation, and population evaluation.
- Parallelize population evaluations and independent islands.
- Keep roulette accumulation, global-best commits, and output reduction in a
  deterministic order.
- Use counter-based random events keyed by semantic profile, seed, generation,
  island, child, operator, and gene, making results independent of scheduling.
- CPU is the production backend because each layout performs branch-heavy
  small-array work and the 600-layout batch already saturates Waffle's 20 CPU
  workers without host-device transfer.
