# FODE-E0-L native parallel design

## Frozen scientific task

For case \(c=(N_d,N_v,N_t)\), FODE maximizes the expected aggregate turbine
power

\[
P(x)=\sum_{d=1}^{N_d}\sum_{v=1}^{13}p_{d,v}
      \sum_{j=1}^{N_t}\mathcal P((1-\delta_{d,j}(x))v),
\]

where \(x\) is a feasible set of \(N_t\) grid cells, \(\delta\) is the
source-defined Jensen/Park wake deficiency, and \(\mathcal P\) is the archived
piecewise turbine power curve. One physical FES evaluates this entire sum.

The implemented FODE identity retains \(N_0=|77-N_t|\), fractional order
\(a=0.8\), p-best rate 0.11, archive rate 1.4, memory depth 5, source-compatible
repair, LSHADE memory adaptation, linear population reduction and the optional
local-search event.

## Work and dependencies

The archived MATLAB loop recomputes the speed-independent wake field inside
every wind-speed bin. Its literal work is
\(\Theta(BN_dN_vN_t^2)\). The native evaluator proves from the equations that
the Jensen/Park deficiency depends on layout and direction but not on wind
speed, computes it once, and then applies all 13 power/probability bins:

\[
W_{\mathrm{native}}
=\Theta\!\left(BN_d(N_t^2+N_vN_t)\right).
\]

This is an exact common-subexpression elimination, not a reduced physical
model. Frozen MATLAB anchors have zero observed absolute error for the three
tested layouts. Independent work units are a direction state for each
`(layout,direction)`, a wake calculation for each
`(layout,direction,downstream turbine)`, and a final power accumulation for
each `(layout,turbine)`. All reductions retain a fixed serial arithmetic order.

FODE mutation, both fractional-order combinations, crossover and repair cost
\(\Theta(N_gN_tM)\). Candidate coordinates are computed independently from a
read-only generation snapshot. Selection flags are also independent.

Archive update, memory-parameter update, population reduction and global-best
commit are short ordered control sections. Parallelizing them would not add
meaningful width and could change the state machine.

## Seventeen-stage implementation ledger

| Stage | Mathematical role | HPC treatment |
|---|---|---|
| setup and initial population | sample feasible cell sets | individuals parallel |
| initial evaluator | complete \(N_d\times13\) objective | 20-thread native evaluator |
| ranking and snapshot | freeze generation state | stable ordered control |
| parameter sampling | \(CR_i,F_i,r_1,r_2,p\)-best | individuals parallel |
| fractional mutation and crossover | two depth-five fractional differences | individual-coordinate parallel |
| repair | ceil, bounds, duplicates, unavailable cells | individuals parallel |
| population evaluator | full child batch | layout-direction-turbine parallel |
| best update | maximum expected power | fixed-order commit |
| selection flags | parent/child comparisons | individuals parallel |
| archive update | unique accepted parents and capacity | ordered control |
| selection and memory adaptation | SHADE weighted Lehmer means | parallel copy, ordered reduction |
| history shift | four prior difference generations | contiguous moves |
| population reduction | source LPSR rule | stable ordered control |
| progress ledger | legacy counter plus physical FES | ordered integer accounting |
| local generation and repair | source optional offspring | coordinate parallel |
| local evaluator | one full layout | fine-grained direction-turbine parallel |
| finalization | result and timing contract | ordered control |

OpenMP creates the maximum 20-worker team for every parallel region and libgomp
retains its worker pool between regions. A bounded uncontended campaign
observed 1995% process CPU across a 20-logical-CPU affinity domain. The
publication claim is therefore full-affinity intra-run utilization, not the
stronger and unnecessary claim that the whole state machine is one lexical
OpenMP parallel region.

## Thread-safe random contract

Every stochastic event is keyed by
`(seed,generation,phase,individual,coordinate,draw)`. Therefore a candidate
receives the same random values regardless of which OpenMP worker executes it.
The laws used by FODE remain uniform, normal and Cauchy; cross-language
bit-for-bit agreement with MATLAB's random stream is neither claimed nor
required for mathematical reproduction.

## Precision and performance claims

The copied mathematical equations and fixed-order evaluator reductions are
the precision claim. The complete native endpoint is compared directly with
the archived MATLAB endpoint at the same physical-FES definition. Language
porting, exact wake reuse and 20-thread intra-run parallelism are reported
together as the final system acceleration; intermediate Python endpoints are
excluded. The optional `--profile-phases` mode instruments the 17 stages and
is never used for headline production timing.
