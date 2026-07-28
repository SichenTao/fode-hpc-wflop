# FODE-HPC-WFLOP

FODE-HPC-WFLOP is a reproducible, pure C++20 benchmark for studying
high-performance evolutionary optimization of wind-farm layouts. The current
development branch provides the common FODE-E0-L problem with one exact work
budget and seventeen registered algorithm state machines:

```text
FODE  AGA  SUGGA  ISE  AGPSO  CGPSO  LSHADE  CLSHADE  CEDE  MS-SHADE  BDE  HGPSO  AIGA  CIGA  LSDE  WFADDE  A-LSHADE
```

It also provides a separate pure C++20 GGA package for the eight-site
integrated layout-and-electrical-cable problem from Zhang et al. (2026). That
package minimizes LCOE while evaluating AEP and sector-based inner-array cable
routing. Its repaired physical semantics, source discrepancies, and non-pooling
rule are frozen in `shared/contracts/gga_problem_semantics.json`.

The benchmark contains 50 wind-farm cases: 10 wind scenarios crossed with
farms containing 10, 20, 30, 50, or 80 turbines. One physical function
evaluation (FES) means one complete feasible-layout evaluation over every wind
direction and all 13 wind-speed bins in the selected case. A formal run stops
at exactly 24,000 completed physical FES.

## What is frozen in v0.1.0

- one shared Jensen/Park wake and power evaluator;
- one canonical 50-case contract;
- eight clean-room C++ algorithm implementations on the common problem;
- deterministic, schedule-independent random events;
- one persistent OpenMP team for algorithm-safe population work and evaluation;
- exact physical-FES accounting;
- a 400-run v0.1.0 admission receipt covering the original eight algorithms,
  50 cases, and one seed;
- 504 exact 1-worker/20-worker semantic pairs summarized from 1,008 timing runs.

ISE and CLSHADE are explicitly paper-derived reconstructions because no public
author implementation was available. The other algorithms use the provenance
declared in `shared/contracts/algorithm_provenance.tsv`. CEDE and MS-SHADE use
paper-first profiles because their paper equations and archived MATLAB behavior
diverge; BDE and HGPSO also have paper/source operator, stage, or budget conflicts.
The development branch adds CEDE, MS-SHADE, BDE, HGPSO, AIGA, CIGA, LSDE,
WFADDE, and A-LSHADE after their development gates; these additions are not
part of the v0.1.0 tag. AIGA, CIGA, LSDE, WFADDE, and A-LSHADE are
paper/preprint-derived explicit reconstructions because no author source was
found. Every conflict and reconstruction is listed in
`docs/semantic_discrepancy_ledger.tsv`. NDE is not part of this public
benchmark.

## Build and test

Requirements are CMake 3.20 or newer, a C++20 compiler, POSIX threads, and
OpenMP.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run one full fixed-work optimization:

```bash
OMP_DYNAMIC=FALSE OMP_NUM_THREADS=20 OMP_PROC_BIND=spread OMP_PLACES=threads \
  build/hpc/wflop_cpp/wflop_cpp_hpc \
  --algorithm fode \
  --case WS5tn30 \
  --physical-fes 24000 \
  --seed 20260728 \
  --workers 20 \
  --cases shared/contracts/benchmark_cases.json \
  --models shared/models/sugga_cpp \
  --output results/fode_WS5tn30.json
```

List the canonical algorithm identifiers with
`build/hpc/wflop_cpp/wflop_cpp_hpc --list-algorithms`.

The GGA package uses locally generated snapshots because the upstream
repository has an academic-use notice but no standard redistribution license:

```bash
python3 scripts/prepare_gga_problem_assets.py \
  --source /path/to/WFLO-GGA \
  --output .source-cache/generated/gga_repaired
python3 scripts/validate_gga_problem_assets.py \
  --assets .source-cache/generated/gga_repaired

build/hpc/gga_cpp/gga_cpp_hpc \
  --problem .source-cache/generated/gga_repaired/Netherlands_Egmond_aan_Zee.wfp \
  --physical-fes 3000 \
  --seed 20260316 \
  --workers "$(nproc)" \
  --output results/gga_Egmond.json
```

After a target machine, compiler, and worker count have been frozen, run the
25-seed fixed-work campaign with:

```bash
WFLOP_WORKERS="$(nproc)" bash scripts/run_fixed_work_campaign.sh
```

The script validates and reuses completed per-seed files, so an interrupted
campaign resumes without rerunning accepted work. Results remain untracked
until their machine-specific receipt is reviewed.

The platform also contains the paper-repaired three-objective TWFLO package
from Zhang et al. (2025), exposed as `pbea_cpp_hpc`. It evaluates expected
power, occupied grid area, and construction-plus-land cost for the paper's
20-by-20 grid, WS1/WS2 wind conditions, and 15--30 turbines. The proposed
method is named **MOEA/D-P** in the paper; `PBEA` is retained only as a package
shorthand. The formal unit of work is one completed layout evaluation
returning all three objectives. A 100-individual run containing the initial
population and 100 offspring generations therefore has 10,100 physical FES.

```bash
# Independent three-objective layout evaluation
build/hpc/pbea_cpp/pbea_cpp_hpc \
  --scenario ws2 \
  --evaluate-layout 1,20,21,40,81,100,121,140,181,200,241,260,321,380,400

# Development smoke; formal campaigns use the frozen Waffle manifest
build/hpc/pbea_cpp/pbea_cpp_hpc \
  --scenario ws2 --turbines 30 --population 100 --generations 2 \
  --workers "$(nproc)" --seed 20250729 --ipd 3 --mu-c 80
```

The local author bundle is not vendored: no redistribution license was found,
and 18 files carrying an `.m` suffix are MATLAB binary containers rather than
readable source. Exact hashes and repairs are frozen in the PBEA contracts and
development receipt.

The shared executable exposes the complete six-method comparison set:

```bash
for algorithm in moead_p moead nsgaii mopso morime armoea; do
  build/hpc/pbea_cpp/pbea_cpp_hpc \
    --algorithm "${algorithm}" --scenario ws2 --turbines 30 \
    --population 100 --generations 100 --workers "$(nproc)" \
    --seed 202507290001 --output-front "results/${algorithm}.json"
done
```

`moead_p` uses the paper's IPD3 by default; all six initial probability
distributions remain selectable with `--ipd`. Every algorithm calls the same
independently validated evaluator. Formal non-dominated artifacts include
both objective triples and the exact one-based grid-cell layouts needed for
replay.

## Evidence boundary

`evidence/admission/admission_receipt.json` records the passed v0.1.0
20-worker admission gate with 400 results.
`evidence/performance/performance_receipt.json`
records exact semantic agreement for all 504 paired runs. The timing evidence
also shows why parallel speedup must be reported by workload: on the archived
Spark2 probe, large `WS10tn80` cases generally benefited from 20 workers while
some small cases were slower because thread synchronization dominated useful
work. This repository therefore does not claim one universal speedup.

The previously completed 10,000-run research campaign is not bundled in this
release because its final receipt has not yet been synchronized into this
public repository. A smoke test, a source hash, or a timing probe is not
presented as a paper-level result.

For the GGA development package, the eight repaired problem snapshots pass
boundary, spacing, probability, cable-capacity, and hash validation. An
independent Python/SciPy oracle reproduces fixed-layout C++ AEP, cable cost,
capacity factor, and LCOE for all eight sites. A one-worker/twenty-worker pair
has exact semantic agreement, and the heaviest-site smoke passes address and
undefined-behavior sanitizers. These are development admission results; the
25-seed Waffle campaign remains pending.

For the three-objective MOEA/D-P development package, an independent Python
scalar oracle covers both wind scenarios and three contrasting layouts. The
one-worker/four-worker scientific outputs match exactly, the 30-turbine WS2
case passes address and undefined-behavior sanitizers, and the largest
development smoke completes. A five-repeat Spark diagnostic measured
9.58x evaluator and 2.61x end-to-end speedup from one to twenty workers for
1,100 complete three-objective layout evaluations. This is not a
MATLAB-to-C++ comparison and not a formal Waffle result.

## Expansion contract

The next milestone covers every directly relevant public WFLOP paper signed by
Shangce Gao or Sichen Tao. Each paper package must pass five gates:

1. **R0 — identity:** DOI, authors, paper, source, license, and hashes;
2. **R1 — problem:** objective, constraints, wake model, sampling, and FES;
3. **R2 — algorithm:** equations, parameters, stage order, and randomness;
4. **R3 — reproduction:** source or paper-derived reference behavior;
5. **R4 — HPC:** pure C++ optimization, semantic tests, scaling, and formal run.

Missing papers or source code are registered as explicit states rather than
silently guessed. See `docs/author_lineage_registry.tsv` and
`docs/roadmap.md`. The 2026-07-29 scope freezes 23 unique DOI records and
automatically checks the Jiayi Li, Baohang Zhang, Chen Zhang, Yuhang Ma, and
Jiaru Yang coauthor lines named during scope review. The minimum is append-only:
future official-homepage records can expand it.

## Licensing

Repository code is licensed under Apache-2.0. Owner-created and redistributable
documentation/data explicitly listed in `DATA_LICENSE.md` are licensed under
CC BY 4.0. Third-party papers and source archives are not redistributed; see
`THIRD_PARTY.md`.
