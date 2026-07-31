# T60 mathematical and HPC analysis (H0-H4)

Paper: Feng and Shen, *Solving the Wind Farm Layout Optimization Problem
Using Random Search Algorithm*, DOI `10.1016/j.renene.2015.01.005`.

This document controls the high-performance implementation in
`hpc/core99_cpp/src/feng_t60.cpp`. It records the mathematical dependency
analysis before performance claims are admitted.

## H0: mathematical dependency graph

For direction \(s\), source turbine \(j\), and target turbine \(i\), define

\[
d^{\parallel}_{sji} =
\sin\theta_s(x_i-x_j)+\cos\theta_s(y_i-y_j),
\quad
d^{\perp}_{sji} =
\left|\cos\theta_s(x_i-x_j)-\sin\theta_s(y_i-y_j)\right|.
\]

If \(d^{\parallel}_{sji}\leq0\), the influence is zero. Otherwise the paper
defines the geometric squared influence

\[
q_{sji} =
\left(\frac{A_{\mathrm{overlap}}(R+k d^\parallel_{sji},R,
d^\perp_{sji})}{\pi R^2}\right)^2
\left(1+\frac{k d^\parallel_{sji}}{R}\right)^{-4}.
\]

For target \(i\), \(Q_{si}=\sum_{j\ne i}q_{sji}\). At free-stream speed
\(u\), the effective speed is

\[
u_{si}^{\mathrm{eff}} =
u\max\left(0,1-\left(1-\sqrt{1-C_T(u)}\right)\sqrt{Q_{si}}\right).
\]

Expected farm power is the fixed-order sum of turbine power over speed and
direction probabilities. A complete evaluation from arbitrary coordinates
therefore costs \(O(SN^2+SB N)\), where \(S\) is the direction count and
\(B\) the speed-bin count.

Algorithm 1 moves exactly one turbine \(m\). Only the tensor source column
\(q_{sm i}\) and target row \(q_{sj m}\) can change. All other pair terms are
invariant. A candidate can therefore be evaluated in \(O(SN+SN)\), and its
feasibility can be checked against the other turbines in \(O(N)\), without
changing the mathematical objective.

The search trajectory itself is sequential because acceptance at event
\(t+1\) depends on acceptance and the remembered turbine/direction at event
\(t\). Safe parallel regions are wind directions within a fixed full
evaluation and independent paper runs. The implementation does not convert
this active-state random search into a different synchronous method.

## H1: data organization

- Wind angles store their sine and cosine once; trigonometric functions are
  absent from the source-target hot loop.
- Wind-speed distributions are grouped into one profile for ideal Cases 1-2,
  36 profiles for ideal Case 3, and 12 profiles for Horns Rev.
- Layouts and squared-influence sums use contiguous direction-major storage.
- Horns boundaries use a two-vector affine representation, making boundary
  checks constant-time and preserving the paper parallelogram.
- Counter-keyed random events assign every turbine, direction and step draw to
  a logical proposal index, independent of worker scheduling.

## H2: mathematical precomputation

The speed-bin integral for one turbine depends on a layout only through
\(r=\sqrt{Q_{si}}\). For each unique wind profile, the implementation
precomputes the expected turbine power on 32,769 uniformly spaced values of
\(r\), not \(Q\). This choice removes the square-root singularity at \(Q=0\)
from linear interpolation. H5 measured relative power errors of
`1.213e-09` for ideal Case 1 and `3.656e-07` for the 12-sector Horns layout.

Analytic partial-circle overlap contains two `acos` operations. A shared
1,025-by-1,025 table in normalized wake-radius/crosswind coordinates replaces
those transcendental calls in the hot path. Full containment and disjoint
circles retain exact branch results; only the partial-overlap band is
bilinearly interpolated. The independent H5 oracle uses analytic overlap.

## H3: algorithm-specific acceleration

For a feasible candidate moving turbine \(m\):

1. For target \(m\), recompute \(\sum_{j\ne m}q_{sjm}\).
2. For every other target \(i\), subtract the old \(q_{smi}\) and add the new
   \(q'_{smi}\).
3. Integrate power from the updated \(Q'_{si}\) through the profile lookup.
4. Commit the candidate layout and sums only after strict improvement.

The implementation also checks only the moved coordinate against the
boundary and \(N-1\) pair distances. The first smoke implementation
incorrectly repeated the full \(O(N^2)\) constraint scan for every infeasible
trial; the admitted implementation removes that avoidable work.

The paper leaves a permanently outward remembered ray undefined. Without a
completion, a boundary acceptance can cause an infinite feasibility loop.
The implementation clears the remembered-ray flag after an infeasible
proposal, matching the platform reconstruction of the cited predecessor.
This fact is recorded at the top of the source and in the controlling JSON.

## H4: CPU execution topology

- A single active trajectory uses the \(O(SN)\) incremental evaluator; adding
  a barrier after every FES would cost more than it saves.
- A fixed-layout full evaluation assigns independent directions to the shared
  persistent worker team.
- Formal campaigns assign independent runs to all 20 Waffle cores. Each run
  remains single-threaded internally, so there is no nested oversubscription.
- Output slots are indexed by paper run number. Fixed-order reductions and
  counter-keyed randomness make one-core and all-core scientific hashes
  identical.
- H6 separately reports full-evaluator, incremental-evaluator and independent
  campaign speedups. It does not relabel C++ language translation as the
  algorithmic HPC contribution.

## Admission boundary

H0-H4 are admitted. H5 passed the independent equation, incremental/full,
six-case, FES and worker tests. H6 and the 840-run formal campaign must run
from an immutable Waffle source snapshot before the dossier can advance from
`h5_admitted` to `formal_complete`.
