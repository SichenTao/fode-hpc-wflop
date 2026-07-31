# Y09 H0-H4 mathematical and HPC analysis

## Fact and authority boundary

- Target: Li et al., *Wind Farm Layout Optimization with Multi-Type Wind Turbines for Minimizing Levelized Cost of Energy*, DOI `10.1016/j.renene.2025.124386`, PDF SHA-256 `970def94341cb1efeaa828e19d05cf0bb2bc8e1317c19b053f3446b13d496153`.
- First-party algorithm completion: Chongqing University patent CN117473875B, PDF SHA-256 `7f388b57c53ff9f5a97b43033b8771608d18f7a245796827537aa399ef6421fa`. Patent paragraph 156 gives population 100, crossover 0.08 and total mutation 0.01; paragraphs 152-155 give roulette and category-balanced mutation.
- Pinned public data completion: FLORIS-v2.4 NREL-5-MW CP/CT arrays and the public NREL/IEA-15-240 curve distributed with the Cazzaro VNS instances already admitted in this repository.
- Exact-title, DOI, author, GitHub and institutional searches found no target implementation, CFD arrays, random state, seeds, repeat ledger or numeric result archive.

The accepted result is a flexible academic reproduction, not an author implementation or exact-number replay.

## H0: complete paper-native contract

The problem has a 5 km square partitioned into 100 cells. Every categorical gene is empty, NREL 5 MW or IEA 15 MW. The source model uses the corrected three-dimensional Qian-Ishihara mean deficit and additional-turbulence equations, linear mean-deficit superposition, modified overlap-aware turbulence superposition, fatigue-derived maintenance, Mosetti construction cost and scalar LCOE. The inflow is steady 12 m/s at 150 m with a power-law exponent of 0.12 and IEC reference turbulence 0.12.

The twelve unique optimization roles are the three west-flow turbine compositions; northwest and southwest multi-type layouts; three additional fatigue thresholds 0.08, 0.12 and 0.16 around the shared 0.10 role; and four additional 5-to-15-MW construction-cost ratios 0.2, 0.3, 0.4 and 0.5 around the shared one-third role. Shared roles are run once, not duplicated merely because they appear in more than one figure.

One physical FES is one full mean-wake, turbulence, fatigue, maintenance, construction-cost and LCOE evaluation of a 100-cell layout.

## H1: missing information, conflicts and executable decisions

| Field | Source fact | Executable resolution |
|---|---|---|
| Site and turbine pair | Journal uses 5 km and 5/15 MW; earlier patent example uses 2 km and 2/5 MW | Journal controls the problem. Patent only supplies omitted GA settings. |
| GA settings | Journal gives roulette, crossover, mutation and variable-rate equations but omits numeric settings | First-party patent: population 100, crossover 0.08 and mutation 0.01. Single-point categorical crossover is declared because the exchange operator is not specified. |
| Stop and repeats | Journal says maximum iteration/convergence but supplies neither a numeric limit/tolerance nor repeat count | Fixed 1000 generations and one run per unique case are versioned completions. Formal work does not add an early-stop tolerance that can change the reported comparison. |
| Turbine curves | Journal prints curves only as a figure | Pinned cited-lineage public NREL 5- and 15-MW curves; never described as author arrays. |
| CFD fields | RSM/ADMR samples and fitted raw fields are unavailable | Implement published corrected analytical equations and their deterministic turbine-hub specialization. |
| Objective label | Narrative says multi-objective; Eq.39 is scalar LCOE | Minimize Eq.39 and report power, fatigue dispersion and maintenance as diagnostics. |
| Direction sign | Figure labels northwest 45 degrees and southwest -45 degrees | Register those literal flow angles and expose them in every result. |

## H2: mathematical work decomposition

For every occupied target turbine `i`, upstream turbine `j`, flow direction `theta` and categorical layout `x`:

1. transform the fixed cell coordinates to along-flow and cross-flow coordinates;
2. apply the paper's fifteen-diameter wake horizon;
3. evaluate corrected Qian-Ishihara `sigma`, maximum deficit `F`, Gaussian span `phi`, additional-turbulence `G`, `k3` and `k4`;
4. linearly sum velocity deficits and apply the full/partial-overlap correction before the root-sum-square turbulence reduction;
5. interpolate the type-specific power and thrust curves;
6. evaluate fatigue `P/Prate/(1+Mrep) + Ddis*I/(1+Mrep)`;
7. derive preventive and corrective maintenance from Eqs.30-35;
8. evaluate multi-type Mosetti construction cost and Eq.39 LCOE.

The categorical mutation probabilities satisfy the patent/journal constraint that the expected mutated counts from zero, type-one and type-two genes are equal. Empty-category divisions are handled only on source categories actually present; mutations from present categories can reintroduce a missing type.

## H3: HPC transformations and invariants

1. Construct the 100 grid centers, scenario transforms and turbine tables once.
2. Sort only occupied turbines and prune all sources outside the fifteen-diameter horizon.
3. Keep one layout evaluation serial internally to avoid nested thread teams; distribute independent population layouts across one persistent all-core executor.
4. Compute roulette prefix weights once per generation instead of once per child.
5. Generate offspring in parallel from a frozen parent population. Every random event is keyed by generation, phase, child and gene.
6. Write each offspring and evaluation to a fixed index, then replace the generation in deterministic index order.
7. Record evaluator, algorithm and end-to-end time, FES, thread participation, final layout and scientific hash.
8. Require one-worker/all-worker identity for final layout, metrics, physical FES and hash.

The dominant layout work is sparse wake interaction plus overlap correction; the dominant campaign-level parallelism is population evaluation. Algorithm construction is also parallel, but its shorter loops must share the persistent team to prevent thread-creation slowdown.

## H4: admission and formal protocol

- H5 checks all twelve roles, 100 categorical positions, turbine restrictions, atmosphere laws, mutation expectation, LCOE feasibility, exact FES and one/four-worker identity.
- H6 compares the same pure-C++ executable, seed, population 100 and fixed generations using one Waffle worker versus all Waffle workers for all twelve cases. It is a performance audit, not a MATLAB or author-code claim.
- Formal work runs the twelve unique cases once each with population 100, crossover 0.08, mutation 0.01 and exactly 1000 generations on all Waffle cores.
- Formal output includes layout/type counts, power, fatigue dispersion, construction and maintenance costs, LCOE, FES, generations, timing, worker receipt and provenance.
- Local candidate results remain admission evidence until reproduced from an immutable Waffle snapshot.
