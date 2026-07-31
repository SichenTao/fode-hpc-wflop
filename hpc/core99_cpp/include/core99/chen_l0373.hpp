/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0373 joint layout-control problem and DBHM API
Paper title: Joint optimization of wind farm layout considering optimal control
Paper DOI: 10.1016/j.renene.2021.10.032. Target PDF SHA-256:
a0e5b3c2fe2a24e870e7b17c0220d71a644e96e6fd87b4087f3fb9d0c6555f50.
Paper source: arXiv 2107.11620 source archive, SHA-256
6c5dd1b686f0501051974d4d464a44ca5283847b34a459d6e4daf5535bcbef9c,
contains the complete TeX and original figure files but no executable DBHM.
Cited public dependency: TUDelft-DataDrivenControl/FLORISSE_M, MIT,
https://github.com/TUDelft-DataDrivenControl/FLORISSE_M, commit
36cb0a0295d2a1e05640fdbbcb9bb361ac8d592e, repository-archive SHA-256
451150ba62b242353f9bdaa5e5bef10cad5d63e0e3841d625838572d9f0c1f75,
Zenodo 10.5281/zenodo.4458669. It supplies the missing near-wake length
parameters and model lineage; it is not the target DBHM source.
Search result: exact-title, DOI, author, DBHM and GitHub searches on
2026-08-01 found no target implementation, optimizer settings, numeric wind
rose, initial/optimized coordinates, random states, traces or result archive.
Paper facts: Eqs.1--31; FLORIS Gaussian yaw/induction model; DBHM with PSO
warm start, scenario subproblems, analytic consensus coordination, multiplier
updates, PF=1e5, mu=10 and consensus tolerance10; NREL-5MW-scale D126m,
rho1.29, pp1.88, ad=-0.0356, bd=-0.01, ky=0.0229, yaw[-30,30]deg and
alpha[0.1,1/3]; three-turbine illustration; 16-WT 36/360-direction and
80-WT Horns-Rev 12/180-direction studies; ten PSO trials with the best used.
The paper explicitly solves DBHM scenario subproblems sequentially and states
that parallel implementation should provide further computational savings.
Missing information: target code and exact MATLAB/FLORISSE-M adaptations;
PSO swarm, coefficients and termination; subproblem interior-point settings;
rotor quadrature, x0/TI, exact 36-bin probabilities, rectangular bounds and
coordinates, Horns coordinates, all final layouts/controls and random seeds.
Conflicts/corrections: Eq.1 prints a positive Gaussian exponent; production
restores the physically required negative exponent as in FLORISSE-M. Eq.8
includes i=j and is infeasible for L>0; production evaluates unique i<j
pairs. Eq.15 likewise includes i=j and double-counts pairs; production uses
one penalty per violated unordered pair. Eq.3 has dimensionally ambiguous
empirical scaling relative to FLORISSE-M; target code is absent, so production
retains the printed two-dimensional expression and labels it paper-literal.
The paper's broad nonconvex-ADMM
convergence statement requires assumptions beyond compactness and linear
consensus, so this implementation claims deterministic stationary search,
not a global or unconditional convergence certificate.
Reconstruction: digitize and normalize the original 36-sector arXiv wind
rose, periodically resample it for 12/180/360 sectors; use a 4x4 layout on
the printed 1900x1700m rectangle and a public-formula 10x8 Horns parallelogram
with 7D initial side spacing and 7.2deg tilt while retaining the paper's 4D
minimum-distance constraint. Use the cited FLORISSE-M x0 completion and
fixed lateral rotor quadrature. Retain ten PSO warm-start trials; absent
budgets are declared as swarm40/30 iterations. A bounded projected pattern
solver completes the unspecified MATLAB interior-point call while preserving
the published DBHM decomposition, consensus equation and multiplier update.
Method semantic ID: l0373_pso_warm_dbhm_projected_declared_v1.
Problem semantic ID: l0373_joint_layout_yaw_induction_floris_declared_v1.
Protocol semantic ID: l0373_native_illustrative_16_36_360_80_12_180_v1.
Production backend: pure C++20 CPU-HPC. One persistent full-core executor
parallelizes complete PSO individuals and mutually independent wind-scenario
control/DBHM subproblems. Constants and wind trigonometry are immutable;
fixed-index writes, ordered commits and counter-keyed random events preserve
one/all-core scientific identity without nested teams or oversubscription.
Controlling contract: shared/contracts/core99_l0373_chen_2021.json.
Claim boundary: source-backed flexible academic reconstruction of the target
problem, DBHM and every target paper role; not author code, exact FLORISSE-M
adaptation, MATLAB trajectory, original private arrays or numerical replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::l0373 {

enum class ProfileId {
    illustrative_unrestricted,
    illustrative_4d,
    turbines16_directions36,
    turbines16_directions360,
    turbines80_directions12,
    turbines80_directions180,
};

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct WindState {
    double direction_degrees = 0.0;
    double probability = 0.0;
};

struct Controls {
    std::vector<double> yaw_degrees;
    std::vector<double> axial_induction;
};

struct Evaluation {
    bool feasible = false;
    double aep_gwh = 0.0;
    double expected_power_mw = 0.0;
    double no_wake_power_mw = 0.0;
    double efficiency_percent = 0.0;
    double minimum_distance_m = 0.0;
    double spacing_violation_squared_m2 = 0.0;
};

struct CaseResult {
    std::string role;
    std::vector<Point> layout;
    std::vector<Controls> controls_by_wind;
    Evaluation evaluation;
};

struct RunConfig {
    std::uint64_t seed = 37301;
    int workers = 20;
    int pso_trials = 10;
    int pso_population = 40;
    int pso_iterations = 30;
    int control_passes = 5;
    int dbhm_iterations = 12;
};

struct RunResult {
    std::string profile_id;
    std::string method_semantic_id =
        "l0373_pso_warm_dbhm_projected_declared_v1";
    std::string problem_semantic_id =
        "l0373_joint_layout_yaw_induction_floris_declared_v1";
    std::string protocol_semantic_id =
        "l0373_native_illustrative_16_36_360_80_12_180_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t parallel_regions = 0;
    std::uint64_t complete_layout_evaluations = 0;
    std::uint64_t single_wind_state_evaluations = 0;
    int pso_trials = 0;
    int pso_population = 0;
    int pso_iterations = 0;
    int dbhm_iterations_completed = 0;
    double final_consensus_violation_m = 0.0;
    double isolated_layout_stage_seconds = 0.0;
    double control_stage_seconds = 0.0;
    double dbhm_stage_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<CaseResult> cases;
};

class Problem {
public:
    struct Impl;

    explicit Problem(ProfileId profile);
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] ProfileId profile() const noexcept;
    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] double width_m() const noexcept;
    [[nodiscard]] double height_m() const noexcept;
    [[nodiscard]] double minimum_spacing_m() const noexcept;
    [[nodiscard]] const std::vector<WindState>& winds() const noexcept;
    [[nodiscard]] std::vector<Point> initial_layout() const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout,
        const std::vector<Controls>& controls_by_wind
    ) const;

private:
    std::unique_ptr<Impl> impl_;
    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config);
[[nodiscard]] std::vector<ProfileId> paper_profiles();
[[nodiscard]] std::string to_string(ProfileId value);

}  // namespace core99::l0373
