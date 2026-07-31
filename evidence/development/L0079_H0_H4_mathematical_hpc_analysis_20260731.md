# L0079 H0-H4 mathematical and HPC analysis

## Fact and authority boundary

- Target: Pillai et al., *Application of an offshore wind farm layout optimization methodology at Middelgrunden wind farm*, DOI `10.1016/j.oceaneng.2017.04.049`, target PDF SHA-256 `38f5f9b607d9d1d96fe72a6b1b7e57144691736a1dda368cd630762ebaacd863`.
- First-party completion source: A. C. Pillai, *On the optimization of offshore wind farm layouts* (2017 doctoral thesis), SHA-256 `d5eb7ae4cbbbc7beb4d1c838ba3528377fd51f4af8a4366e28d339721c9338be`.
- Direct predecessor sources: DOI `10.17736/ijope.2016.mmr16` and the authors' 2016 OMAE GA/PSO comparison. The target paper and thesis take precedence when versions differ.
- Pinned independent public data completion: Imperial College London's OpenWAKE revision `cdd23b9155e9d8181d6176a5702d167411d9bcc6`, used only for the 20 as-built coordinates, Bonus B76 power/thrust tables, and a public 12-sector Middelgrunden wind rose.
- Exact-title, DOI, author, GitHub and institutional-repository searches found the target PDF and thesis but no target-linked source, native input arrays, random state or numeric result archive.

The accepted claim is a source-backed flexible academic reproduction. It is not the authors' implementation and does not claim exact trajectory or exact-number replay.

## H0: complete paper-native scientific contract

The target pair contains one coupled real-site problem and six native optimizer/encoding roles:

1. adaptive GA with six-variable rectilinear-array encoding;
2. adaptive GA with a 628-variable binary position encoding;
3. adaptive GA with 40 continuous turbine-coordinate variables;
4. global-best PSO with the rectilinear-array encoding;
5. global-best PSO with the binary encoding and the paper's V-shaped transfer function;
6. global-best PSO with the continuous encoding.

All roles minimize LCOE for 20 Bonus B76-2000 turbines subject to the site boundary and 175 m minimum separation. The evaluation contains first-order G. C. Larsen wakes, root-sum-square wake superposition, 12 direction by 23 one-metre-per-second speed cells, electrical/cable loss, a capacitated cable-network surrogate, lifetime cost, AEP and LCOE. Paper parameters are population/swarm 100, maximum 1000 generations, GA adaptive crossover/mutation, 20% elitism, PSO dynamic boundary clamping, global topology, diversity at most 10%, no improvement for 50 generations, and the printed GA mean/best termination test. The thesis explicitly says every case/optimizer/constraint combination was a single run.

One physical FES is one complete LCOE evaluation of one decoded 20-turbine layout. GA pre-mutation child evaluations count because the authors' Algorithm 3.1 and thesis state that adaptive mutation requires that extra evaluation.

## H1: missing information, conflicts and deterministic completions

| Field | Source fact | Executable resolution |
|---|---|---|
| Candidate positions | Target says 100 m triangulation and 628 points; later thesis says 658 | Target-authoritative `journal_628` profile. A separate metadata-only `thesis_658` profile remains declared; formal target experiments use 628. |
| Site arrays | Target SCADA, polygon, bathymetry, obstacles and costs are not published as machine-readable arrays | A 5.7 km2 elongated ellipse centered on pinned as-built coordinates is the declared boundary proxy. A 100 m triangular lattice is clipped and boundary-depth ranked to the requested version count. No obstacle is invented. |
| Wind/turbine arrays | Target cites datasets but does not print the full arrays | Pinned OpenWAKE B76 and 12-sector tables; one fixed as-built AEP calibration factor maps the unavailable SCADA aggregate to the target's 95.41 GWh at 93% availability. |
| Cost model | Industry-calibrated empirical relationships, port, bathymetry, vessel and cable inputs are incomplete/proprietary | Preserve the target as-built lifetime-cost anchor; keep cable length/loss layout-dependent through deterministic capacity groups and MSTs; calibrate only the layout-independent residual. |
| Cable optimizer | Target uses pathfinding plus Gurobi CMST; obstacles, cable catalogue and MILP are absent | Public deterministic capacity-five angular groups plus exact per-group Euclidean MST. This is labelled an open CMST surrogate, not Gurobi identity. |
| GA operators | Adaptive equations and replace-weakest semantics are given; real-encoded crossover detail is not | Roulette selection from the direct predecessor; uniform crossover for every encoding; equal-count swap mutation for binary and bounded random-reset mutation for real genes. |
| PSO coefficients | Printed equation and tables omit tuned values and conflict with canonical stochastic PSO notation | Canonical global-best completion with inertia 0.9 to 0.4 and independent cognitive/social random factors with coefficients 2; no neighbourhood term or invented fourth force. |
| AEP hours | Target Eq. (2) prints 8766 hours | Preserve literal 8766 AEP and emit calendar-8760 audit AEP separately. |
| Repeats | “executed three times” can be misread as repeats | It names the three constraint modes. Thesis confirms one run per combination. |

## H2: mathematical work decomposition

For layout `X`, direction-speed cell `s`, turbine `i`, and cable edge `e`:

- geometry: pairwise projections `x_ij(s)` and `r_ij(s)`;
- wake: first-order Larsen deficit `D_ij(s)` from thesis Eqs. 5.9-5.15;
- turbine state: upstream-ordered effective speed with RSS superposition, B76 power and thrust interpolation;
- state power: sum turbine powers, then cable-loss correction;
- annual energy: `8766 sum_s p_s P_s(X)`;
- cable/cost: capacity-group MST length and calibrated layout-dependent lifetime cost;
- objective: discounted lifetime cost divided by discounted annual energy.

The array decoder maps six variables to a regular lattice and selects exactly 20 legal points. The binary decoder selects exactly 20 of 628 target-authoritative points. The continuous decoder maps 40 variables directly to coordinates. All three feed the same evaluator.

## H3: HPC transformations and correctness invariants

1. Precompute the 276 joint wind probabilities, B76 interpolation tables, fixed boundary data and 628 candidate positions once.
2. Within one layout, reuse the pairwise geometry across all speed states of the same direction.
3. Across a population/swarm, evaluate independent layouts on one persistent all-core C++ worker team. Do not create nested thread teams.
4. GA uses two explicit population-level evaluation phases: post-crossover (required by adaptive mutation) and post-mutation. Children are generated from a frozen parent generation and committed in index order.
5. PSO uses a frozen generation global best, then updates, repairs and evaluates particles independently before an ordered personal/global-best commit.
6. Counter-keyed random events assign every draw to `(generation, phase, individual, coordinate, draw)`, so scheduling cannot change scientific results.
7. Fixed-index output and ordered reductions require one-worker/all-worker equality for final layout, objective, FES and scientific hash.
8. The evaluator and algorithms report separate evaluator, algorithm and end-to-end time plus executor participation receipts.

## H4: admission, performance and formal experiment protocol

- H5 equation/semantic tests: target role count; 20 turbines; 628 candidates; 175 m spacing; literal 8766/calendar 8760 identities; as-built AEP/cost/LCOE anchors; GA/PSO role completion; exact schedule identity.
- H6 performance test: small fixed-work one-worker versus Waffle-all-worker comparison of the same C++ binary, seed, role and generation budget. This is a performance audit only, not the paper's formal result.
- Formal experiment: six paper-native roles, population/swarm 100, up to 1000 generations, source termination criteria, one deterministic seed per role because the paper reports one run. Emit the complete best layout, metrics, FES, generation count, convergence reason, timings and provenance.
- Formal acceptance requires H5 pass before the immutable Waffle snapshot is queued. Candidate local results never become formal evidence.
