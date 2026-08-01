# L0805 HPC analysis: PCE-Kriging-EI high-fidelity WFLO

## Paper pair and evidence boundary

- Target: Shao et al., *Towards high-fidelity wind farm layout optimization using polynomial chaos expansion and Kriging model*, DOI `10.1016/j.energy.2025.138820`.
- Target method: PCE annual-energy approximation, Kriging response surface, maximum-surrogate-prediction and expected-improvement infill, and GA acquisition optimization.
- Target problems: four NREL-5MW grid layouts with 8, 16, 32 and 8 turbines; the first three use low-fidelity wind integration and the fourth uses an ADM-RANS model in the paper.
- Public asset: arXiv `2502.11088v1`, CC BY-NC-SA 4.0, archive SHA-256 `35cba7a5caf514416d09d0a5ed86ea5a6f11a0c899b6512a39f57cb11c751e33`. It contains manuscript source and figures, but no program, wind array, CFD case, mesh, trained surrogate or responses.

The target implementation is therefore a flexible equation- and lifecycle-level reproduction. Cases I-III use a declared Gaussian wake proxy. Case IV uses a separately labelled asymmetric ADM-Gaussian proxy because the modified OpenFOAM solver, 626k-cell mesh and eight CFD states are unavailable. Neither proxy is represented as author FLORIS or ADM-CFD.

## Modeling and mathematical audit

The paper specifies 72 wind directions, 22 speeds, 50 PCE samples, a second-order Kriging trend and GA-based MSP/EI infill. Four ambiguities require executable resolutions.

1. Equation 21 prints the current prediction where Equation 20 and standard expected improvement require the historical best observation. The implementation uses standard best-observed EI.
2. A full quadratic trend in dimension `d` has `(d+1)(d+2)/2` terms. Cases I-III have `d=16,32,64`, hence 153, 561 and 2145 terms, but only 80, 160 and 320 initial layouts. The implementation uses the identifiable additive quadratic basis `1,x_i,x_i^2`, with 33, 65 and 129 terms.
3. A total-degree-ten bivariate PCE has 66 terms, but the paper supplies 50 samples. The largest identifiable total degree is eight, with 45 terms. Five-fold cross-validation therefore searches degrees one through eight.
4. The Kriging kernel and DAKOTA posterior-variance configuration are absent. The mean uses an additive quadratic trend plus a fitted squared-exponential residual process. EI uses a disclosed conservative nearest-observation squared-correlation variance proxy; no DAKOTA variance replay is claimed.
5. Case IV reports 108.51 MW for eight 5-MW turbines, although instantaneous farm power cannot exceed 40 MW. This must be an unnormalized eight-direction aggregate or a unit/label inconsistency. The implementation averages its eight directions, reports physically interpretable mean power and AEP, and retains 108.51/106.79 only as paper-label anchors; the positive constant scaling does not change layout ranking.

The direction distribution is independently reconstructed from the licensed source figure. The speed distribution is a declared 22-bin truncated-Weibull fit. The direction polynomial coordinate is wrapped around the dominant 225-degree sector before empirical Gram-Schmidt orthogonalization, avoiding an artificial 0/360-degree discontinuity.

## H0 complexity and bottlenecks

Let `N` be turbines, `S` physical wind states per layout, `M` accumulated truth layouts, `P=50` the GA population and `G` the GA generation count.

- one Gaussian wake state costs `O(N^2)` pair work;
- one low-fidelity PCE truth call costs `O(S N^2 + S B^2 + B^3)`, with `S=50` and at most `B=45` basis terms;
- the paper's full 72-by-22 reference quadrature costs 1,584 wake states and is used only for H5 accuracy checks;
- a naive Kriging rebuild after each infill costs `O(M^3)`; a rank-one Cholesky append costs `O(M^2)`;
- additive-trend recursive least squares costs `O(N^2)` in the trend dimension rather than refitting all observations;
- one GA acquisition generation costs `O(P M d)` for surrogate inference plus independent genetic updates.

The paper-native physical work totals are 17,150, 28,350, 41,950 and 2,176 wake-state simulations per run for Cases I-IV. The complete protocol contains 91 target runs.

## H1-H4 implementation

- H1: precompute deterministic wind weights, counter-key every LHS and GA event, and store each layout in a fixed output slot.
- H2: evaluate the initial layout batch and every GA population on one persistent all-core C++ team. Non-elite crossover, mutation and feasibility repair also run in independent fixed slots.
- H3: choose the squared-exponential length scale once on the initial design by deterministic concentrated likelihood, append each Kriging sample with a rank-one Cholesky extension, and update the additive trend recursively. The implementation does not rebuild the entire surrogate after every truth call.
- H4: retain ordered science reductions and immutable surrogate reads within each parallel population. One-core and all-core executions consume the same counter-keyed random events and produce the same layouts, convergence history and scientific hash.

The optimizer does not create short nested thread teams. Initial truth layouts form one batch parallel region; each acquisition generation has one inference region and one offspring region. Timing separates wake evaluation, PCE fitting, surrogate training, surrogate inference and residual algorithm work.

Each continuous layout-LHS coordinate uses a multiplier coprime to the requested sample count, so its pre-snap stratum sequence is a true permutation rather than a repeated modular cycle. Grid snapping and 2D-feasibility repair are then explicit discrete decoding operations; they are not falsely described as preserving exact marginal Latinness after repair.

## H5 gates

1. All four paper case sizes, initial sample counts, target truth-call counts, wind-state counts and repeat counts match the paper contract.
2. Every generated layout contains the required number of distinct grid nodes and satisfies the `2D` minimum spacing.
3. The corrected standard EI equation passes analytical spot checks.
4. Degree-four PCE AEP on deterministic layouts agrees with the complete reconstructed 72-by-22 integration within 5%; observed local relative errors are 0.383%, 0.935% and 2.133% for Cases I-III.
5. One-core and all-core batches and smoke optimizations produce identical scientific hashes, layouts, histories and objective values.
6. The all-core implementation reports more than one observed worker and never reduces the best-observed AEP.

## H6 and formal protocol

H6 compares one CPU worker with every available Waffle CPU worker on the identical Case-III batch of 320 layouts, each using 50 common-random PCE wake states. Each configuration runs three times and reports the median wall time. The final local provisional release campaign measured 0.09071 s with one worker and 0.007774 s with 20 workers, an 11.67-fold batch speedup; Waffle results remain authoritative.

The formal campaign runs Cases I-III with the paper's 30 independent repetitions and Case IV once, for 91 target runs. Every run uses all available Waffle cores and records truth calls, physical wake-state simulations, surrogate FES, stage wall times, best-history data, worker participation, source commit, binary hash and scientific hash. Paper-reported objective values remain qualitative external anchors because the unavailable wind and CFD assets prevent numerical replay.
