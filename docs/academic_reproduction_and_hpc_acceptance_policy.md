# WFLOP-HPC Academic Reproduction and Performance Policy

## Academic reproduction

Every scoped paper must end in a fixed, executable, testable
algorithm--paper-problem baseline.

1. When public source exists, the implementation combines the paper and the
   pinned public source.  A material paper/source conflict is recorded and
   either resolved paper-first or retained as two semantic profiles.
2. When source, data, a trained model, or settings are missing, the
   implementation is completed from the paper, cited predecessor work,
   same-lineage source, and finally a deterministic reasonable setting.
3. Missing author assets do not leave a target method undetermined.  They are
   recorded in the implementation fact declaration together with the DOI,
   source URL and revision, missing information, completion basis, selected
   semantics, and claim boundary.
4. Author-exact numerical identity is optional evidence.  It is not the
   common completion gate, including for papers that publish source code.
5. A declared reconstruction is a formal baseline.  Its name must remain
   distinct from an author-original implementation when the latter cannot be
   established.
6. Every algorithm, paper-problem, and learning entry source begins with an
   implementation fact declaration.  The declaration lists the paper and DOI,
   public source/data and pinned revision, information provided by the paper
   or source, every material missing item, every material paper/source
   conflict, the adopted resolution, method/problem/training semantic IDs,
   production backend, controlling contract, and claim boundary.  A registry
   or external ledger may add detail, but it does not replace this source-file
   reminder.

## Highest-performance implementation

The production implementation uses the fastest academically equivalent
phase-specific execution profile that is supported and validated on the
target host.

1. Independent population transitions and complete-layout evaluations use
   the persistent C++ CPU worker team with adaptive task granularity.
2. Learning phases use the fastest validated target-native backend:
   optimized LibTorch CPU, CUDA, or a CPU--GPU hybrid.  Generic device probes
   do not qualify as target-method performance evidence.
   LibTorch is the C++ interface to the PyTorch runtime; production campaigns
   call it directly from C++ and do not require a Python Torch execution
   version.
3. CPU cores and accelerator resources are assigned by phase.  A small
   tensor operation is allowed to use fewer threads when dispatch and
   synchronization make all-core execution slower; the saved cores must not
   be described as utilized work.
4. Development performs short performance calibration on representative
   workloads.  It does not run a full worker-count sweep for every
   algorithm.  The formal quality campaign runs only the selected
   highest-performance profile.
5. A paper-facing speedup baseline may use a small, separately measured
   one-worker C++ or original MATLAB reference.  It is not repeated as a
   full quality campaign.
6. Admission requires academic-semantic checks, physical-FES accounting,
   result-tolerance checks, stage timing, actual worker/device receipts, and
   demonstrated end-to-end benefit for the selected backend.
