# Native C++ WFLOP HPC platform

This platform provides one pure C++ executable for the frozen FODE-E0-L
benchmark and eighteen registered algorithm state machines:

```text
fode aga sugga ise agpso cgpso lshade clshade cede msshade bde hgpso
aiga ciga lsde wfadde alshade ppga
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
