# T84 WEC high-performance analysis

T84 is Thomas, McOmber, and Ning, *Wind Energy* 25 (2022), DOI
`10.1002/we.2692`. The target contribution is wake expansion continuation
(WEC-D), evaluated on four continuous-coordinate problems using two wake
models and both gradient-based and gradient-free optimizers.

## Mathematical work graph

For one layout with (N) turbines and (S) wind states, wake evaluation is
dominated by the (O(SN^2)) upstream/downstream interactions. The four paper
cases contain 16/20, 38/12, 38/36, and 60/72 turbine/state pairs. Complete
coordinate gradients add (2N) derivative components to each scalar in the
same wake graph. Pairwise spacing constraints cost (O(N^2)), and boundary
constraints cost (O(N)) for square/circle cases or (O(NF)) for the
14-facet Amalia polygon.

Wind states are independent until the fixed annual-energy reduction. This is
the useful inner parallel axis for SLSQP and its exact forward derivatives.
ALPSO evaluates 30 particles independently before an ordered personal/global
best and multiplier update, making the population the stronger parallel axis.
The continuation stages, SLSQP iterations, PSO iterations, augmented-
Lagrangian updates, and best-state commits are dependency chains and remain
ordered. Parallelizing those chains would change the target algorithms.

## High-performance realization

One persistent pure-C++ worker team is reused across every legal inner region.
The Bastankhah-Niayifar and Jensen-cosine/Katic evaluators use contiguous
numeric arrays, direction-local scratch state, fixed output slots, and a
fixed-order annual reduction. The exact-gradient path uses fixed-width
forward automatic differentiation for at most 120 coordinate variables;
spacing and boundary Jacobians are analytic. This avoids Python, Tapenade,
and per-evaluation process creation while retaining the mathematical graph.

For SLSQP, the team partitions wind states. For ALPSO, it partitions complete
particle evaluations and never creates nested direction teams. Random events
are generated before the parallel physical evaluations and assigned to fixed
particle slots; the ordered PSO commit then consumes those slots. Thus one-
and all-core runs have identical layouts, physical call counts, final
evaluations, and scientific hashes.

The formal 3,200-run campaign has a second, coarser legal axis: 200 common
starts for each of 16 final paper roles are independent. Its production
mapping launches 20 single-worker processes concurrently on Waffle. This
fills all CPU cores and avoids nested oversubscription. H6 separately proves
single-optimization acceleration with all cores for complete Case-4
SLSQP+WEC and Case-2 ALPSO+WEC workflows.

## Admission evidence and boundaries

H5 covers all four cases, five evaluator case/model combinations, the full 16
final roles, one/all-core deterministic identity, lifecycle stage counts,
final feasibility, and exact ALPSO complete-layout budgets. Source-backed
initial AEP checks for Case 2 Bastankhah/Jensen, Case 3, and Case 4 are within
2%; Case 1 is excluded because paper and source disagree on 10 versus 8 m/s.

Local full-budget probes on 2026-08-01 gave 5.24 to 0.896 seconds for Case-2
ALPSO+WEC (5.84x) and 4.57 to 0.721 seconds for Case-4 SLSQP+WEC (6.35x),
with identical scientific hashes. These are development evidence; the
immutable Waffle snapshot supplies the authoritative H6 timing.
At common start 0 and seed 2026088399, complete ALPSO control and ALPSO+WEC
runs produced distinct feasible layouts and 161.513 versus 163.853 GWh,
respectively. This is a branch-activity check, not a formal quality claim.

The claim is a source-backed flexible academic reproduction. It does not
claim author SNOPT/Tapenade/pyOptSparse trajectories, exact random-state
replay, exact numerical optima, or first use of parallel wake evaluation.
