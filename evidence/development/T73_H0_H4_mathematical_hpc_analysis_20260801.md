# T73 H0-H4 mathematical and HPC analysis

## H0: authority and identity

- Target: Song et al., DOI `10.1016/j.cie.2018.04.051`; the local ten-page PDF hash is frozen in the controlling contract.
- No target implementation or data archive was found by exact-title, DOI, author and GitHub searches on 2026-08-01.
- The target explicitly cites Li and Wang, DOI `10.1109/WSC.2016.7822324`. Its official WSC PDF is pinned by SHA-256 and supplies the same GA--pattern-search lineage, 50 GA generations and 200 pattern iterations. Its 132-point problem is not substituted for T73's 342-point problem.
- All missing, conflicting and completed facts are listed before the C++ implementation and in `shared/contracts/core99_t73_song_2018.json`.

## H1: mathematical work decomposition

For a layout with `N` turbines, one stochastic objective evaluation has 40 wind samples and an ordered Jensen interaction cost of `O(40 N^2)`. The binary GA contributes `P(G+1)` complete evaluations. The continuous rotating-block pattern search contributes at most `20I` complete evaluations. Maintenance for interval `V`, `R` replications and `K=4N` components costs `O(R K 25*365/V)` with event-driven life renewal and a declared high-wind inspection-delay process.

The native result contract has twelve unique roles: the two Table-3 pre-maintenance layouts and the ten Table-5 combinations formed by two layouts and five inspection intervals. The paper's comparison literature is outside project scope.

## H2: legal parallelism and deterministic schedule

- GA candidates read one immutable generation and write distinct result indices; evaluation is fully parallel.
- Offspring randomness is counter-keyed by generation, child and gene. Survivor ranking and commits are stable and ordered.
- Each pattern iteration constructs twenty independent complete poll candidates. They are evaluated in parallel, and only the deterministic best improving move is committed.
- Maintenance replications are independent conditional on layout, clusters and interval. Each writes one receipt; means are reduced in replication order.
- Nested worker teams are forbidden. One persistent executor owns all optimization phases.

## H3: pure-C++ realization

The implementation is C++20 with `-O3 -march=native -ffp-contract=off`. Wind samples and trigonometry, site coordinates and physical tables are immutable. The same binary accepts one or every visible CPU core; the algorithm, problem and work budget do not change with worker count. Development profiling is not a paper result.

## H4: admission and formal protocol

H5 requires strict C++ tests, independent JSON validation, all twelve roles, both cluster-conflict profiles, physical dimensions and exact one/all-core scientific identity. H6 uses fixed nontrivial work only to measure one/all-core speedup. After H6, the primary four-cluster profile runs 25 seeds at 100 individuals, 50 generations, 200 pattern iterations and 1000 maintenance replications. Every run retains source commit, binary hash, work counts, timings, layouts, clusters, role results and scientific hash.
