/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T68 offshore layout/power-dispatch co-design problem and
Adaptive Particle Swarm Optimization academic reconstruction API
Paper title: Offshore Wind Farm Layout Design Considering Optimized Power
Dispatch Strategy
Paper DOI: 10.1109/TSTE.2016.2614266
Paper-provided assets: equations for partial Jensen wakes and RSS multiple
wakes, NREL 5 MW turbine dimensions and operating speeds, hierarchical cable
losses, LPC, row/column spacing, farm rotation and per-turbine/per-time pitch
variables, APSO citation, penalty factor 1,000,000, 80-turbine 400 MW FINO3
case, 55 km export distance, 12 direction by 5 speed-bin wind rose, paper
roles, population sizes, iteration receipts, and 10/20-repeat statements.
Public source: exact-title, DOI, author, GitHub and repository searches on
2026-07-31 located no target source or machine-readable target arrays.
Cited open assets: Zhan et al. Adaptive Particle Swarm Optimization,
doi:10.1109/TSMCB.2009.2015956, author manuscript from the University of
Glasgow; Hou et al. 2015 predecessor, doi:10.1109/TSTE.2015.2429912, author
manuscript from Aalborg University; Lundberg 2003 report from Chalmers.
Missing information: target code and random states; numerical 60-state FINO3
array; cable-model coefficients, cable current limits and OAM/rate/lifetime;
the previous optimized-power-dispatch paper and its CP(beta,lambda) data;
beta maxima, time-series length TE and Num_c; optimized row/column intervals;
velocity bounds; maximum iteration for direction-only study; whether Table
III's 10-trial header or the text's 20 trials controls Scenarios I--IV.
Paper/source conflicts: the paper defines Opt4 at every turbine and time index
but publishes only an aggregated 12 by 5 wind rose; Table III says averages of
10 trials while the case-study text explicitly says 20 trials.
Reconstruction: digitize and normalize the 12 by 5 plotted wind rose; map each
published time-indexed pitch control to the 60 published wind states and
decode it as nonnegative pitch beyond the rotor-feasible MPPT baseline; use a
smooth NREL-rated-power/12.1-rpm-constrained CP(beta,lambda) completion and
induction-consistent CT (the common generic CP surface was rejected because
it contradicted the paper's near-lossless Scenario-III behavior); calibrate
the otherwise unavailable wind,
loss and cable scales to the paper's theta=0 Table-II anchors; use published
Scenario-II direction with 7D spacing as the declared fixed layout for
Scenario III; use beta in [0,45] degrees, a 20-percent range velocity bound,
100 direction-only iterations, the zero additional-pitch incumbent with
sparse local dispatch perturbations for pitch initialization, and the text-authoritative
20 scenario trials.
APSO retains Zhan et al.'s exact evolutionary factor, four fuzzy memberships,
sigmoid inertia, state-dependent c1/c2 adaptation, coefficient bounds and
normalization, and convergence-state Gaussian elitist learning.
Method semantic ID: t68_zhan_apso_offshore_codesign_declared_v1
Problem semantic ID: t68_fino3_layout_dispatch_lpc_5role_declared_v1
Protocol semantic ID: t68_native_10plus4x20_repeat_declared_v1
Production backend: pure C++20 CPU-HPC. A persistent all-core team evaluates
particles, builds the exact symmetric APSO distance matrix once per pair,
reduces row distances, and updates particles. Indexed counter RNG and ordered
best reductions preserve one/all-worker scientific identity. Formal runs use
concurrent paper-native repeats to occupy all 20 Waffle cores.
Controlling contract: shared/contracts/core99_t68_hou_2017.json
Claim boundary: academic flexible reconstruction of the target APSO and five
paper problem roles, not author code, native FINO3/cable/control arrays,
original random stream, or exact numerical optimum replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t68 {

enum class Role {
    direction_only,
    scenario_i_spacing,
    scenario_ii_spacing_direction,
    scenario_iii_pitch,
    scenario_iv_codesign,
};

struct WindState {
    double direction_deg = 0.0;
    double speed_mps = 0.0;
    double probability = 0.0;
};

struct Evaluation {
    double gross_energy_gwh = 0.0;
    double cable_loss_gwh = 0.0;
    double net_energy_gwh = 0.0;
    double cable_cost_mdkk = 0.0;
    double annualized_cost_mdkk = 0.0;
    double pitch_penalty_mdkk = 0.0;
    double lpc_dkk_per_mwh = 0.0;
    double theta_deg = 0.0;
    double minimum_spacing_m = 0.0;
    bool feasible = false;
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    int workers = 20;
    int population_override = 0;
    int iteration_override = 0;
    int unchanged_iterations = 50;
};

struct RunResult {
    std::string case_id;
    std::string method_semantic_id =
        "t68_zhan_apso_offshore_codesign_declared_v1";
    std::string problem_semantic_id =
        "t68_fino3_layout_dispatch_lpc_5role_declared_v1";
    std::string protocol_semantic_id =
        "t68_native_10plus4x20_repeat_declared_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int population_size = 0;
    int generations = 0;
    int dimensions = 0;
    std::uint64_t physical_fes = 0;
    bool converged = false;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<double> best_decision;
    Evaluation best_evaluation;
};

class Problem {
public:
    explicit Problem(Role role);

    [[nodiscard]] Role role() const noexcept;
    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] int dimensions() const noexcept;
    [[nodiscard]] int population_size() const noexcept;
    [[nodiscard]] int maximum_iterations() const noexcept;
    [[nodiscard]] int paper_repeats() const noexcept;
    [[nodiscard]] const std::vector<double>& lower_bounds() const noexcept;
    [[nodiscard]] const std::vector<double>& upper_bounds() const noexcept;
    [[nodiscard]] const std::vector<WindState>& wind_states() const noexcept;
    [[nodiscard]] std::vector<double> reference_decision() const;
    [[nodiscard]] Evaluation evaluate(const std::vector<double>& decision) const;

private:
    Role role_;
    std::string id_;
    int dimensions_ = 0;
    int population_size_ = 0;
    int maximum_iterations_ = 0;
    int paper_repeats_ = 0;
    std::vector<double> lower_bounds_;
    std::vector<double> upper_bounds_;
    std::vector<WindState> wind_states_;
};

[[nodiscard]] std::vector<std::string> paper_case_ids();
[[nodiscard]] std::string role_name(Role role);
[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config = {});

}  // namespace core99::t68
