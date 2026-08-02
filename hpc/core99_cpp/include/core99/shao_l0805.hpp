/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0805 PCE-Kriging-EI high-fidelity WFLO framework
Paper/DOI: Shao, Wang, Naung, Zhang, Yao and Zhou, Towards high-fidelity
wind farm layout optimization using polynomial chaos expansion and Kriging
model; 10.1016/J.ENERGY.2025.138820; arXiv:2502.11088v1.
Primary paper: local 12-page PDF SHA-256
eed96b6f8e1264a167fa2990e2c6a0437fcd29db762bbc1c5d3d0807c1113360.
Public source: https://arxiv.org/e-print/2502.11088, CC BY-NC-SA 4.0,
archive SHA-256
35cba7a5caf514416d09d0a5ed86ea5a6f11a0c899b6512a39f57cb11c751e33.
The archive provides the manuscript source and source figures but no program,
wind-condition array, CFD case, mesh, response table or random state.
Paper-provided facts: four NREL-5MW cases with D=126 m; 8/16/32/8 turbines
on 9x9/13x13/17x17/9x9 grids in 8D/12D/16D/8D squares; minimum spacing
2D; 72 direction by 22 speed low-fidelity wind states; 50 PCE samples;
5d initial Kriging layouts for cases I-III and 10d for case IV; second-order
Kriging trend; MSP then EI infill with EI tolerance 0.1; GA; target layout
evaluation totals 343/567/839/272; 30 repeats for cases I-III; and eight
directions, 2176 ADM-CFD calls and one target run for case IV.
Missing: author code and data; FLORIS/DAKOTA/OpenFOAM versions and input
files; modified simpleFoam ADM source, mesh and CFD fields; exact wind-rose
numbers, PCE basis/order selection, LHS arrays, Kriging kernel/options,
genetic operators, random seeds, layouts and per-run response tables.
Paper/source conflicts and mathematical gaps: printed EI Equation 21 uses
the current prediction where the historical best value is required by
Equation 20 and the cited standard EI definition. A complete quadratic trend
has (d+1)(d+2)/2 coefficients: 153/561/2145 for cases I/II/III, exceeding
the paper's 80/160/320 initial samples. Fifty PCE samples can identify at
most a total-degree-eight bivariate basis (45 terms), despite the stated
maximum polynomial order ten.
Case IV reports 108.51 MW for eight 5-MW turbines, exceeding the 40-MW
instantaneous farm limit; the printed value is therefore an unnormalized
multi-direction aggregate or a unit/label inconsistency, not mean farm power.
Reconstruction: use standard best-observed EI; an identifiable additive
quadratic trend 1,x_i,x_i^2; squared-exponential residual Kriging with a
declared nugget and deterministic concentrated-likelihood length-scale search
on the initial design, held fixed during rank-one infill. The surrogate mean
uses the complete fitted residual process; because the paper omits the DAKOTA
variance configuration, EI uses a disclosed conservative nearest-observation
correlation variance proxy instead of claiming DAKOTA posterior variance.
Data-driven empirical
orthogonal polynomials with cross-validated total degree at most eight;
common-random-number 50-point LHS; and the GA parameters reported by the
cited same-lineage predecessor 10.1016/j.oceaneng.2023.116644 (population
50, crossover 0.95, mutation 0.15, cap 1000), with declared operators.
The 72-bin direction distribution is independently reconstructed from the
licensed source figure and the 22-bin speed distribution is a declared
truncated-Weibull fit. Low fidelity uses an equation-level Gaussian wake
proxy. Missing ADM-RANS assets are represented by a separately labelled
asymmetric ADM/Gaussian response proxy; it is never called CFD.
Case-IV directions are averaged to a physically interpretable mean power and
AEP; the paper's 108.51/106.79 labels remain non-numerical aggregate anchors.
HPC realization: pure C++20; initial layout/scenario batches and GA
population inference and genetic updates use one persistent all-core team.
Kriging uses rank-one Cholesky extension and recursive additive-quadratic
trend updates, avoiding complete O(n^3) rebuilding after every infill while
retaining the same sample-conditioned surrogate lifecycle. Fixed reductions
and counter-keyed random events make one/all-core science identical.
Method semantic ID: l0805_pce_additive_quadratic_kriging_msp_ei_ga_v1.
Problem semantic IDs: l0805_case_i_n8_gaussian_proxy_v1;
l0805_case_ii_n16_gaussian_proxy_v1;
l0805_case_iii_n32_gaussian_proxy_v1;
l0805_case_iv_n8_adm_gaussian_proxy_v1.
Protocol semantic ID: l0805_native_30x3_plus_single_iv_v1.
Controlling contract: shared/contracts/core99_l0805_pce_kriging_2025.json.
Claim boundary: flexible equation- and lifecycle-level academic reproduction
of the four target problems and PCE-Kriging-MSP-EI-GA method on declared
Gaussian/ADM-Gaussian proxies; not author FLORIS, DAKOTA, OpenFOAM, CFD,
random stream, layouts, response arrays, numerical table or timing replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::l0805 {

using Layout = std::vector<int>;

struct Evaluation {
    double aep_gwh = 0.0;
    double mean_power_mw = 0.0;
    double minimum_spacing_margin_m = 0.0;
    bool feasible = false;
    int pce_degree = 0;
    std::uint64_t physical_wake_simulations = 0;
};

struct CaseSpec {
    std::string case_id;
    std::string problem_semantic_id;
    int turbines = 0;
    int grid_width = 0;
    int initial_layout_samples = 0;
    int target_layout_evaluations = 0;
    int wind_samples_per_layout = 0;
    int formal_repeats = 0;
    bool high_fidelity_proxy = false;
};

struct RunConfig {
    std::uint64_t seed = 2026080501ULL;
    int workers = 20;
    int initial_samples = -1;
    int target_truth_calls = -1;
    int maximum_ga_generations = 1000;
    bool smoke = false;
};

struct RunResult {
    std::string case_id;
    std::string problem_semantic_id;
    std::string method_semantic_id =
        "l0805_pce_additive_quadratic_kriging_msp_ei_ga_v1";
    std::string protocol_semantic_id =
        "l0805_native_30x3_plus_single_iv_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int selected_pce_degree = 0;
    int initial_samples = 0;
    int truth_calls = 0;
    int msp_infills = 0;
    int ei_infills = 0;
    std::uint64_t surrogate_fes = 0;
    std::uint64_t physical_wake_simulations = 0;
    Layout best_layout;
    Evaluation initial_best;
    Evaluation best_evaluation;
    std::vector<double> best_history_gwh;
    double selected_kernel_theta = 0.0;
    double evaluator_seconds = 0.0;
    double pce_seconds = 0.0;
    double surrogate_training_seconds = 0.0;
    double surrogate_inference_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

struct BatchReceipt {
    std::string case_id;
    int layouts = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t physical_wake_simulations = 0;
    double aep_checksum_gwh = 0.0;
    double seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(std::string case_id);
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] const CaseSpec& spec() const noexcept;
    [[nodiscard]] Evaluation evaluate(
        const Layout& layout,
        std::uint64_t seed,
        int pce_degree = 4
    ) const;
    [[nodiscard]] RunResult optimize(const RunConfig& config) const;
    [[nodiscard]] BatchReceipt profile_batch(
        int layouts,
        std::uint64_t seed,
        int workers
    ) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::vector<std::string> paper_case_ids();
[[nodiscard]] CaseSpec case_spec(const std::string& case_id);
[[nodiscard]] Layout perimeter_layout(const CaseSpec& spec);
[[nodiscard]] double expected_improvement(
    double prediction_mean,
    double prediction_standard_deviation,
    double best_observed
);

}  // namespace core99::l0805
