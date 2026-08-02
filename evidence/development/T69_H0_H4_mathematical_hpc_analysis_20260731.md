# T69 H0-H4 mathematical and HPC analysis

Paper: Feng and Shen, *Wind Farm Power Production in the Changing Wind:
Robustness Quantification and Layout Optimization*, DOI
`10.1016/j.enconman.2017.06.005`.

## H0 - paper problem and computational graph

The decision is the `2 x 80` continuous Horns Rev 1 coordinate array. A
feasible random-search event replaces one turbine position inside the source
parallelogram while retaining pairwise distance at least `5D=400 m`.

For every layout, the paper requires the complete power surface
`P(theta,v)` on 360 one-degree directions and 23 one-metre-per-second cells
from 3 through 25 m/s. Jensen RSS wake composition costs `O(360 N^2)` from
scratch. PSRI uses the eight-cell stencil on the normalized cyclic surface.
VoP integrates PSRI with the local joint wind PDF. Long-term robustness uses
1000 perturbed 35-parameter distributions. The three objective families are
the five alpha, five beta and five gamma cases, each with 10000 complete
feasible layout evaluations.

## H1 - measured and asymptotic bottlenecks

A naive formal campaign repeats a full `360 x 80 x 79` wake construction and
a `1000 x 360 x 23` Monte Carlo dot product for every trial. The first term
ignores that only one turbine moved; the second ignores that the selected
piecewise joint PDF has only 12 sector functions. Both are mathematically
redundant. Per-evaluation thread creation would also dominate the short
stencils and reductions.

## H2 - exact reformulations

1. Store the direction-by-target sum of squared Jensen influence terms.
   Moving turbine `m` changes only row/column contributions involving `m`, so
   the exact update costs `O(360 N)` rather than `O(360 N^2)`.
2. Build the 360-by-23 power surface from the updated tensor. The V80 thrust
   coefficient depends on free wind speed, but the geometric Jensen influence
   does not; therefore geometry is reused across every speed cell.
3. Aggregate the 30 one-degree power values inside each of the 12 sectors.
   Every base or perturbed piecewise joint distribution is then an exact dot
   product over `12 x 23`, not `360 x 23`.
4. Precompute one common counter-keyed bank of 1000 lambda-PDF scenario weight
   vectors. This makes the optimization objective deterministic and prevents
   layout-dependent Monte Carlo noise.
5. Keep the literal paper VoP quadrature. Because Eq. (4) omits the Jacobian
   after nondimensionalizing speed and direction, the physical probability
   mass is divided by `22 x 360`; this is what yields the paper's `1e-4` VoP
   scale. The ratio-based short robustness is unaffected.

## H3 - parallel schedule and scientific identity

One persistent C++20 thread team owns a trajectory. Full and incremental wake
tensors, power-surface directions, PSRI directions and Monte Carlo scenarios
write disjoint fixed indices in parallel. All sums, objective comparisons and
incumbent commits occur in fixed index order. Counter-keyed candidate events
do not depend on worker scheduling. Consequently one-worker and all-worker
fixed-work runs must produce identical layouts, metrics, FES and science hash.

## H4 - production protocol and evidence boundary

The production binary uses every allocated Waffle CPU core. H5 independently
checks equations, paper anchors, all 15 roles, both Table-3 conflict profiles
and one/all-worker identity. H6 measures one worker against every available
Waffle worker for short, long and overall objectives at identical work. Only
the fastest admitted all-core configuration runs the formal campaign.

The paper states `beta=0.95`, while its reported `R_long=4.750` equals
`sqrt(78.54/3.482)` and hence uses `beta=0.5`. The platform executes and labels
both the equation-declared and Table-3-compatible overall profiles. It also
reports literal 8770-hour and calendar-correct 8760-hour AEP. The unavailable
three-year time series is registered but is not replaced by invented data.

Local pre-admission candidates on Spark use 1000 physical evaluations and
identical one/twenty-worker seeds, scenarios, layouts, metrics and hashes.
End-to-end speedups are `9.909x` for short, `7.856x` for long and `8.380x`
for overall robustness; corresponding wake speedups are `4.226x`, `4.418x`
and `4.219x`, and metric speedups are `10.947x`, `9.085x` and `9.017x`.
These are local candidate values pending immutable Waffle H6 confirmation.
