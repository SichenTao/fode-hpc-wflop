# T11 BlockCopy mathematical and HPC analysis

Status: implemented and H5 equation-validated on 2026-07-31.

## H0 — paper-pair scope

The target pair is the paper's proposed BlockCopy mutation/crossover and its
four native continuous fixed-count WFLOPs. The target execution matrix is four
BlockCopy-containing ES configurations by four problems by 30 independent
runs, with 2,000 complete wind-farm evaluations per run. TDA and the
perturbation-only ES are comparison baselines, not proposed assets, and do not
gate this package.

The primary PDF, DOI, public WindFLO revision, exact consumed-file hashes,
same-author thesis, missing fields, conflicts, completions and claim boundary
are registered in `shared/contracts/core99_t11_blockcopy_2016.json` and at the
top of every T11 source unit.

## H1 — mathematical problem

For a fixed layout \(X=\{(x_i,y_i)\}_{i=1}^N\), each paper problem minimizes

\[
 C(X)=
 \frac{
 (c_tN+c_s\lfloor N/m\rfloor)
 (2/3+\exp(-0.00174N^2)/3)+c_{OM}N
 }{
 ((1-(1+r)^{-y})/r)\,8760\,P(X)
 }+\frac{0.1}{N}.
\]

The released evaluator computes \(P(X)\) over 24 direction sectors. For each
target turbine and sector it sums squared Jensen deficits from all source
turbines, scales the sector's Weibull distribution by one minus the square
root of that sum, and integrates the released linear/rated power curve.
Coordinates must lie inside the rectangular site, outside every open
rectangular obstacle, and at least \(8R=308\) m apart.

The four cases preserve the paper dimensions, fixed counts and BlockCopy
grids. Competition cases consume the official 2014 XML arrays rather than
digitizing the paper wind roses.

## H2 — algorithm semantics

BlockCopy removes the target block, translates the source block's relative
coordinate pattern, omits copied turbines that would violate a constraint,
then restores the fixed count by uniform purge or feasible uniform addition.
Mutation uses one layout; crossover copies a donor block into a base parent.

The two single-solution methods use a `(1+1)` lifecycle. The `Both` method
chooses BlockCopy or ten-turbine random perturbation with equal probability.
The two population methods use the conventional `(5,10)` comma lifecycle:
five parents generate ten children and the best five children survive.
This last rule is an explicit completion because the paper reports the
population notation and operators but omits an executable survivor algorithm.

One full objective call is one physical FES. Repair, constraint prescreening,
lookup access and comparison do not create hidden FES.

## H3 — full-evaluator analysis

The released wake-membership test uses an `acos` angle:

\[
\beta=\arccos\frac{d_\parallel+R/k}
{\sqrt{(d_x+(R/k)\cos\theta)^2+(d_y+(R/k)\sin\theta)^2}}<\arctan k.
\]

Algebra gives the equivalent branch

\[
 R+k d_\parallel>0,\qquad
 |d_\perp|<R+k d_\parallel.
\]

The C++ hot path uses this exact cone inequality, eliminating one square root
and one inverse cosine per turbine pair. Direct Weibull/power integration is
compiled into a direction-specific lookup over the squared aggregate deficit.
An independent Python oracle retains the original `acos` and direct
quadrature. Across all four cases, the maximum observed relative difference
is below \(2\times10^{-7}\).

Full recomputation remains \(O(SN^2)\), with \(S=24\). Its independent
direction/turbine tasks run on the shared persistent CPU team.

## H4 — BlockCopy-specific incremental analysis

A BlockCopy child differs from its base parent only in a small target pattern
and the fixed-count repair. Let \(K\) be the number of removed plus added
coordinates. The parent's per-direction, per-target squared-deficit state is
retained.

For an unchanged target, the child state subtracts every removed source
contribution and adds every new source contribution. Only newly added targets
are recomputed against all child sources. This changes the dominant work from
\(O(SN^2)\) to \(O(SNK)\), while retaining the complete paper objective at
every physical FES. Exact coordinate lineage is recovered with bitwise
coordinate keys; all reductions for a target remain in deterministic order.

Single-trajectory execution parallelizes direction/turbine work across all
requested cores. Formal 30-run campaigns instead assign independent
trajectories to all cores, avoiding nested oversubscription and making the
paper's run-level parallelism dominant. The runtime records evaluator,
algorithm and end-to-end time separately.

## Admission evidence

- `core99_t11_cpp`: structural, exact-FES, incremental and schedule tests.
- `core99_t11_h5`: independent equations, four-by-four lifecycle smoke,
  full/incremental equivalence, all-core science identity and replay.
- `evidence/development/T11_h5_independent_equation_validation_20260731.json`:
  machine-readable H5 receipt.
- H6 and the 480-run paper campaign are produced by
  `scripts/run_core99_t11_h6_formal.py`.
