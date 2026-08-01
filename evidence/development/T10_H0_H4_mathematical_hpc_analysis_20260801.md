# T10 H0-H4 mathematical and HPC analysis

Paper: Rodrigues, Bauer and Bosman (2016), DOI `10.1016/j.rser.2016.07.021`.

This is development/admission evidence. Immutable Waffle H6 and the 1,960-run paper-native formal campaign remain separate.

## H0 — scientific identity

The paper studies one binary multi-objective WFLOP and four deliberately differentiated MOEAs: MOGOMEA, geographic offline o-MOGOMEA, NSGA-II and objective-clustered c-NSGA-II. The target package therefore contains all four. Algorithms appearing only as prior literature in Table 1 are comparison context and are not target implementations.

The fixed-resolution matrix has four wind-farm areas, three grid steps, four active CHT labels and four algorithms: 192 roles. Ten independent runs yield 1,920 receipts. Figure 17 adds four wind-farm-B multi-resolution roles and 40 receipts, for 196 roles and 1,960 receipts in total.

One physical FES is one complete binary layout evaluation returning both normalized energy and efficiency. It is not counted twice merely because the paper calls the budget one million evaluations “per objective”; both objectives share one physical wake traversal.

## H1 — mathematical work

For `m` occupied turbines and `S=12` wind directions, one evaluation performs `O(S*m^2)` directed wake checks and `O(S*m)` power interpolation. The worst dense paper layout has `m=169`, so the evaluator remains substantial but bounded. Conflict preprocessing performs `O(l^2)` work once; runtime violation checks traverse only the precomputed conflicting pairs.

NSGA-II dominance ranking is `O(n^2)`. Two-point crossover and mutation are `O(l)` per offspring. One MOGOMEA generation has five objective clusters, up to `2l-2` linkage subsets per individual, and online linkage construction from binary mutual information. A direct average-link implementation would repeatedly scan cluster pairs and approach cubic work. The selected nearest-neighbor-chain UPGMA uses the reducibility of average linkage, an immutable condensed distance matrix and deterministic Lance-Williams updates, reducing tree construction to quadratic work and memory.

The Katic-Jensen evaluator implements the paper's circle-overlap equation, thrust-dependent pair deficit and root-sum-square multiple-wake combination. The two objectives share exactly the same expected-power numerator. The penalty CHT subtracts one ideal isolated-turbine expected production per conflicting pair before both objective normalizations.

## H2 — legal parallel axes

- A batch of layouts is independent and is the primary evaluator axis. Each layout retains fixed wind/turbine arithmetic order, and results are committed by population index.
- An isolated layout can use twelve independent wind-state tasks for equation/H6 fixtures; formal optimization instead parallelizes the larger population to avoid nested oversubscription.
- NSGA-II child construction and all pairwise dominance rows are independent before deterministic survivor selection.
- GOM is sequential inside one individual because each accepted subset changes the next parent. Different individual trajectories read the same frozen generation, donor clusters and archive snapshot and therefore execute independently; their archive updates are committed in index order.
- The five linkage trees are independent. Online mutual-information pair statistics and tree construction run once per cluster; o-MOGOMEA builds its geographic tree once and reuses it.
- Archive commits, hypervolume history, population growth, forced-improvement ordering, stage transfer and generation transitions remain sequential.

## H3 — memory and numerical design

Binary layouts use 64-bit words, so the largest 2,401-variable chromosome occupies 304 bytes instead of a byte/object representation. Geometry, conflicts, wind projections and ideal powers are immutable. A linkage tree stores `2l-2` variable subsets. Average-link distances use a condensed triangular array; the largest tree requires about 92 MB while active, and cluster tasks release their matrices after the generation.

Counter-keyed random events assign every initialization, donor, crossover, mutation, repair and tie-break draw to a logical event rather than a worker. Fixed population indices and archive commit order make trajectories independent of scheduling. The build disables floating-point contraction. H5 requires exact one/all-worker objectives and archive hashes, with an independent Python implementation of Eqs. (3)-(9).

## H4 — selected implementation and admission gates

The selected backend is pure C++20 with the shared persistent CPU executor. Production uses every Waffle core. Admission requires:

1. all 12 farm/resolution cases reproduce Table 5 variable counts and construct paper-style feasible farthest-site populations;
2. 8D has no conflict pair and 4D/2D have explicit conflict graphs;
3. independent partial-overlap, Katic-Jensen, normalized-energy and efficiency equations agree on every case;
4. domination, penalty, repair and 100-try resample paths execute with their distinct physical-FES accounting;
5. all four target algorithms execute and one/all-worker scientific hashes agree;
6. strict warning, ASAN and UBSAN builds pass;
7. H6 measures the largest fixed evaluator batch and representative online/offline linkage orchestration at one versus every Waffle core;
8. only an immutable source commit may start the 196-role, 1,960-receipt formal campaign.

## Declared evidence boundary

No author source or result archive was found. Wind direction, UPGMA/MI ties, archive duplicate behavior, NIS base and Figure-17 CHT selection are therefore declared deterministic completions, not hidden guesses or author claims. The package is an academic flexible paper-equation reproduction, not a numerical replay of the unavailable Python implementation.
