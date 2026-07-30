/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0341 3-D Gaussian wake, uniform/nonuniform WF problems
and mixed-discrete particle swarm optimization
Paper: Tao, Xu, Feijóo, Zheng and Zhou, Wind farm layout optimization with
a three-dimensional Gaussian wake model; 10.1016/j.renene.2020.06.003.
Primary PDF SHA-256:
385658381f3af634dc47ceafef8d03ece5cd6c0084eb0289da4810941b72fbd8.
Paper-provided facts: 3-D wake Eqs. (1)-(15), power/CF/efficiency Eqs.
(16)-(19), uniform/nonuniform decision models Eqs. (20)-(24), MDPSO Eqs.
(25)-(27), three farm areas/capacities, three turbine specifications, three
wind scenarios, a=0.5, beta_l=beta_g=1.4, gamma_c=10, population
20 times decision dimension, 15000/20000 iterations, and 5D spacing.
Public source: no paper-linked implementation or machine-readable Figs.
10-11 was located. The direct MDPSO predecessor
10.1007/s00158-012-0851-z was legally recovered, PDF SHA-256
5ea0011e876de7da09a70847fa6bc3bb1409dc43095e9c31451dd531bdcefeb1.
The already recovered 3-D wake predecessors are
10.1016/j.apenergy.2018.06.027 and 10.1016/j.renene.2019.08.122.
Missing/conflicts: target omits initialization/seeds/repeats, exact velocity
bounds, system-constraint handling, variable-cardinality encoding, turbine
power/thrust curve arrays, Fig. 11 numeric JPDF, C/I0/calibration, rotor
quadrature and boundary equality behavior.
Reconstruction: source-backed nearest-domain rounding and Deb comparison;
counter-keyed low-discrepancy-like initialization; zero denotes an inactive
nonuniform slot and exact capacity is deterministically restored; Fig. 11
uses the versioned Mosetti/Grady 36x3 joint distribution already audited in
this platform; turbine curves are logistic completions from Tables 2/4;
C=5.15,
a=1/3 and onshore wake expansion 0.075 are isolated model completions.
The equations print strict >5D while the paper diagnostic uses exactly 5D;
the visible case takes precedence and at-least-5D spacing is implemented.
Jensen/J-G wake models are comparison baselines and are not target packages.
Problem semantic ID: l0341_three_farm_3d_gaussian_v1.
Method semantic ID: l0341_mdpso_predecessor_completed_v1.
Protocol semantic ID: l0341_10case_25repeat_paper_iterations_v1.
Production backend: pure C++ CPU. Wind states are factored by direction,
layout evaluation and particle motion use one persistent full-core team,
and counter-keyed events preserve one/all-core trajectories.
Contract: shared/contracts/core99_l0341_tao_3d_mdpso_2020.json.
Claim boundary: academic declared reproduction of the target 3-D model,
MDPSO and ten unique optimization combinations; not author source, exact
figures/data/curves, exact variable-cardinality encoding or numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::l0341 {

struct Turbine {
    int type = 0;
    double x_m = 0.0;
    double y_m = 0.0;
};

struct Evaluation {
    double expected_power_mw = 0.0;
    double capacity_factor_percent = 0.0;
    double efficiency_percent = 0.0;
    double installed_capacity_mw = 0.0;
    double minimum_spacing_margin_m = 0.0;
    double constraint_violation = 0.0;
    int active_turbines = 0;
    bool feasible = false;
};

struct RunConfig {
    std::uint64_t seed = 2026034100;
    int workers = 20;
    int generations = -1;
    int population_override = -1;
};

struct RunResult {
    std::string case_id;
    std::string problem_semantic_id;
    std::string method_semantic_id;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int population = 0;
    int generations = 0;
    std::uint64_t physical_fes = 0;
    Evaluation initial_best;
    Evaluation best_evaluation;
    std::vector<Turbine> best_layout;
    std::vector<double> best_power_history_mw;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
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

    [[nodiscard]] const std::string& case_id() const noexcept;
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] bool nonuniform() const noexcept;
    [[nodiscard]] const std::string& wind_scenario() const noexcept;
    [[nodiscard]] int maximum_slots() const noexcept;
    [[nodiscard]] int decision_dimension() const noexcept;
    [[nodiscard]] int paper_population() const noexcept;
    [[nodiscard]] int paper_generations() const noexcept;
    [[nodiscard]] double width_m() const noexcept;
    [[nodiscard]] double height_m() const noexcept;
    [[nodiscard]] double capacity_mw() const noexcept;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Turbine>& layout
    ) const;
    [[nodiscard]] RunResult optimize(const RunConfig& config) const;

private:
    friend Evaluation evaluate_diagnostic_4x4(double, double);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::vector<std::string> paper_case_ids();
[[nodiscard]] std::vector<Turbine> diagnostic_4x4_layout();
[[nodiscard]] Evaluation evaluate_diagnostic_4x4(
    double reference_speed_mps,
    double direction_degrees
);

}  // namespace core99::l0341
