# T62 FODE-HPC-level analysis

## H0-H2: paper semantics and mathematical work

T62 adds the improved Jensen-Gaussian wake equations to the standard
2000 m square Case (b). One complete physical FES evaluates 36 directions
and, for \(N\) turbines, accumulates \(O(36N^2)\) source-target wake terms.
The native N values are 38, 39, and 40. The reconciled MPGA contract has ten
demes and 20 individuals per deme.

Material internal conflicts are registered in
`shared/contracts/core99_t62_gao_2016.json`; no silent choice is made. The
grid-centre interpretation is primary and a continuous-coordinate sensitivity
problem preserves the incompatible 2N 20-bit prose.

## H3-H4: high-performance implementation

For the fixed grid, wake fraction depends only on direction, target site, and
source site. The implementation precomputes all
\(36\times100\times100\) squared deficits once. Candidate evaluation becomes
contiguous lookup and fixed-order accumulation, avoiding repeated
trigonometry, turbulence, exponential, and Gaussian work.

The persistent executor parallelizes 200 independent deme-individual
evaluations. Random generation, selection, crossover, mutation, elite
retention, and ring migration use deterministic ordered state commits. This
avoids both algorithm-semantic drift and nested oversubscription.

## H5-H6 admission

H5 independently checks Eq. (17)-(19), isolated-turbine 518.4 kW, efficiency
arithmetic, and the cost/fitness relation; it detects the paper's inconsistent
N=38/39 table cells. A bounded MPGA smoke verifies FES accounting.

H6 runs N=38, 39, and 40 with the 500-unchanged-generation stop, ten demes,
20 individuals, and all currently available Waffle cores. Admission requires
feasible layouts, complete receipts, observed multicore work, fixed-seed
determinism, and plausible results relative to the printed power/efficiency
anchors. Exact author values are not required because source, seeds, layouts,
and several MPGA rules are unavailable.
