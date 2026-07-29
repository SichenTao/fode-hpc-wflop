# FODE-HPC-WFLOP

FODE-HPC-WFLOP is a reproducible, pure C++20 benchmark for studying
high-performance evolutionary optimization of wind-farm layouts. The current
development branch provides the common FODE-E0-L problem with one exact work
budget and eighteen registered algorithm state machines:

```text
FODE  AGA  SUGGA  ISE  AGPSO  CGPSO  LSHADE  CLSHADE  CEDE  MS-SHADE  BDE  HGPSO  AIGA  CIGA  LSDE  WFADDE  A-LSHADE  PPGA
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

PPGA is an explicitly identified algorithm transfer to the common FODE-E0
problem. It does not replace or reproduce the unavailable Nantong 3D terrain
study. Its conventional completions and problem boundary are frozen in
`shared/contracts/ppga_fode_e0_transfer_execution_contract.json`.

The original ALGA, TAAE, RLPSO, and RL-FODE identifiers are deliberately not
executable. ALGA, TAAE, and RL-FODE lack required problem or learned-state assets. The
official RLPSO source is present, but it creates a fresh unseeded PPO policy on
every outer iteration, performs up to 10,000 unreported complete-layout
evaluations per call, and executes an update that conflicts with the paper PPO
equation. Attempts to run these identifiers fail with method-specific R1/R2
evidence. The removal conditions for these guards are frozen in
`shared/contracts/blocked_learning_methods_execution_guard.json`.
The separately named `alga_attention_declared_reconstruction_v1` is an M3
engineering reconstruction and does not remove or weaken the original `alga`
guard; its completions and composite transfer boundary are declared in
`shared/contracts/alga_attention_declared_reconstruction_contract.json`.
Likewise,
`taae_zhangbei_structured_declared_proxy_v1` is only a P3 problem proxy and
`taae_formula_fixture_v1` is only a P4 scalar fixture. Neither makes the
original `taae` method executable or reproduces the unavailable Zhangbei
arrays and reported fronts.

## Build and test

Requirements are CMake 3.20 or newer, a C++20 compiler, POSIX threads, and
OpenMP.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The Step-12 declared-reconstruction suite freezes its Spark CPU environment,
canonical `build/full` binary paths, and binary SHA-256 values in
`formal/contracts/declared_reconstruction_formal_suite_v1.json`. The default
contract audit supports a clean clone and verifies binary hashes when those
binaries are present. The formal launch/evidence gate requires the canonical
Release build and all seven frozen binaries:

```bash
cmake -S . -B build/full -DCMAKE_BUILD_TYPE=Release
cmake --build build/full -j20
python3 scripts/audit_formal_suite_contracts.py --require-binaries
```

Both modes preserve the older Waffle and Spark2 suite checks. The
`--require-binaries` mode additionally fails when any canonical binary is
missing or its SHA-256 differs.

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

The isolated BDE WS5/WS6 executable covers the paper-visible 8-by-12 and
8-by-16 scenario structures under two distinct P3 composite problem IDs. The
numeric wind arrays are explicit declared constructions. The 28-by-28 mask is
a manual two-pass transcription of numbered paper Fig. 5, independently
cross-checked against the hash-frozen author source; Table 1 supplies 250 m,
while the separately preserved source replay executes 231 m. These results
must never be pooled or ranked with official-source WS1--WS4:

```bash
python3 scripts/prepare_bde_ws56_declared_proxy.py
cmake -S hpc/bde_ws56_cpp -B build/bde_ws56_cpp \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/bde_ws56_cpp -j "$(nproc)"

build/bde_ws56_cpp/bde_ws56_hpc \
  --cases shared/contracts/bde_ws56_declared_proxy_cases.json \
  --case BDEWS5P3DAEtn30 \
  --physical-fes 10000 \
  --seed 20260729 \
  --workers 0 \
  --execution-mode auto
```

The method ID
`bde_paper_equations_imax400_exact_fes_v1` keeps paper
`Imax=2*FES/L=400` inside the scale and crossover schedules. A 10,000-FES
run includes 50 initialization evaluations and therefore completes 398
half-population generations. Fusion is source-resolved as 12 superior plus 13
inferior rows, with the complementary 13 plus 12 rows. Full objective and
feasible-set hashes, exact selected-case FES, independent layout oracles,
stage/work receipts, and one/all-visible scientific equivalence are audited by
`scripts/audit_bde_ws56_contract.py`,
`scripts/audit_bde_ws56_transition_parity.py`, and
`scripts/validate_bde_ws56_declared_proxy.py`.

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

The same executable also contains a separately identified reconstruction of
the published GeoGA operators. It maximizes AEP, uses roulette selection,
one-point crossover, and a five-nearest-free-candidate geometric mutation.
Because the Anholt boundary, wind arrays, turbine curve, actual layout, and
paper-used Poisson sample are not public, this command deliberately runs on an
admitted WFLO-GGA problem asset and must not be described as an Anholt
reproduction:

```bash
build/hpc/gga_cpp/gga_cpp_hpc \
  --algorithm geoga \
  --problem .source-cache/generated/gga_repaired/UK_London_Array.wfp \
  --physical-fes 10000 \
  --seed 20260729 \
  --workers "$(nproc)" \
  --output results/geoga_proxy_London_Array.json
```

The exact reconstruction controls and claim boundary are frozen in
`shared/contracts/geoga_reconstruction_execution_contract.json`.

An isolated executable also reuses those admitted operator transitions on a
new Anholt-structured P3 declared proxy. It freezes a synthetic irregular
polygon, deterministic 180-point Poisson sample, 111 turbines, twelve declared
joint wind bins, and explicit turbine/wake completions. The original Anholt
boundary, candidate and wind arrays, actual layout, reported AEP, and
actual-layout comparison remain blocked:

```bash
build/hpc/geoga_cpp/geoga_anholt_hpc \
  --case shared/contracts/geoga_anholt_structured_declared_proxy_case.json \
  --physical-fes 10000 \
  --seed 20260729 \
  --workers 0
```

Here `--workers 0` resolves to every hardware thread visible to the job. One
physical FES is one complete layout evaluation over this selected case's twelve
joint wind states. The problem, operator-parity, oracle, and execution
boundaries are frozen in the `geoga_anholt_structured_*` contracts.

The GGA executable also preserves the historical v1 T-MOEA Nysted
reconstruction and exposes a distinct corrected CPU R4 profile. The corrected
profile uses the paper's Eq. (16) complement set, so the relocated turbine
cannot return to any site occupied by the complete pre-mutation layout. It has
the separate method semantic ID
`tmoea_nysted_gga_asset_reconstruction_paper_eq16_v2` and the complete
biobjective problem semantic ID
`tmoea_nysted_paper_wake_gga_router_problem_v1`. The latter binds the paper's
fixed-deficit Jensen AEP objective to the same-author GGA Nysted feasible set
and declared cable router; it is distinct from the GGA LCOE problem.

```bash
build/hpc/gga_cpp/gga_cpp_hpc \
  --algorithm tmoea \
  --tmoea-profile paper-eq16-v2 \
  --execution-mode auto \
  --problem .source-cache/generated/gga_repaired/Denmark_Nysted.wfp \
  --physical-fes 3000 \
  --seed 20260729 \
  --output results/tmoea_nysted_paper_eq16_r4.json
```

Omitting `--workers` in this corrected profile requests all hardware threads
visible to the process; `--workers 1` selects serial execution. `cpu` and
`auto` are admitted, while `hybrid` and `gpu` fail closed. One physical
fitness evaluation (FES) is one complete AEP-plus-cable layout evaluation,
including initialization. Output includes actual stage/work receipts and
population/front hashes. The independent Python oracle re-evaluates every
stored front member and checks nondominance.

The default `--tmoea-profile historical-v1` retains the earlier output under a
frozen canonical scientific hash. Both profiles remain declared
reconstructions: the original T-MOEA candidate set, router, omitted controls,
seed, and reference front are unavailable. The v1 boundary remains in
`shared/contracts/tmoea_nysted_reconstruction_execution_contract.json`; the
corrected profile and its distinct problem are controlled by the
`tmoea_nysted_paper_*` contracts.

After a target machine, compiler, and worker count have been frozen, run the
25-seed fixed-work campaign with:

```bash
WFLOP_WORKERS="$(nproc)" bash scripts/run_fixed_work_campaign.sh
```

The script validates and reuses completed per-seed files, so an interrupted
campaign resumes without rerunning accepted work. Results remain untracked
until their machine-specific receipt is reviewed.

The currently approved full formal host is Spark2, whose frozen hostname is
`spark-9ab3`. From the canonical Spark worktree, deploy, validate, and launch
the complete 25-seed matrix with:

```bash
bash scripts/deploy_and_launch_spark2_formal_suite.sh
```

The controller transfers the exact clean Git `HEAD`, stages the authorized
non-redistributed BDE arrays and audited GGA assets, verifies every processor
visible to Spark2, creates the project-local pinned NumPy/SciPy validation
environment, runs the complete test gate, and starts the resumable
`spark2_campaign_suite_v1` process. Every environment receipt records the
Python package set. Spark2 results use distinct campaign
identities and must not be merged with older Waffle or Spark2-v2 probes.

On Waffle, the complete admitted formal matrix is launched through one
resumable entrypoint:

```bash
WFLOP_WORKERS="$(nproc)" bash scripts/run_all_waffle_formal_campaigns.sh
```

For a disconnect-safe Waffle launch, use the host-restricted locked wrapper:

```bash
mkdir -p logs
nohup bash scripts/launch_waffle_formal_suite.sh \
  > logs/waffle_campaign_suite_v1.log 2>&1 &
```

It rejects non-Waffle hosts, holds a single-process suite lock, uses all
processors visible to the job, and atomically records the running or terminal
state in `results/waffle_campaign_suite_v1/control/status.json`.

From the canonical development worktree, the complete deployment and guarded
launch can instead be performed with:

```bash
bash scripts/deploy_and_launch_waffle_formal_suite.sh
```

The controller requires a clean local Git worktree, transfers the exact
`HEAD` through a Git bundle, stages only the authorized non-redistributed BDE
source arrays and the audited generated GGA problem assets, runs the complete
Waffle validation gate with every visible processor, and launches only when
there is no live or completed suite process for that Git identity.

After all four file matrices pass, either host-specific suite automatically
writes separate single-objective, three-objective, and offshore descriptive
tables plus a hash-bound analysis receipt under its own suite result
directory. Common-problem algorithm ranks
use per-case median power. The analysis deliberately omits HV/IGD until a
reference-front and normalization contract is frozen, and it does not pool
objective values across different problem profiles.

It runs one optimization process at a time and uses all processors visible to
that process internally. The four result families remain separate:
18 algorithms on the common 50-case problem; BDE on 24 official-source WS1-WS4
standard/Daegwallyeong cases; isolated development-only BDE WS5/WS6 P3 cases
outside the formal suite and its rankings; six algorithms on the three-objective T46
problem; and GGA/GeoGA/T-MOEA on their declared offshore profiles. The BDE
source arrays have no license file and remain outside Git; the campaign
regenerates its local manifest from a staged authorized archive. ALGA, TAAE,
RLPSO, and RL-FODE remain excluded by the learned-state guard. Every family
records a machine receipt and supports validated resume. The suite contract
freezes 28,325 independent optimization runs and 597,155,000 complete layout
evaluations. The four mathematical problem families retain separate quality
and reproduction claims; the shared suite only standardizes execution,
validation, provenance, and recovery.

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
host-specific 25-seed formal campaign is reported only after its Spark2
receipt passes.

For the GeoGA reconstruction, all eight available WFLO-GGA problem assets pass
a 150-layout smoke, the one-worker/twenty-worker scientific outputs and
best-layout replay match exactly, and the London Array sanitizer probe passes.
A five-repeat Spark diagnostic on 1,050 complete London Array AEP evaluations
measured 13.30x evaluator and 12.92x end-to-end speedup from one to twenty
workers. These are development results for a declared proxy profile, not
MATLAB-to-C++ evidence and not reproduction of the unavailable Anholt case.

For the three-objective MOEA/D-P development package, an independent Python
scalar oracle covers both wind scenarios and three contrasting layouts. The
one-worker/four-worker scientific outputs match exactly, the 30-turbine WS2
case passes address and undefined-behavior sanitizers, and the largest
development smoke completes. A five-repeat Spark diagnostic measured
9.58x evaluator and 2.61x end-to-end speedup from one to twenty workers for
1,100 complete three-objective layout evaluations. This is not a
MATLAB-to-C++ comparison and not a host-specific formal result.

## Expansion contract

The next milestone covers every directly relevant public WFLOP paper signed by
Shangce Gao or Sichen Tao. Each paper package must pass five gates:

1. **R0 — identity:** DOI, authors, paper, source, license, and hashes;
2. **R1 — problem:** objective, constraints, wake model, sampling, and FES;
3. **R2 — algorithm:** equations, parameters, stage order, and randomness;
4. **R3 — reproduction:** source or paper-derived reference behavior;
5. **R4 — HPC:** pure C++ optimization, semantic tests, scaling, and formal run.

Missing papers or source code are registered as explicit states rather than
silently guessed. See `docs/author_lineage_registry.tsv`,
`docs/lineage_r0_r4_completion.tsv`, and `docs/roadmap.md`. The completion
matrix maps every DOI to its original-problem status, executable profile,
evidence paths, and current host-specific formal campaign. It explicitly separates original
problem reproduction, common-benchmark algorithm transfer, declared
reconstruction, and blocked learned-state identities. The machine-readable
omission boundary for TAAE, ALGA, the original PPGA Nantong study, the original
T-MOEA study, and GeoGA is frozen in
`shared/contracts/remaining_heterogeneous_reproducibility.json`. The
2026-07-29 scope freezes 23 unique DOI records and
automatically checks the Jiayi Li, Baohang Zhang, Chen Zhang, Yuhang Ma, and
Jiaru Yang coauthor lines named during scope review. The minimum is append-only:
future official-homepage records can expand it.

The Step 10 machine-audited global capability matrix is
`shared/contracts/global_execution_capability_matrix.json`. It joins every
executable profile to the scoped paper where applicable, exact method and
problem semantic IDs, real C++ binary target, runnable CLI, controlling
contracts, oracle/tests, training-state boundary, four execution modes, and
formal status. It covers the independent TAAE, PPGA, BDE, GeoGA, GGA/T-MOEA,
PBEA, and shared WFLOP executables instead of treating the local
`wflop_cpp_hpc` registry as global coverage.

Run `python3 scripts/audit_execution_backends.py` for the static audit. After
a Release build and explicit staging of trusted ignored source assets, add
`--build-dir BUILD` to execute every bounded CPU CLI, supported auto-to-CPU
path, and declared hybrid/GPU fail-closed path. Original guarded identities
remain blocked beside their separately named executable reconstructions.
Step 10 records development capability only; Step 11 sensitivity, quality,
and performance admission remains outside this commit.

<!-- BEGIN GENERATED: STEP14 CLOSURE -->

## Machine-generated project closure

_Generated by `scripts/generate_step14_closure.py`; edit the registries, not this block._

The `2026-07-29` scope snapshot contains **23 WFLOP papers** and 23 source dossiers. The executable registry contains **40 algorithm–problem profiles**, spanning 33 algorithm IDs and 14 problem IDs. The complete list is generated in [`docs/roadmap.md`](docs/roadmap.md#complete-executable-profile-inventory) and is machine-readable in [`step14_project_summary.json`](evidence/closure/step14_project_summary.json).

Method evidence counts are M0=7, M1=7, M2=10, M3=16, M4=0. Problem evidence counts are P0=24, P1=0, P2=9, P3=7, P4=0. Method tiers, problem tiers, and the execution-form labels `original`, `source-replay`, `paper-complete`, `citation-derived`, `declared-proxy`, and `fixture-only` are separate dimensions; their counts must not be added into one total. Here `original` means only an M0 method plus P0 problem authority combination; it does not automatically establish reproduction of an original paper result.

**Identity boundary.** 8 original method/problem identities remain guarded. Their 8 executable reconstructions use distinct profile IDs. The 14 complete-information profiles (M0/M1 with P0/P1) retain their registered method, problem, and profile identities.

**Exact formal-suite status**

| Exact suite ID | Status | Campaigns | Optimization runs |
|---|---|---:|---:|
| `declared_reconstruction_formal_suite_v1` | `contract_frozen_not_launched` | 11 | 6625 |
| `spark2_campaign_suite_v1` | `approved_for_spark2_formal_execution` | 4 | 28325 |
| `waffle_campaign_suite_v1` | `development_admitted_waiting_for_waffle` | 4 | 28325 |

All 6 executable learning reconstruction profiles retain explicit training-state and claim boundaries in the generated roadmap. Step 14 launched no formal campaign.

<!-- END GENERATED: STEP14 CLOSURE -->

## Licensing

Repository code is licensed under Apache-2.0. Owner-created and redistributable
documentation/data explicitly listed in `DATA_LICENSE.md` are licensed under
CC BY 4.0. Third-party papers and source archives are not redistributed; see
`THIRD_PARTY.md`.
