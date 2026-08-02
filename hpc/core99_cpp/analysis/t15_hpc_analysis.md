# T15 IEA37 case-study H0–H4 analysis

Paper: Baker et al., *Best Practices for Wake Model and Optimization Algorithm
Selection in Wind Farm Layout Optimization*, DOI `10.2514/6.2019-0540`.

## H0 — contribution and reproduction unit

T15 is a benchmark/comparison-protocol paper, not a new-optimizer paper.
Creating a fictitious “T15 algorithm” would be academically wrong. Its
paper-native completion unit is:

- case study 1: the common simplified Gaussian evaluator, 16/36/64-turbine
  circular problems, one example and participant 1–10 layouts for each size,
  and the published ranking;
- case study 2: the 9-turbine/900 m problem, five submitted layouts, and the
  cross-evaluation protocol/report matrix.

The five case-study-2 wake-model implementations and all participant optimizer
implementations were not released. Their absence is a declared original-asset
boundary, not permission to fabricate them.

## H1 — evaluator decomposition

For every direction \(d\), the released case-study-1 evaluator rotates
coordinates and computes

\[
\delta_i(d)=\sqrt{\sum_{j:x_i>x_j}
\left[
\left(1-\sqrt{1-\frac{C_TD^2}{8\sigma_{ij}^2}}\right)
\exp\left(-\frac{y_{ij}^2}{2\sigma_{ij}^2}\right)
\right]^2},
\quad
\sigma_{ij}=kx_{ij}+D/\sqrt{8}.
\]

The effective velocity is \(9.8(1-\delta_i)\), the released cubic/rated power
curve is applied, and AEP sums 16 directional contributions.

Every archived layout is independent. Ranking is a deterministic reduction
within each farm size.

## H2 — cost and parallel grain

One layout costs \(O(16N_t^2)\); the full paper matrix contains 33 layouts.
The appropriate H6 grain is therefore layout-level parallelism over the
persistent worker team. Creating nested thread regions for only 16 directions
or short turbine-pair loops would add avoidable synchronization.

For future algorithm adapters, independent candidate layouts can use this same
batch evaluator. A single candidate can instead use direction-level
parallelism when no batch exists.

## H3 — implementation decisions

- Compile all paper-native layouts and public expected AEP values as immutable
  arrays generated from YAML.
- Evaluate all 33 layouts concurrently in pure C++.
- Commit ranks only after evaluation, with stable AEP-descending order.
- Report archived AEP, recalculated AEP, absolute error, constraint violation,
  worker receipt, timing, and scientific hash.
- Retain participants 11–12 in the public-source provenance but exclude them
  from the 2019 paper-native matrix.
- Treat case study 2 as a heterogeneous cross-model protocol; store the five
  public layouts and paper 5×5 values, but do not claim a common evaluator.

## H4 — correctness hazards and discovered data issue

1. The archive’s expected AEP values are decimal snapshots; the independent
   Python/C++ tolerance is \(10^{-5}\) MWh.
2. The paper constraint is radius plus 2D minimum spacing. Several public
   participant layouts violate 2D even though they were ranked in the paper.
3. The largest current violations include participant 5 and 7 for the 36- and
   64-turbine problems. They must be reported rather than repaired because
   repairing would change the published comparison asset.
4. Case-study-2 YAML and paper units differ by a factor of 1000 in at least one
   public default field; the paper table in MWh controls the comparison matrix.
5. H6 passes only if all 33 AEP values reproduce the archive within tolerance,
   participant 4 remains rank 1 for all three sizes, and the full worker team is
   observed.
6. No 25-seed formal optimization is applicable to this fixed-layout
   benchmark paper. Its full formal run is the complete deterministic matrix.
