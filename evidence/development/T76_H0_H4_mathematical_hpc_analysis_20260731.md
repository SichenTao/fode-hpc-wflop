# T76 H0--H4 mathematical and HPC analysis

## Scope and authority

- Corpus: `T76`
- DOI: `10.1016/j.energy.2018.11.073`
- Primary authority: the hashed local paper PDF.
- Target source status: exact-title, DOI, author, GitHub, laboratory, and
  paper-link searches found no target code or native arrays.
- Completion and claim boundary:
  `shared/contracts/core99_t76_sun_2019.json`.

This is an academic flexible reconstruction. It does not claim author-code,
author-random-stream, native manufacturer-curve, Sha Chau hourly-record, or
numerical-optimum replay.

## H0: paper problem and missing-data audit

The paper defines six problem roles in a 4 km by 4 km square:

1. Case 1 evaluates aligned 48-turbine E-82 layouts under omnidirectional and
   directional spacing restrictions.
2. Case 2 optimizes the corresponding two uniform-turbine problems with MPGA.
3. Case 3 optimizes 45 turbines, nine of each of five ENERCON types, under a
   concentrated north wind and type-dependent hub-height inflow.
4. Case 4 optimizes the same heterogeneous farm under the Sha Chau joint wind
   climate.

The target source, numerical manufacturer curves, axial-induction values,
wake-decay value, complete MPGA settings, Sha Chau hourly array, and optimum
coordinates are unavailable. The controlling contract records every
deterministic completion. The paper's own conflicts over E-44 hub height,
wind-shear exponent, directional width, and “individual” terminology are
resolved from its tables, printed numerical outputs, figures, and cited MPGA
lineage.

## H1: mathematical decomposition

For wind state \(s\) and turbine type \(t\), hub-height inflow is

\[
U_{s,t}=U_{s,\mathrm{ref}}
\left(\frac{H_t}{H_{\mathrm{ref}}}\right)^\alpha,
\]

where \(U_{s,\mathrm{ref}}\) is reference speed, \(H_t\) is hub height,
\(H_{\mathrm{ref}}\) is reference height, and \(\alpha\) is the paper-case
wind-shear exponent.

For every upstream source \(j\) and target \(i\), the wake radius is

\[
r_{w,j}=r_j+kx_{ji},
\]

where \(r_j\) is source rotor radius, \(x_{ji}\) is downwind distance, and the
missing Jensen expansion coefficient is completed with the lineage-standard
\(k=0.075\). Exact circle intersection supplies the partial-overlap fraction
\(q_{ji}\). Multiple deficits combine by root-sum-square:

\[
U_i=U_{s,t_i}\left[1-
\sqrt{\sum_j\left(
\frac{2a_jr_j^2}{r_{w,j}^2}q_{ji}
\right)^2}\right],
\]

where \(a_j\) is the type-specific axial-induction completion derived from
the published power-coefficient anchor.

The missing manufacturer curve is completed by a monotone cubic energy-law
interpolation through every published Case-3 power anchor and the rated point.
The objective is expected farm power \(\sum_s p_s\sum_iP_i(U_i)\), where
\(p_s\) is wind-state probability. A physical function evaluation (FES) is one
complete restriction, wake, power, and feasibility evaluation of one layout.

## H2: complexity and parallel structure

Let \(N\) be turbine count, \(D\) the number of unique wind directions, and
\(S\) the number of joint wind states. One layout requires
\(O(DN^2+SN)\) work. Case 4 has \(N=45\), \(D=36\), and \(S=972\).

MPGA uses ten demes with 20 chromosomes each. One elite per deme is retained,
so every generation evaluates 190 offspring. The complete 5000-generation
H6 trajectory consumes

\[
200+5000(190)=950200
\]

physical layout evaluations.

Independent within-run work comprises population initialization, offspring
construction, and complete layout evaluation. Generation stopping, ordered
elite selection, and ring migration remain deterministic control dependencies.

## H3: high-performance implementation

The production backend is pure C++20 CPU-HPC. One persistent all-core worker
team is created per optimization and reused across every generation. Counter-
keyed random events and fixed-index writes preserve identical one/all-core
scientific trajectories.

The evaluator performs the following problem-specific optimizations:

- wind states are grouped by direction so pair geometry and wake deficits are
  computed once and reused across all speed bins;
- hub-height ambient speeds and no-wake powers are precomputed for every
  state/type pair;
- axial induction is solved once per turbine type rather than inside the
  turbine-pair loop;
- exact overlap and root-sum-square wake physics remain intact;
- whole-layout evaluations and offspring construction run across all cores;
- bit mutation uses geometric skipping, avoiding a full scan of every bit.

## H4: fixed-work performance and paper-anchor evidence

The fixed Case-1 checks use the paper layouts and published E-82 power anchor:

| Quantity | Paper | Reconstruction | Relative difference |
|---|---:|---:|---:|
| Directional total output | 38.68 MW | 38.642 MW | -0.10% |
| Directional minimum turbine | 787 kW | 785.90 kW | -0.14% |
| Omnidirectional total output | 32.88 MW | 33.602 MW | +2.20% |
| Theoretical no-wake output | 40.18 MW | 40.176 MW | -0.01% |

The complete-work Spark development admission used the same Case-2
directional problem, seed, 950200 physical layout evaluations, and scientific
trajectory:

| Stage | 1 worker | 20 workers | Speedup |
|---|---:|---:|---:|
| Physical wind-farm evaluation | 24.687931 s | 2.018265 s | 12.232x |
| Algorithm construction/control | 1.260159 s | 0.461048 s | 2.733x |
| End to end | 25.948090 s | 2.479313 s | 10.466x |

The one- and 20-worker best layout, evaluation, physical FES, and scientific
hash were identical. These are Spark development-admission values. The queued
Waffle H6 campaign is the authoritative all-core performance measurement.
