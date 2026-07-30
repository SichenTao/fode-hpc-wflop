# WFLOP-HPC Benchmark Onboarding Contract

This contract defines how a future wind-farm layout optimization (WFLOP)
paper becomes a switchable, high-performance, academically reproducible
benchmark package. The admission unit is one paper's target algorithm plus
its paper problem and protocol. Comparison-only baselines are optional and
never block target admission.

## 1. Evidence and identity

1. Register the paper title, DOI, authors, target method, paper problem,
   objective mode, case matrix, physical work budget, repeat count, metrics,
   convergence sampling, and public assets.
2. Pin every public source or data archive by URL and revision or SHA-256.
3. Put a fact declaration at the top of every target algorithm, target
   problem, and learning entry source. It must list:
   - paper and DOI;
   - source/data URL and pinned identity;
   - what the paper and source actually provide;
   - every material missing item and paper/source conflict;
   - the selected paper-first, source-first, or dual-profile resolution;
   - the predecessor, same-lineage source, or deterministic setting used to
     complete missing details;
   - method, problem, training, and protocol semantic IDs;
   - production backend and claim boundary.
4. A declared reconstruction receives its own semantic ID. It is not renamed
   as the unavailable author-original implementation.
5. Author-exact numerical identity is useful evidence, not the common
   completion criterion. The required result is a deterministic, executable,
   testable academic reproduction with an explicit evidence boundary.

## 2. Problem and algorithm interfaces

Every problem package supplies:

- an immutable case contract and semantic hash;
- decision encoding, feasible-set definition, repair rules, objectives and
  directions;
- wind states, joint probabilities, wake/power/cost equations, units, and
  turbine/site parameters;
- one complete-layout evaluation as the physical function-evaluation (FES)
  accounting unit;
- scalar or multiobjective result serialization;
- an independent formula/oracle fixture and bounded numerical tests.

Every algorithm package supplies:

- a registered algorithm and method semantic ID;
- paper parameters and every inferred parameter in one visible contract;
- initialization, variation/update, repair, selection/archive, termination,
  training, and inference stages;
- schedule-independent random events for mathematically parallel stages;
- exact physical-FES accounting separated from offline training work;
- one common command-line entry that accepts algorithm, problem, case, seed,
  work budget, backend, and worker count.

The platform compatibility registry may route any algorithm to any problem
whose objective mode, decision representation, constraints, and required
feedback are compatible. Interface compatibility does not imply that the
combination was studied or is scientifically meaningful; such combinations
remain interoperability tests unless separately registered as experiments.

## 3. H0-H6 high-performance admission

Each target pair completes the same analysis and evidence chain.

- **H0 — mathematical work model:** derive problem and algorithm work by
  stage, data size, complete-layout evaluations, synchronization, memory, and
  training operations.
- **H1 — authority trace:** bind each formula, setting, array, and repair rule
  to paper, public source, predecessor, or disclosed reconstruction.
- **H2 — dependency graph:** mark independent population/evaluator work and
  ordered state transitions. Do not parallelize an ordered transition by
  changing the method.
- **H3 — backend design:** select persistent CPU workers, CUDA kernels,
  LibTorch C++ CPU/CUDA, or a phase-specific hybrid. Include dispatch
  granularity, memory movement, reductions, and oversubscription control.
- **H4 — implementation:** implement the complete algorithm and problem,
  including population update, evaluator, repair, selection/archive,
  learning, serialization, and physical-FES receipts.
- **H5 — scientific equivalence:** compare with an independent formula,
  paper/source fixture, or separately written oracle. Backend self-agreement
  alone is insufficient. Repeat H5 after every semantic or numerical change.
- **H6 — measured production performance:** run representative fixed work on
  the selected highest-performance profile, record at least five
  observations, attribute at least 95% of time to named H0 stages, and record
  actual threads/devices, affinity, CPU time, wall time, throughput, memory,
  and claim boundary.

On Spark2 the CPU production profile uses all 20 affinity-visible CPUs for
the persistent C++ team. A learning phase may use fewer tensor threads when a
short calibration proves that synchronization makes 20 slower. The outer
cores must not be described as utilized while they are idle. LibTorch is
called directly from C++; a separate Python/Torch production route is not
required. CUDA compilation or a generic device smoke is compatibility only;
target GPU H6 requires the complete target training and optimization loop.

## 4. Formal experiment admission

After H5-H6 acceptance, create a new append-only manifest version containing:

- target pair and all semantic IDs;
- immutable cases and semantic hashes;
- 25 independent optimization seeds unless another project protocol is
  explicitly frozen;
- exact physical FES per run and a separate offline-training ledger;
- selected backend, workers, CPU affinity or accelerator identity;
- source, binary, environment, H6, artifact, and control-software hashes;
- unique raw-result paths and atomic resume state.

Run only the selected highest-performance production profile for the full
quality experiment. One-worker C++ and original MATLAB are measured on a
small representative subset only when a paper-facing acceleration ratio is
needed. They are not repeated as full quality campaigns.

Every formal result must pass:

- process exit and valid JSON;
- exact result key, binary hash, case, seed, and physical FES;
- actual worker/device and affinity receipt;
- feasibility and objective payload checks;
- atomic complete status; partial or failed files are never reusable.

Paper-scale learned methods train from scratch under a frozen training
contract when author artifacts are absent. If the required accelerator is
temporarily unavailable, the target remains an executable,
`validated_deferred_full_training` package with immutable resume
requirements; all other targets continue.

## 5. Closure and public claims

The final bundle is generated from raw results. For every paper it records:

- target algorithm and paper-native or explicitly named proxy problem;
- source authority, missing/conflicting assets, completion basis, and fact
  declaration files;
- H0-H6 evidence and selected backend;
- formal status, cases, 25-seed quality summaries, physical-FES throughput,
  and evaluator/algorithm/end-to-end timing;
- training state and reconstruction/proxy/source-replay claim boundary.

Admission is complete when all currently executable targets pass their full
campaign and audit, every resource-deferred learning target has an exact
resume contract, non-target baselines do not enter readiness, private paths
or restricted assets are absent from public evidence, and the public audit
passes.
