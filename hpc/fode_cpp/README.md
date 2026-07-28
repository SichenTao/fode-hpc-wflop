# FODE_CPP_HPC_FULL

This directory contains the single native implementation selected for the
FODE-first study:

- the complete source-derived FODE state machine;
- the complete FODE-E0-L analytical Jensen/Park objective;
- OpenMP parallelism across FODE individuals and coordinates;
- OpenMP parallelism across layouts, wind directions and downstream turbines;
- a deterministic counter-keyed random stream whose values do not depend on
  thread scheduling; and
- an exact physical-FES ledger.

The executable has no Python, MATLAB, pybind11 or third-party JSON dependency.
The frozen case contract remains
`shared/contracts/benchmark_cases.json`.

Headline timing uses the default uninstrumented path. Add `--profile-phases`
only to a separate diagnostic run when the 17-stage time ledger is needed;
profiled timing must not be mixed into the MATLAB-to-C++ headline comparison.

## Standalone FODE build

```bash
cmake -S hpc/fode_cpp -B build-fode \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-fode --parallel
OMP_DYNAMIC=FALSE OMP_NUM_THREADS=20 OMP_PROC_BIND=spread \
OMP_PLACES=threads build-fode/fode_cpp_hpc \
  --self-check --cases shared/contracts/benchmark_cases.json
```

## One full fixed-work run

```bash
OMP_DYNAMIC=FALSE OMP_NUM_THREADS=20 OMP_PROC_BIND=spread \
OMP_PLACES=threads build-fode/fode_cpp_hpc \
  --case WS5tn30 --physical-fes 24000 --seed 20260728 \
  --cases shared/contracts/benchmark_cases.json --output result.json
```

A physical FES is one complete feasible-layout evaluation over all wind
directions and all 13 wind-speed bins in the selected case. A production
campaign must freeze its own machine, affinity, compiler, source, binary, and
worker contract before timing.

An all-case production run is:

```bash
OMP_DYNAMIC=FALSE OMP_NUM_THREADS=20 OMP_PROC_BIND=spread \
OMP_PLACES=threads build-fode/fode_cpp_hpc \
  --all-cases --physical-fes 24000 --seed 20260728 \
  --cases shared/contracts/benchmark_cases.json \
  --output results.jsonl
```
