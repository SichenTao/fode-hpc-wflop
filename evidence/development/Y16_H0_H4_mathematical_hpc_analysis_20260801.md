# Y16 H0-H4 mathematical and HPC analysis

## Evidence and executable boundary

The controlling paper is Huang et al., DOI `10.1109/TSTE.2026.3686029`, PDF SHA-256 `4288399ae7711a6fc1acbbfba5d5c510c5f56a8cb820e6d8d91e7f419d480e3c`. The first-party supporting patent is CN121683298A/CN121683298B, application CN202610193544.7A, by Xinwei Shen and Zehai Huang. Exact-title, DOI, method, author/institution and GitHub searches found no target code, Gurobi model, private numeric arrays, layouts, traces or result archive. This package is therefore a source-backed flexible academic reconstruction, not an author-code or numerical-replay claim.

The paper and patent determine the BMM/IMM structure, wake/TI/cost equations, 18 rotation angles, ten patterns, BDA procedure, three turbine types and Tables I-X roles. They do not publish the two site boundaries as data, depth/soil rasters, wind arrays, ten exact pattern pairs, BDA controls, solver model/settings or original layouts. Every completion is frozen in `shared/contracts/core99_y16_huang_2026.json` and repeated at the top of the source.

## H0-H1: mathematical contract and professional corrections

The regular layout selects one angle-pattern pair and activates whole pattern rows so that the exact turbine count is reached and every selected pair is separated by at least `5D`. The 31 deterministic target roles cover three seabed types, three objectives, four TI discretizations, the complete BMM/IMM turbine-count/grid-size matrix, the 100-WT scaling study and two Hainan objectives. The GA, PSO and GWO comparison baselines are not target contributions and are intentionally excluded.

Several printed expressions cannot be used literally without violating the paper's own physical intent. Table I's 155/90/263 m values are model-consistent rotor diameters, not radii. Eqs.13-14 require upstream selection as well as target selection; Eqs.12/51 require target selection; and Eq.43 requires scenario probabilities. Eq.29's discounted multi-year energy conflicts with annualized cost and the reported LCOE scale, so the executable consistently uses annual expected energy. Table IX itself controls its 5530/7445/16237 s values over the conflicting prose. These are explicit repairs, not silent claims about author behavior.

The missing private inputs are replaced by analytic boundary, terrain and wind proxies derived from the published figures and tables. Since the paper reports ten patterns but not their `(starting column, gap)` values, each grid resolution generates the first ten safety-valid pairs beginning at `gap = ceil(5D/grid_spacing)`. This avoids calling a pattern valid when it violates the paper's own safety distance internally.

## H2: BMM, IMM and bounded Dinkelbach

BMM retains turbine-wise power constraints and point-pair distance rows. IMM deduplicates the conflict graph and aggregates wake-power coefficients by regular-layout row while retaining the same physical evaluator and piecewise-linear TI equations. Their decision semantics, exact turbine count and accepted layouts are therefore directly comparable.

For every angle-pattern subproblem, the lower bound is the minimum numerator divided by the maximum denominator and the upper bound is the ratio attained by the maximum-energy layout. Global upper-bound pruning implements Eq.69. Each retained fractional subproblem uses the two endpoint solves and the secant lower-bound update in Eq.68, while the upper bound is updated from the current upper-endpoint solution. The missing stopping controls are fixed at `1e-6` and 20 iterations. Each returned layout is independently re-evaluated before admission.

## H3: high-performance transformations

1. Build geometry once per angle-pattern task and generate immutable wake, TI, terrain and cost coefficient blocks in parallel.
2. Solve initial numerator/denominator bounds for all tasks concurrently, reduce the global upper bound deterministically, and prune only mathematically dominated tasks.
3. Execute BMM/IMM and BDA at the independent subproblem level using one pinned HiGHS thread per task, preventing nested oversubscription while using every Waffle core across tasks.
4. Use the persistent C++ executor for both coefficient and MILP phases; fixed task indices and ordered reduction make scientific output independent of worker completion order.
5. Keep BMM and IMM on the same corrected evaluator so a speed difference cannot be manufactured by omitting physical terms.
6. Disable HiGHS presolve for this reconstructed disjunctive TI model: an isolated test showed presolve could report infeasibility, while the unchanged presolve-off model produced evaluator-valid 40-WT layouts. This solver-stability choice is explicit and testable.

A local full `18 angles x 10 patterns` min-annual-cost diagnostic, run before the final safety-valid pattern-index correction, used all 20 requested workers, admitted 33 feasible incumbents without evaluator rejection, and returned 40 turbines with minimum spacing approximately 775 m. Its 1722.19 summed MILP seconds completed in 93.60 wall seconds, corresponding to 18.40-way effective subproblem concurrency. It demonstrates executor saturation only: it is development evidence, not final-science evidence and not an immutable Waffle H6 result.

## H4: validation and formal campaign

H5 verifies the 31-role matrix, 9 BMM/22 IMM split, turbine-diameter interpretation, exact count, spacing, finite physical metrics, independent evaluator acceptance, BDA controls and one/all-worker scientific identity on fixed reduced work. A strict-warning build and source/fact/dossier audits are required.

H6 compares the same pure-C++ executable with one Waffle worker and every Waffle core on 20 identical angle-pattern tasks. Formal work then runs each of the 31 deterministic paper-native roles once at all-core maximum performance with all 18 angles and ten patterns. No artificial 25-seed replication is used for deterministic mathematical programming. Outputs remain candidate evidence until an immutable Waffle snapshot, environment receipt and complete runner summary exist.
