# T85 H0-H4 mathematical and HPC analysis

## Evidence and reproduction boundary

The controlling paper is Song, Kim, and You, *Renewable Energy* 206 (2023)
738-747, DOI `10.1016/j.renene.2023.02.058`, local PDF SHA-256
`898e441b2d92c3c56d70dc3a6dd89ee27730550ac00e0905de8d7c02ca19ebc5`.
Its complete arXiv manuscript source (`2210.02084`) was inspected. Exact-title,
DOI, author, arXiv-ID, and GitHub searches found no paper-linked optimizer,
wake evaluator, case-data, or result-replay program.

The paper delegates the full AGLDPSO transition to Wang et al., DOI
`10.1109/TCYB.2020.2977956`. That public primary paper provides Eqs. (3)-(9),
Algorithm 1, and the controlling parameters, but no public implementation was
located. The resulting package is a paper-and-cited-predecessor academic
reconstruction. All missing fields and completions are frozen in
`shared/contracts/core99_t85_song_2023.json`; it is not author-code or an
author-number replay.

## H0: mathematical work graph

For one joint solution with `N` turbines, `S=8` wind states and `Q=8`
equal-area rotor samples, the coupled evaluator performs:

1. `O(SN log N)` stable downstream ordering;
2. `O(SN^2Q)` three-dimensional yawed wake propagation and linear
   local-inlet deficit superposition;
3. `O(SNQ)` rotor averaging, turbine-curve interpolation, yaw power loss,
   and probability-weighted AEP reduction.

The six paper cases use `N=25` except WF3, which uses `N=36`. Joint decision
dimensions are therefore 250 and 360. One physical FES means one complete
layout-and-eight-yaw-schedules evaluation over all eight winds.

Within a wind state, the local inlet of every upstream turbine controls both
its thrust coefficient and the amplitude of its downstream deficits. This
causal sweep cannot be arbitrarily parallelized. Complete particles and
AGLDPSO subpopulations are independent and form the safe, sufficiently large
parallel axes.

## H1: semantic and numerical reformulation

- Precompute the eight wind rotations, digitized turbine tables, and
  equal-area rotor quadrature.
- Rotate each layout once per wind, then use one stable upstream sweep.
- Preserve the paper's Eq. (5) linear superposition using every upstream
  turbine's already-computed local inlet velocity.
- Normalize position and yaw coordinates to `[0,1]` only for LSH projection;
  optimization and physical evaluation remain in metres and degrees.
- Use fixed-order rotor, wake, power, and AEP reductions with floating-point
  contraction disabled.
- Count only complete eight-wind layout evaluations as physical FES.

## H2: parallel architecture

One optimization owns one persistent C++ worker team. The team parallelizes:

- initialization, velocity construction, and feasibility repair for all 500
  particles;
- normalized LSH projection for all particles;
- independent worst-particle updates across adaptive subpopulations;
- complete-layout evaluation across updated candidates;
- independent subpopulation replacement.

Each layout's downstream sweep remains serial, preventing nested
oversubscription. Counter-keyed random events assign every initialization,
LSH, shuffle, inertia, and acceleration draw to a logical event rather than
a worker, so scheduling cannot change the scientific trajectory.

## H3: implementation traceability

- Paper/model and optimizer API:
  `hpc/core99_cpp/include/core99/song_t85.hpp`
- Pure-C++ yawed evaluator and AGLDPSO:
  `hpc/core99_cpp/src/song_t85.cpp`
- Machine-readable command:
  `hpc/core99_cpp/src/song_t85_main.cpp`
- Structural and deterministic-parallel tests:
  `hpc/core99_cpp/tests/song_t85_test.cpp`
- Independent equation oracle:
  `scripts/validate_core99_t85.py`
- Resumable H6/formal runner:
  `scripts/run_core99_t85_h6_formal.py`

Every implementation unit points to the full fact declaration, which records
the public assets, cited predecessor, missing values, completion decisions,
semantic IDs, and claim boundary.

## H4: bounded performance and scientific-equivalence evidence

Spark candidate measurements used the same Release binary, seed `20260731`,
WF1, population 500, and 10,000 complete joint-layout evaluations:

| quantity | one worker | twenty workers | one-to-twenty speedup |
|---|---:|---:|---:|
| coupled evaluator | 8.574964 s | 0.744221 s | 11.522x |
| algorithm and AGLDPSO orchestration | 0.247158 s | 0.094842 s | 2.606x |
| end to end | 8.828108 s | 0.890368 s | 9.915x |

Both executions produced scientific hash `6200f4fd71981cf4`. All twenty
workers participated. The candidate best AEP was `174.703652 GWh`; the paper
reports `174.74 GWh` for WF1 joint AGLDPSO. This is a strong scale-consistency
check, not a claim of author-number replay.

These are Spark development-candidate measurements. Waffle H6 must repeat
the identical one/all-twenty comparison before formal performance admission.
