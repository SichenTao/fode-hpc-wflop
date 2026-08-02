# L0581 HPC analysis: sparse coloured gradients for round-farm WFLO

## Scientific object

L0581 is not an evolutionary-optimizer paper. Its target contribution is a
cheaper AEP gradient for continuous wind-farm layout optimization. For each
wind state, it differentiates the `N` turbine-energy outputs with respect to
the `2N` layout coordinates, thresholds the `N x 2N` Jacobian, colours
non-conflicting columns and evaluates the compressed forward-AD seeds. The
paper tests eight round farms from 38 to 349 turbines and pairs dense and sparse
optimization on ten randomized 95-turbine starts.

The comparison is therefore controlled at the Jacobian implementation layer.
Dense and sparse runs use the same Cumulative-Curl-lineage equations, wind
resource, turbine curve, layout, projected optimizer and stopping rule. A
different wake model or optimizer would confound the claimed sparsity benefit.

## Source and geometry audit

The official BYU paper is complete, but it does not link target code. Current
FLOWFarm supplies the Cumulative Curl equations, NREL-5MW curve, exact 12-bin
Nantucket resource and a 38-turbine round layout under MIT. Git history shows
that its substantial unstable-sparsity source began in February 2024, after
the 2023 paper. It is an equation/data and same-author lineage oracle, not the
target executable.

The target's geometry statements are internally incompatible. Radii are
`5.1 k D`, while the exact size sequence requires ring populations
`floor(6.4 k)`. This produces 6, 12, 19, 25, 32, 38, 44, 51, 57 and 64
turbines per ring and exactly the reported cumulative sizes. The public
38-turbine fixture agrees, but the outer-ring chord spacing approaches 5D and
is below the stated 5.1D. The implementation preserves the observable sizes,
radii and fixture lineage, exposes the actual spacing, and does not claim the
inconsistent 5.1D minimum.

## Mathematical work decomposition

For one wind state, let `P(x) in R^N` be normalized turbine power and
`J = dP/dx in R^(N x 2N)`. Dense forward AD assigns every input column a
unique seed. With a binary retained pattern `S`, columns `j` and `k` conflict
when any row retains both `S_ij` and `S_ik`. Greedy graph colouring assigns
one seed to every conflict-free colour class. A row then contains at most one
retained derivative per colour, so the compressed directional derivative can
be decompressed without ambiguity.

The pure C++ evaluator uses eight-lane fixed-width dual blocks. Its dominant
work is therefore proportional to

`ceil(number_of_colours / 8) * cost_of_one_wake_evaluation`,

rather than one scalar traversal per coordinate. The same block kernel serves
dense and sparse modes. Sparse pattern discovery deliberately uses a dense
Jacobian once and is separately accounted; the paper's benefit is the reuse of
that pattern in later optimizer iterations.

## HPC mapping

- One persistent executor owns every CPU worker for an optimization.
- Colour blocks are independent, read the same immutable layout and direction,
  and write disjoint Jacobian columns.
- The 12 wind states are traversed in fixed paper order. Each state exposes all
  CPU cores through its colour blocks; this avoids nested thread teams and
  preserves the target's per-state adaptive pattern lifecycle.
- AEP and gradient reductions use fixed row, column and wind-state order.
- Counter-keyed perturbations make common dense/sparse starts independent of
  thread scheduling.
- Boundary and spacing repair are deterministic and remain outside the timed
  gradient component while staying inside end-to-end optimization time.

## Admission questions

H5 verifies the eight sizes, the documented geometry conflict, compression,
finite gradient accuracy, one/all-core deterministic science, real worker
participation, paired starts and non-worsening optimization. H6 then reports:

1. one-worker versus all-available-Waffle-core time for the same dense and
   sparse 349-turbine gradient kernels;
2. all-core dense versus sparse repeated-kernel speed, colour count and error;
3. pattern-discovery cost separately, so it is not hidden inside or omitted
   from the end-to-end interpretation.

Formal execution consists of 40 deterministic accuracy roles (eight paper
sizes by five disclosed thresholds) and 20 all-core optimization roles (ten
common starts by dense/sparse). The paper omits the exact accuracy thresholds,
optimizer and stopping rules, so those reconstructed settings are frozen in
the contract and are not numerical-replay acceptance targets.

## Claim boundary

This package supports a flexible equation-, algorithm- and protocol-level
academic reproduction. It does not claim the authors' target FLOWFarm source,
ForwardDiff/SparseDiffTools program, optimizer, random stream, layouts, figure
coordinates or timings. Paper values are external anchors; admission depends
on method semantics, paired problem semantics, accuracy and measured HPC work.
