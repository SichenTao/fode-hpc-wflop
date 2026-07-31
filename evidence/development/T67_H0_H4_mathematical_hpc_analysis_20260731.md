# T67 H0--H4 mathematical and HPC analysis

## Scope and authority

- Corpus: `T67`
- DOI: `10.1016/j.renene.2016.10.038`
- Primary authority: the hashed local paper PDF.
- Target source status: no author MATLAB source or complete 61-turbine data
  were located by exact-title, DOI, author, GitHub, and laboratory searches.
- Completion and claim boundary:
  `shared/contracts/core99_t67_abdulrahman_2017.json`.

This is an academic flexible reconstruction. It does not claim author-code,
author-random-stream, native commercial-curve, or numerical-result replay.

## H0: paper problem and missing-data audit

The paper defines two mixed-variable classes:

1. A six-turbine line with six continuous downwind locations, six integer
   turbine codes, and six integer hub heights.
2. Fixed 18-turbine array and staggered layouts with 18 integer turbine codes
   and 18 integer hub heights.

The paper crosses three spacing multipliers, three reference speeds, two
terrain roughnesses, and three separately optimized objectives. Including the
three layout roles gives 162 target problem roles.

The complete 61 commercial-turbine arrays and all fitted coefficients are
unavailable. Six selected rows are printed. The implementation therefore
freezes those six rows exactly and declares the deterministic completion of
the remaining data in the controlling contract and every implementation unit.

## H1: mathematical decomposition

For turbine \(i\), the undisturbed speed at hub height \(H_i\) is

\[
U_{0,i}=U_{\mathrm{ref}}\,
\frac{\ln(H_i/z_0)}{\ln(H_{\mathrm{ref}}/z_0)}.
\]

For every upstream turbine \(j\), the Frandsen-expanded initial wake radius,
roughness-dependent expansion, exact circle-overlap area, and Jensen deficit
are computed. Multiple deficits combine by root-sum-square. The resulting
speed feeds the declared fifth-degree commercial power completion.

The three scalar objectives are:

\[
\max \sum_i P_i,\qquad
\max \frac{\sum_iP_i}{\sum_iP_{r,i}},\qquad
\min \frac{\mathrm{TCI}}{\sum_iP_i}.
\]

The physical FES unit is one complete evaluation of all wake, power, cost,
and feasibility terms for one layout.

## H2: complexity and parallel structure

For \(N\) turbines, one layout evaluation has \(O(N^2)\) upstream-pair work.
One GA generation evaluates 243 offspring after preserving 13 elites from a
population of 256. The full 3000-generation trajectory consumes

\[
256+3000(243)=729256
\]

complete physical layout evaluations.

Independent work within one optimization:

- population initialization;
- parent selection and mixed-variable offspring construction;
- complete layout evaluations.

Serial dependencies retained for scientific semantics:

- generation stopping;
- stable fitness ordering;
- ordered elite commitment.

## H3: high-performance implementation

The production backend is pure C++20 CPU-HPC. A persistent worker team is
created once per optimization and reused across every generation. Counter-keyed
random events make each logical draw independent of thread scheduling.
Parallel stages use fixed-index writes; population ordering and elite commits
remain deterministic.

The evaluator is parallelized across complete layouts, where each task retains
the entire \(O(N^2)\) physical model. This granularity is large enough to
amortize scheduling overhead. Offspring construction is also parallel, while
the small generation-control and stable-ordering steps remain serial.

## H4: expected and observed bottlenecks

The dominant serial baseline cost is the physical evaluator. The fixed-work
Spark development admission used the same problem, seed, 729256 physical
layout evaluations, and scientific trajectory:

| Stage | 1 worker | 20 workers | Speedup |
|---|---:|---:|---:|
| Physical wind-farm evaluation | 3.473037 s | 0.288961 s | 12.019x |
| Algorithm construction/control | 0.443295 s | 0.435021 s | 1.019x |
| End to end | 3.916331 s | 0.723982 s | 5.409x |

The one- and 20-worker best decision, evaluation, physical FES, and scientific
hash were identical. These are development-host admission values. The queued
Waffle H6 campaign is the authoritative all-core performance measurement.
