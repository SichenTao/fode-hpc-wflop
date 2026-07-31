# L0298 H0-H4 mathematical and HPC analysis

## H0: source and semantic audit

The controlling paper is Tao et al., DOI `10.1109/TSG.2020.3022378`, PDF SHA-256 `99057161f4efdb2cf0c8a72b82890bba6c7a4ff67fc3c2d71509129e5b8609e6`. Exact-title, DOI, author and repository searches found no target MATLAB project, raw front, layout archive or replay package. The paper says its NSGA-III and BPSO code came from Yarpiz. The identified NSGA-III asset is YPEA126, BSD-2-Clause, pinned at commit `a6e206086cdf1e29c0ae29c2699bef85df728181`; it is a generic optimizer, not the target wind project. No corresponding paper-matching Yarpiz BPSO package was found. The public continuous YPEA102 PSO is not relabeled as binary source.

The IEEE RTS-24 generator/load lineage is independently auditable through MATPOWER `case24_ieee_rts.m`, pinned at commit `5f1b70611a573f5455de7a2e5786aed12adfbaf8`. It completes published test-system tables only and is not represented as the authors' missing network modifications. The raw Greater-Gabbard observations, machine-readable Figs. 3-5, emission and price-correlation coefficients, several cable constants, random states and optimizer traces are absent.

Four material paper ambiguities are resolved explicitly. Equation 37's printed spacing expression conflicts with the case study's explicit 500 m minimum and 500 m grid; the experimental definition controls. Model 2 is fixed at 210 MW and Table VI reports 210 MW, while Fig. 7 labels a 75-turbine solution; the consistent 70-turbine definition controls. Equation 37 lists AC-network constraints, while the stated `quadprog` inner model in Eqs. 42-43 retains active balance, reserve and generator bounds only; production implements the reproducible convex QP semantics and does not claim AC-OPF. The reserve symbol has no value; 50 MW is a declared completion that retains feasibility of all six reported connection buses.

The 24-hour wind/load curves are deterministic figure digitizations. The grid is encoded by one real key per cell plus a capacity key and stable top-N decoding. For three objectives, 14 simplex divisions generate exactly 120 reference directions, matching the paper population. BPSO uses the exact Table V coefficients over a compact binary parent-edge encoding. Restricting parents to nodes closer to the central substation guarantees an acyclic connected tree; subtree power determines cable feasibility and the least daily-cost type. Every numerical completion is versioned and is not calibrated to reproduce Tables VI-IX.

## H1: mathematical work decomposition

One decoded outer candidate contains 60-80 occupied cells. Its 24-hour Gaussian wake work is `O(24 N^2)`. The convex economic dispatch uses a fixed 32-unit RTS list and 80 bisection steps per hour. The cable decoder uses seven bits per turbine; one BPSO particle is decoded and scored in `O(N)`. Thus the nested candidate cost is dominated by `O(P_b (G_b+1) N + 24 N^2)` with paper values `P_b=100` and `G_b=250`.

YPEA126 semantics yield 60 blend-crossover children and 60 Gaussian mutants per generation for population 120. Initial evaluation plus 250 generations gives 30,120 complete candidates per optimization context. There are eleven unique contexts per seed: three planning models, two alternate turbines, summer, and five non-base buses. Re-evaluating the 29 selected paper roles with retained cable trees gives 331,349 complete outer evaluations, 8,316,859,900 BPSO particle evaluations and 7,952,376 hourly wake evaluations per formal seed. These physical counts are reported rather than hidden behind the number 250.

## H2: high-performance design

One optimization owns one persistent worker team. Complete outer candidates are independent and occupy fixed output slots; their inner cable/wake/dispatch calculation remains serial to avoid nested teams. Dominance rows and NSGA-III reference association reuse the same team. Immutable grid, turbine, cable, generator, wind/load and reference data avoid repeated parsing. Counter-keyed random events assign each outer and inner event to a logical index, while survivor selection and normalization commits remain ordered. The design therefore accelerates a single optimization internally and preserves exact one/all-core scientific identity.

The nested BPSO budget is the dominant research cost, not the wake evaluator alone. H6 must report evaluator, orchestration and end-to-end speedups against the same C++ program with one versus every Waffle core. If the full nested budget is prohibitively slow, that performance fact is evidence about the paper method; it must not be silently replaced by a proxy or smaller formal budget.

## H3: implementation and correctness gates

The C++20 target is compiled with `-O3 -march=native -ffp-contract=off -Wall -Wextra -Wpedantic -Werror`. Unit tests cover all nine profiles and 29 roles, capacity/cardinality, radial cable cardinality, exact BPSO physical work, finite coupled metrics, fixed-210-MW Model 2 and one/four-worker identity. The independent Python H5 validator executes every profile, validates all 29 role names and dimensions, checks cable and layout receipts, and compares full one/four-worker scientific projections.

The strict C++ and independent H5 tests passed on 2026-08-01. A reduced end-to-end runner smoke completed nine profiles and 29 roles. These are development gates, not formal performance or paper-quality results.

## H4: immutable Waffle protocol

H6 first runs the complete three-model base profile at outer population 120 for five generations and BPSO population 100 for twenty generations with one worker and every Waffle core. It requires exact identity of all seven roles, layouts, cable trees, objectives, physical work and hash, plus end-to-end acceleration.

After H6 admission, the resumable formal lane runs nine profiles for 25 seeds on every Waffle core using the paper maxima: outer NSGA-III 120 by 250 and inner BPSO 100 by 250. Each JSON result binds the immutable source commit and binary SHA-256 and contains full selected layouts/cable trees, all coupled metrics, work counts, stage timings, worker receipts and scientific hash. The runner writes a partial summary after every profile so a stopped campaign resumes without repeating completed work.
