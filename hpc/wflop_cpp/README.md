# Native C++ WFLOP HPC platform

This platform provides one pure C++ executable for the frozen FODE-E0-L
benchmark and the registered algorithm state machines:

```text
fode aga sugga ise agpso cgpso lshade clshade cede msshade bde hgpso
aiga ciga lsde wfadde alshade ppga
rlpso_compact_policy_declared_reconstruction_v1
rlpso_paper_corrected_training_reconstruction_v1
fqfode_seeded_training_declared_reconstruction_v1
alga_attention_declared_reconstruction_v1
```

Every optimization owns one persistent thread team.  The same team performs
algorithm-safe population work and all phases of the shared Jensen/Park
evaluator.  `TotalOnly` and `TotalAndPerTurbine` are two output-detail modes of
the same physical kernel; the detailed mode is used only by algorithms whose
published state transition needs worst-turbine information.

Build and bounded tests from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run the bounded semantic smoke directly:

```bash
bash scripts/smoke.sh
```

After freezing the target machine and worker count, the resumable public
fixed-work entrypoint runs all 25 seeds:

```bash
WFLOP_WORKERS="$(nproc)" bash scripts/run_fixed_work_campaign.sh
```

`hpc/wflop_cpp/spark2/campaign.sh` is the stricter archived Spark2 workflow. It
is retained for provenance and requires the original private evidence bundle;
do not use it as the portable entrypoint.

All runs use exactly 24,000 completed full-layout physical evaluations.
SUGGA reads the frozen, round-trip-validated native model assets from
`shared/models/sugga_cpp`; neither model training nor Python is part of the
native build or runtime.

The `ppga` identifier is a declared paper-derived transfer to this common
problem, not a reproduction of the unavailable Nantong 3D data. Its exact
fitness normalization, diversity, stagnation, power-law sampler, elitism, and
parallel barriers are frozen in
`shared/contracts/ppga_fode_e0_transfer_execution_contract.json`.

The exact `alga` identifier remains blocked because the author network state
and original 3D Guishan assets are unavailable. The distinct
`alga_attention_declared_reconstruction_v1` identifier is an M3 engineering
reconstruction with every missing training and mask field frozen in
`shared/contracts/alga_attention_declared_reconstruction_contract.json`.
Its primary problem is the separately identified composite planar transfer in
`shared/contracts/alga_guishan_planar_transfer_cases.json`; the FODE-E0 common
profile is only an additional platform-stress transfer. `--compute-backend
cpu` is executable. `cpu+gpu` and `gpu` are recognized, fail-closed
compatibility interfaces in the current CPU build and never silently fall back.
The CPU path has worker-count semantic admission. Its HPC throughput admission
is still pending an uncontended formal host; local Spark timing is recorded
without a speedup claim.
