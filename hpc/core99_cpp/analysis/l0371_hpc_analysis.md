# L0371 IET-GWM and DEEM HPC analysis

## Scientific kernel

For `S` wind/stability states, `N` turbines and `C` candidate grid points, a
literal whole-layout evaluation computes every source-target wake pair and
costs `O(SN²)`. The target DE encoding changes one turbine at a time and the
direct DEEM predecessor explicitly identifies this as the condition that makes
evaluation caching possible.

The implementation therefore precomputes

`deficit[state, source_candidate, target_candidate]`

with the target IET-GWM equations. For a trial that replaces turbine `j`, it:

1. removes the old and adds the new source contribution for every unchanged
   target turbine;
2. recomputes only the moved target's incoming deficits; and
3. updates the state power totals.

This is `O(SN)` per feasible physical evaluation. It is an algorithm-specific
HPC transformation derived from DEEM's mathematics, not merely a language
translation.

## Semantics that must remain sequential

The offspring population is generated from one layout snapshot, but offspring
are exchanged with the active layout in turn. Every accepted move therefore
changes the layout against which later offspring are tested. Evaluating all
offspring independently would create a different synchronous DE algorithm.

The implementation preserves this sequential acceptance. It parallelizes the
candidate-pair precomputation and, for the 504-state actual Horns case, the
state axis inside each incremental trial using one persistent thread team.
For light one- to 108-state ideal tasks, short inner parallel regions lose to
coordination overhead; the formal scheduler instead runs independent
case/seed tasks concurrently. This is the highest-throughput allocation while
preserving the paper transition order.

## Complexity and storage

- Precomputation: `O(SC²)` work and `4SC²` bytes using float deficit ratios.
- Initial/full oracle evaluation: `O(SN²)`.
- Feasible DEEM trial: `O(SN)`.
- Constraint test: `O(N)`.
- Random events: counter-keyed by generation, phase, turbine and draw, so
  one-core and multicore schedules produce the same scientific hash.

The actual Horns proxy has `S=504`, `C=531`, and `N=80`, so the wake table is
about 542 MiB. This deliberate memory-for-work trade removes transcendental
MOST/Gaussian calculations from all 150,000 physical evaluations.

## H5 and H6 gates

H5 requires:

- all 29 paper-native case IDs;
- exact fixture hash and dimensions;
- the 10x10 ideal and parallelogram Horns grids;
- non-strict 5D feasibility evidenced by target figures;
- probability, physical-power and efficiency bounds;
- exact physical-FES accounting;
- one-core/multicore/replay scientific-hash parity; and
- observed multicore wake-table construction.

H6 on Waffle compares the same full-core C++ source with one and twenty
workers on the heavy actual-stability task, records precomputation, evaluator,
algorithm and end-to-end times, and then launches the 29-case, 30-repeat
paper-scale campaign using at most twenty aggregate CPU workers.
