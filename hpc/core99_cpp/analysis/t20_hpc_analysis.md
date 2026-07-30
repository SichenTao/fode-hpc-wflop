# T20 paper-paired HPC analysis

Paper: *Comparative Performance of Twelve Metaheuristics for Wind Farm Layout Optimisation*  
DOI: `10.1007/s11831-021-09586-7`

## Target contribution boundary

T20 proposes four benchmark problems and a twelve-metaheuristic comparison
protocol. It does not propose a new optimizer. The paper-owned production asset
is therefore the four-case problem/evaluator package and the generic
candidate-batch interface. The twelve named methods are comparison baselines
and are not reproduced as T20-owned algorithms.

## Mathematical work structure

For one candidate with \(N\) turbines, the evaluator performs 36 wind
directions and up to \(N(N-1)\) upstream/downstream interaction checks per
direction. The partial-overlap cases add a circle-intersection calculation.
Every complete 36-direction average is one physical function evaluation.

The paper's population-level optimizer interface exposes independent candidate
evaluations. This is the dominant safe parallel axis:

1. decode each candidate independently;
2. evaluate all 36 directions inside that candidate without shared state;
3. write the result to its deterministic candidate index;
4. reduce the best candidate in fixed index order.

This preserves every paper problem and optimizer-facing semantic while avoiding
nested short parallel regions. A persistent C++ thread team amortizes scheduling
cost across the full candidate batch.

## Explicit non-transformations

- The fixed-39 decoder is not changed to a binary encoding.
- Partial overlap is not approximated by hub-only overlap.
- The 36 directions are not sampled or reduced.
- Algorithm comparison baselines are not relabeled as paper-proposed methods.
- Parallelism changes evaluation order only; indexed values and deterministic
  best reduction keep scientific output reproducible.

## Admission

- H0: primary paper and scope audit.
- H1: equations, constants, four cases, and protocol frozen.
- H2: operation graph above frozen.
- H3: candidate-batch parallelism selected; inner nested teams rejected.
- H4: pure-C++ production implementation and source declarations.
- H5: independent Python equation oracle, decoder tests, exact cost, and
  reconstructed Fig. 5/6 layout within 1.5% of printed powers.
- H6: pending Waffle all-available-core paper-budget profile.
