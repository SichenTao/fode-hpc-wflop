# L0373 H0-H4 mathematical and HPC analysis

## H0: source and semantic audit

The controlling paper is Chen et al., DOI `10.1016/j.renene.2021.10.032`, PDF SHA-256 `a0e5b3c2fe2a24e870e7b17c0220d71a644e96e6fd87b4087f3fb9d0c6555f50`. Its arXiv 2107.11620 source archive, SHA-256 `6c5dd1b686f0501051974d4d464a44ca5283847b34a459d6e4daf5535bcbef9c`, supplies the complete TeX and original figures but no executable DBHM. Exact-title, DOI, author, DBHM and repository searches found no target code, optimizer settings, numeric wind rose, layouts, controls, seeds, traces or result archive.

The paper cites FLORISSE_M. Its public MIT repository is pinned at commit `36cb0a0295d2a1e05640fdbbcb9bb361ac8d592e` and repository-archive SHA-256 `451150ba62b242353f9bdaa5e5bef10cad5d63e0e3841d625838572d9f0c1f75` (Zenodo `10.5281/zenodo.4458669`). It supplies the missing near-wake relationship and model lineage; it is not represented as the unpublished target MATLAB/DBHM source.

Four printed-form issues are handled explicitly. Equation 1 has a positive Gaussian exponent and is restored to the physically required negative exponent. Equation 8 includes self-pairs and would make any positive separation infeasible, so constraints use unique `i<j` pairs. Equation 15 also includes self-pairs and double-counts unordered pairs, so each violated unordered pair contributes once. Equation 3 has dimensionally ambiguous empirical scaling relative to the cited FLORISSE_M form; the target source is missing, so the target's printed two-dimensional expression is retained and identified as a declared paper-literal adaptation rather than exact FLORISSE_M replay. The broad nonconvex-ADMM convergence paragraph is not promoted to an unconditional certificate; the executable claims deterministic stationary search only.

The arXiv wind-rose figure is digitized into 36 normalized sectors. Twelve directions aggregate three adjacent bins; 180 and 360 directions divide each ten-degree mass uniformly. The 16-turbine rectangle and 80-turbine Horns-Rev geometry are reconstructed from text and original figures. For Horns Rev, the stated approximately 7D value defines the initial parallelogram side; it is not substituted for the paper's 4D minimum-distance constraint. The missing PSO and subproblem budgets are declared as ten trials, population 40, 30 PSO iterations, five projected control passes and at most 12 DBHM iterations.

## H1: mathematical work decomposition

For a layout `L`, wind scenario `omega`, yaw vector `gamma` and induction vector `alpha`, the evaluator rotates coordinates into wind axes, sorts upstream turbines, computes the Gaussian deficit at nine fixed lateral rotor samples, combines multiple wakes by root-sum-square, and evaluates actuator-disk power. A complete layout/control schedule costs `O(|Omega| N^2 Q)`, where `Q=9`. Layout feasibility costs `O(N^2)`.

The method first runs ten independent PSO warm-start trials on isolated greedy-control layout optimization. It then solves every wind scenario's control problem independently. DBHM alternates independent scenario-local layout/control subproblems, deterministic analytic consensus coordination and ordered multiplier updates. The paper-native profiles are the two three-turbine illustrations and the four 16/80-turbine by 36/360/12/180-direction studies; the latter each produce Cases 1-5. The paper reports that its DBHM subproblems were executed sequentially and explicitly identifies parallel execution as a source of further savings; scenario parallelism is therefore source-stated, not an invented change to the mathematics.

## H2: high-performance design

One optimization owns one persistent full-core executor. Complete PSO individuals and mutually independent control/DBHM scenario subproblems are parallel fixed-index tasks. Stable PSO commits, consensus reduction and multiplier updates stay ordered. Each wind-state evaluator remains serial inside its owning task, preventing nested teams and oversubscription. Immutable wind data, fixed-index output and counter-keyed random events preserve exact one/all-core scientific identity.

This design accelerates both the evolutionary layer and the coupled simulation/control layer. It does not rely on seed-level parallelism: one paper-native optimization uses the available cores internally.

## H3: implementation and correctness gates

The C++20 target is compiled with `-O3 -march=native -ffp-contract=off -Wall -Wextra -Wpedantic -Werror`. Unit tests cover all six profiles, wind-probability normalization, the 4D separation constraint, a heterogeneous-control no-wake sum transcribed independently, all five case roles, feasibility, monotone control/joint incumbents and one/four-worker scientific identity. The independent Python H5 validator checks profile cardinalities and paper anchors, semantic IDs, physical work receipts, complete control dimensions, role ordering, feasibility, improvement relations and exact one/four-worker case/hash identity.

The final local fixed-work H6 candidate used the complete declared `N=16`, 36-direction protocol. One worker took 14.426950820 s end to end and all 20 Spark workers took 1.387054290 s, an end-to-end speedup of 10.401143577. The isolated-layout, control and DBHM stages accelerated by 10.431652461, 11.409734883 and 10.195709789 respectively. All 20 workers participated; the five cases, controls, layouts, 12,401 complete-layout evaluations, 536,219 wind-state evaluations and scientific hash were exactly identical. These are development data, not immutable Waffle evidence.

## H4: formal campaign

Immutable Waffle H6 repeats the complete declared `N=16`, 36-direction run with one worker and every available Waffle core. The formal campaign then runs all six paper-native profiles once on all cores with ten PSO trials, population 40, 30 iterations, five control passes and 12 DBHM iterations. It records every Case 1-5 or illustration role, full layouts and controls, AEP, expected/no-wake power, efficiency, spacing, consensus, physical layout/state evaluations, stage timings, worker receipts, paper anchors and scientific hashes.

The runner is resumable per profile and binds every result to an immutable source commit and binary hash. Numerical differences from the paper's private MATLAB/FLORISSE adaptation are reported rather than calibrated away.

The final local all-core campaign gate passed all six profiles after correcting the Horns `7D` initial-lattice versus `4D` feasibility distinction. It performed 51,988 complete-layout evaluations and 8,977,948 single-wind-state evaluations in 277.890332634 summed in-binary seconds. In the 80-turbine, 12-direction case, AEP progressed from 1553.761 GWh initially to 1642.508 GWh after layout optimization, 1747.973 GWh after sequential control and 1826.166 GWh after joint DBHM while retaining at least 504 m separation. This is a local readiness gate; only the immutable Waffle snapshot is formal evidence.
