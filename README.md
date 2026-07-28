# FODE-HPC-WFLOP

FODE-HPC-WFLOP is a reproducible, pure C++20 benchmark for studying
high-performance evolutionary optimization of wind-farm layouts. The current
release freezes one common physical problem, one exact work budget, and twelve
registered algorithm state machines:

```text
FODE  AGA  SUGGA  ISE  AGPSO  CGPSO  LSHADE  CLSHADE  CEDE  MS-SHADE  BDE  HGPSO
```

The benchmark contains 50 wind-farm cases: 10 wind scenarios crossed with
farms containing 10, 20, 30, 50, or 80 turbines. One physical function
evaluation (FES) means one complete feasible-layout evaluation over every wind
direction and all 13 wind-speed bins in the selected case. A formal run stops
at exactly 24,000 completed physical FES.

## What is frozen in v0.1.0

- one shared Jensen/Park wake and power evaluator;
- one canonical 50-case contract;
- twelve clean-room C++ algorithm implementations on the common problem;
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
diverge; BDE and HGPSO also have paper/source operator, stage, or budget conflicts. Every conflict is listed in
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

After a target machine, compiler, and worker count have been frozen, run the
25-seed fixed-work campaign with:

```bash
WFLOP_WORKERS="$(nproc)" bash scripts/run_fixed_work_campaign.sh
```

The script validates and reuses completed per-seed files, so an interrupted
campaign resumes without rerunning accepted work. Results remain untracked
until their machine-specific receipt is reviewed.

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
