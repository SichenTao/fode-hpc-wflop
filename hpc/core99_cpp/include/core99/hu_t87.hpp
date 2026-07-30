/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T87 Qianjiang complex-terrain proxy and IGA-PSO
Paper/DOI: Wind Farm Layout Optimization in Complex Terrain Based on CFD and
IGA-PSO; 10.1016/j.energy.2023.129745
Paper-provided facts: WD156-3300 with D=156 m, hub height 100 m, rated power
3.3 MW, cut-in/rated/cut-out speeds 3/11.5/20 m/s; 20D by 92D layout region;
16 wind directions; AEH>=2000 h prefilter; 6.9% feasible grid locations;
4D spacing; Jensen/GWM/DGWM plus RSS superposition; AEP/NAV cases; IGA
population 300, Pc=0.9, mutation 0.1->0.2, 1000 iterations; PSO population
100, inertia 0.4->0.9, 200 iterations and +/-D coordinate neighborhoods.
Public source: the paper explicitly says the authors do not have permission
to share data. No author code/data archive was located at the publisher,
paper links, or public-code search.
Direct method predecessor: Hu et al., DOI 10.1016/j.energy.2022.123970,
supplies the 0.5D grid, per-individual Bernoulli initialization probability,
roulette selection, unique parallel fitness evaluation, variable-mutation
formula and 4D spacing. Local PDF SHA-256:
1bcc30b835c9d44c36a2321838939684656c40889564504d6a7eb5a12554db2a.
Direct DGWM predecessor: Keane, DOI 10.1016/j.renene.2021.02.078, supports a
representative rmin/r0=0.55. Local PDF SHA-256:
c4ff88b3991d0af44e2fbd7d73014cc84649f59672b2a30236fd2c3ca483f842.
Missing/conflicts: private measured series, private CFD/terrain arrays,
author seeds/code/layouts, kd, rmin, PSO c1/c2, crossover cut law, mutation
granularity and every numerical NAV input are omitted. Fig. 2 displays Ct>1
at low speeds although Eqs. (16)-(18) contain sqrt(1-Ct). Table 2 reports
case-2 IGA-PSO as both 1.3881e5 and 1.3804e5 MWh.
Reconstruction: a hashed Fig. 9 extraction yields 522/7585=6.882% candidate
points; visible neighboring colors rank AEH and anchor it to 2000-2500 h.
Hashed Figs. 2 and 5 provide the turbine curves and 49 normalized direction-
speed states. Spatial speed multipliers are solved so each candidate's
no-wake AEP exactly matches its proxy AEH. Use z0=0.037 m inherited from the
direct predecessor, kd=0.5/log(100/z0), rmin/r0=0.55 from Keane, c1=c2=2,
single-point crossover, one random bit when an individual-level mutation
event fires, one preserved elite, and a Table-2-scale declared NAV completion
of 0.38 RMB/kWh net energy value minus 2.00e6 RMB/turbine/year aggregate
annualized cost. Retain digitized Ct but clamp to 0.999 only inside wake
square roots and report that fact. Fig. 2/5/9 fixture SHA-256:
e8fbb28bd24f97aaef4923907e03d91afd4b4e07adb4a370c2d2c9bc2f13ea8a.
Problem semantic ID: t87_qianjiang_figure_proxy_v1
Method semantic ID: t87_iga_pso_predecessor_completed_v1
Production backend: pure C++ CPU. Candidate wake coefficients, terrain
speed scales and geometry are precomputed; a persistent full-core team
evaluates unique IGA layouts and PSO particles in parallel; all hot buffers
are reused and random events are counter-keyed for schedule-independent
replay.
Controlling contract: shared/contracts/core99_t87_hu_iga_pso_2024.json
Claim boundary: academic declared reproduction of the published problem and
IGA-PSO on a versioned image-derived Qianjiang proxy; not author CFD,
measured data, source code, exact random state or exact numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t87 {

enum class WakeModel {
    jensen,
    gaussian,
    double_gaussian,
};

enum class ObjectiveModel {
    aep,
    nav,
};

struct Candidate {
    double x_d = 0.0;
    double y_d = 0.0;
    double aeh_h = 0.0;
    double speed_multiplier = 1.0;
};

struct WindState {
    double direction_degrees = 0.0;
    double speed_mps = 0.0;
    double probability = 0.0;
};

struct Evaluation {
    double fitness = 0.0;
    double aep_mwh = 0.0;
    double nav_rmb_per_year = 0.0;
    double wake_efficiency = 0.0;
    double total_normalized_constraint_violation = 0.0;
    int turbine_count = 0;
    bool feasible = false;
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    int workers = 20;
    int iga_population = 300;
    int iga_generations = 1000;
    int pso_population = 100;
    int pso_iterations = 200;
};

struct RunResult {
    std::string case_id;
    std::string problem_semantic_id;
    std::string method_semantic_id;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int iga_population = 0;
    int iga_generations = 0;
    int pso_population = 0;
    int pso_iterations = 0;
    std::uint64_t proposed_fes = 0;
    std::uint64_t physical_unique_fes = 0;
    Evaluation best_grid_evaluation;
    Evaluation best_continuous_evaluation;
    std::vector<int> best_grid_candidate_indices;
    std::vector<double> best_continuous_coordinates_d;
    std::vector<double> best_fitness_history;
    double precomputation_seconds = 0.0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    Problem(
        std::string case_id,
        const std::string& proxy_path,
        int precomputation_workers = 20
    );

    [[nodiscard]] const std::string& case_id() const noexcept;
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] WakeModel wake_model() const noexcept;
    [[nodiscard]] ObjectiveModel objective_model() const noexcept;
    [[nodiscard]] const std::vector<Candidate>& candidates() const noexcept;
    [[nodiscard]] const std::vector<WindState>& wind_states() const noexcept;
    [[nodiscard]] int turbine_curve_point_count() const noexcept;
    [[nodiscard]] double wind_probability_sum() const noexcept;
    [[nodiscard]] double precomputation_seconds() const noexcept;

    [[nodiscard]] Evaluation evaluate_candidate_indices(
        const std::vector<int>& candidate_indices
    ) const;
    [[nodiscard]] Evaluation evaluate_coordinates_d(
        const std::vector<double>& coordinates_d
    ) const;

private:
    struct TurbineCurvePoint {
        double speed_mps = 0.0;
        double normalized_power = 0.0;
        double thrust_coefficient = 0.0;
    };

    std::string case_id_;
    std::string semantic_id_ = "t87_qianjiang_figure_proxy_v1";
    WakeModel wake_model_ = WakeModel::jensen;
    ObjectiveModel objective_model_ = ObjectiveModel::aep;
    std::vector<Candidate> candidates_;
    std::vector<WindState> wind_states_;
    std::vector<TurbineCurvePoint> turbine_curve_;
    std::vector<float> candidate_deficit_ratio_;
    int precomputation_workers_ = 20;
    double precomputation_seconds_ = 0.0;

    void load_proxy(const std::string& proxy_path);
    void configure_case();
    void calibrate_candidate_speed_multipliers();
    void precompute_candidate_deficits();
    [[nodiscard]] double normalized_power(double speed_mps) const;
    [[nodiscard]] double thrust_coefficient(double speed_mps) const;
    [[nodiscard]] double spatial_speed_multiplier(
        double x_d, double y_d
    ) const;
    [[nodiscard]] double wake_deficit_ratio(
        double downstream_d,
        double crosswind_d,
        double thrust_coefficient
    ) const;
    [[nodiscard]] Evaluation finish_evaluation(
        const std::vector<double>& coordinates_d,
        const std::vector<double>& local_speed_multipliers,
        bool use_candidate_precomputation,
        const std::vector<int>& candidate_indices
    ) const;
};

[[nodiscard]] RunResult run(
    const Problem& problem,
    const RunConfig& config
);

[[nodiscard]] std::string wake_model_name(WakeModel model);
[[nodiscard]] std::string objective_model_name(ObjectiveModel model);

}  // namespace core99::t87
