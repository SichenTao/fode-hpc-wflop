# T27 H0–H6 HPC analysis

Paper: *Wind Farm Layout Optimization with Diffusion Models*
DOI: `10.1145/3711896.3737181`

## H0 — scientific workload

The paper performs 5,000 constrained-layout evaluations before learning, then
ten train–sample–evaluate rounds with 10,000 optimizer steps and as many as
1,000 new FLORIS evaluations per round. The fully connected graph contains
`N(N-1)` directed edges. The paper cases span 10–100 turbines, so dense graph
attention, EDM sampling, constraint repair, and the repeated wake evaluations
are all material workloads rather than cosmetic parallel loops.

## H1 — authority and conflicts

The primary authority is the local KDD paper. The paper-linked repository is
`dbsxodud-11/layopt` at
`19ff38950ec6d23b6241875c1e5936878508a10d`; it has no license and therefore
is inspected but not redistributed. FLORIS 4.1.1 at
`2c3be8fd91fdb2ce519e2f3139444a7045f50473` supplies the licensed simulator
reference.

The paper specifies 5,000 initial layouts, 10,000 training steps per round,
Adam learning rate `5e-4`, batch 256, and LeakyReLU. The source defaults to
1,000 layouts, 5,000 steps, `1e-4`, and applies GELU. It also references an
absent requirements file, provides no data/checkpoints, cannot actually
disable GPU through its declared CLI flag, and has a non-closed checkpoint
and cumulative-dataset transition. The production profile follows the paper;
the source activation remains separately selectable for discrepancy studies.

## H2 — mathematical decomposition

1. Generate unit-square layouts and enforce the physical `2D=252 m` spacing.
2. Evaluate layouts under the paper's NREL 5 MW, FLORIS 4.1.1 GCH case.
3. Min–max normalize AEP conditions and coordinates.
4. Train the conditional EDM with its paper sigma distribution and
   preconditioning.
5. Sample with the 128-step stochastic Heun solver and classifier-free
   guidance.
6. Repair spacing, evaluate new layouts, and append them to the cumulative
   dataset.
7. Repeat ten rounds without resetting learned state or losing old layouts.

The ordered optimizer/EMA/cumulative-dataset commits remain serial. Independent
layouts, graph batches, tensor kernels, repair batches, and wake evaluations
are parallel.

## H3 — high-performance design

- Dense complete-graph GAT is expressed as batched matrix operations instead
  of Python/PyG edge-object orchestration.
- EDM forward/backward, AdamW, EMA, Heun sampling, and repair gradients run
  directly in LibTorch C++ on optimized CPU or CUDA.
- CUDA is the paper-scale primary learning backend; multi-core CPU is a full
  functional fallback rather than a placeholder.
- The GCH evaluator is a native contiguous C++ zero-yaw reconstruction,
  parallel over layouts with one persistent worker set.
- Fixed-index output and ordered round commits make CPU thread-count changes
  scientifically stable; GPU determinism is recorded as best effort.

## H4 — implementation

`core99_t27_hpc` exposes `cpu`, `cuda`, and `auto` backends, the paper and
source activation profiles, paper-native case dimensions, bounded smoke
overrides, evaluator fixtures, and evaluator-throughput mode. The default
values are the paper protocol; smoke tests must override them explicitly.

## H5 — scientific validation

The pinned official FLORIS 4.1.1 fixture covers single, aligned-wake,
near-independent crosswind, and staggered layouts. Current farm-power relative
errors are respectively 0%, 0.203%, 0.000018%, and 0.0052%. The deterministic
100-layout, 30-turbine stress checksum differs by 1.72% from official FLORIS.
The declared H5 acceptance bound is 0.5% on fixed farm-power cases and 3% on
the diverse stress checksum. This validates an equation-level high-throughput
reconstruction; it is not described as byte-identical FLORIS.

The unit test additionally exercises dense GAT shape, finite EDM loss,
gradient spacing repair, and a complete train–sample–evaluate–append round.
Both CPU and CUDA bounded lifecycle smokes pass.

## H6 — performance admission

On the local 20-core host, five repeated evaluations of 10,000 deterministic
30-turbine layouts have medians of `0.283061 s` with one C++ worker and
`0.029996 s` with 20 workers: `9.44x`, with identical AEP checksums.
The same 100-layout prefix takes `9.744936 s` in optimized reused-model
official FLORIS Python and `0.006821 s` in 20-worker C++: `1428.7x`.
Because the native evaluator is a validated reconstruction rather than the
identical FLORIS implementation, the latter is reported as a
reference-to-production throughput ratio, not a pure parallel speedup.

Waffle H6 repeats the CPU and CUDA bounded probes and freezes environment,
affinity, device, raw output, and medians before formal runs begin.
