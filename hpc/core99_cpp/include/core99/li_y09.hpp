/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y09 multi-type turbine LCOE problem and ternary GA
Paper/DOI: Li et al., Wind Farm Layout Optimization with Multi-Type Wind
Turbines for Minimizing Levelized Cost of Energy;
10.1016/j.renene.2025.124386. Target PDF SHA-256:
970def94341cb1efeaa828e19d05cf0bb2bc8e1317c19b053f3446b13d496153.
Public assets: final publisher article at
https://doi.org/10.1016/j.renene.2025.124386; author patent
CN117473875B, https://patents.google.com/patent/CN117473875B/zh, PDF
SHA-256 7f388b57c53ff9f5a97b43033b8771608d18f7a245796827537aa399ef6421fa.
Exact-title, DOI, author, GitHub and institutional searches found no public
target implementation, CFD arrays, turbine curves, seeds or result archive.
Paper-provided facts: corrected three-dimensional Qian-Ishihara mean-wake
and turbulence equations; linear velocity-deficit and modified turbulence
superposition; fatigue-derived preventive/replacement maintenance; Mosetti
construction cost; ternary 0/5-MW/15-MW grid GA; 5 km square, 10 by 10
candidate grid; steady 12 m/s at 150 m; roughness exponent 0.12; Iref 0.12;
three turbine-composition cases, three directions, four fatigue thresholds
and four additional construction-cost ratios.
Missing information: author source and random state; CFD calibration arrays;
printed NREL-5-MW and IEA-15-MW power/thrust samples; GA population,
crossover, mutation, recombination operator, iteration limit, convergence
tolerance and repeat count in the journal paper.
Completion: the first-party patent supplies population 100, crossover 0.08,
total mutation 0.01, roulette selection and the published category-balanced
mutation equations. The journal problem overrides the patent's older 2 km
and 2/5-MW example. Public pinned FLORIS-v2.4 NREL-5-MW and Cazzaro public
NREL/IEA-15-240 curves complete the unavailable figure arrays. Single-point
crossover, a fixed 1000-generation formal budget and one run per native case
are explicit deterministic completions; formal results do not use an invented
early-stop tolerance.
Conflict: the journal attributes the 2026 problem to 5/15-MW turbines on a
5 km grid, while the 2023 patent example uses 2/5-MW turbines on 2 km; the
journal controls the problem and the patent only completes omitted GA
settings. The paper calls the framework multi-objective but optimizes the
single scalar LCOE in Eq.39; this implementation follows Eq.39.
Method semantic ID: y09_ternary_variable_mutation_ga_declared_v1.
Problem semantic ID: y09_multitype_mqi_fatigue_lcoe_declared_v1.
Protocol semantic ID: y09_native_12case_single_run_declared_v1.
Controlling contract: shared/contracts/core99_y09_li_multitype_2026.json
Production backend: pure C++20 CPU-HPC with immutable grid/direction/turbine
precomputation, sparse upstream wake lists, one persistent all-core team,
parallel offspring construction and population evaluation, frozen-generation
updates, fixed-index writes, ordered commits and counter-keyed random events.
Claim boundary: source-backed flexible academic reproduction of the complete
journal problem, GA and twelve unique paper-native optimization cases; not
author source, proprietary CFD data, exact plotted arrays, random trajectory
or numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::y09 {

enum class Composition { five_only, multi_type, fifteen_only };

struct Scenario {
    std::string case_id;
    Composition composition = Composition::multi_type;
    double flow_direction_degrees = 0.0;
    double fatigue_threshold = 0.10;
    double five_to_fifteen_cost_ratio = 1.0 / 3.0;
};

struct TurbineRecord {
    int grid_index = -1;
    int type_code = 0;
    double x_m = 0.0;
    double y_m = 0.0;
    double effective_speed_mps = 0.0;
    double turbulence_intensity = 0.0;
    double power_mw = 0.0;
    double fatigue_coefficient = 0.0;
    double maintenance_cost_units = 0.0;
};

struct Evaluation {
    bool feasible = false;
    int five_mw_turbines = 0;
    int fifteen_mw_turbines = 0;
    double total_power_mw = 0.0;
    double construction_cost_units = 0.0;
    double maintenance_cost_units = 0.0;
    double lcoe_units_per_mw = 0.0;
    double fatigue_standard_deviation = 0.0;
    double average_maintenance_cost_units = 0.0;
    std::vector<TurbineRecord> turbines;
};

struct MutationProbabilities {
    double zero = 0.0;
    double five = 0.0;
    double fifteen = 0.0;
};

struct RunConfig {
    Scenario scenario;
    std::uint64_t seed = 90901;
    int workers = 20;
    int population = 100;
    int maximum_generations = 1000;
    int no_improvement_generations = 100;
    double crossover_rate = 0.08;
    double total_mutation_rate = 0.01;
    bool enable_convergence = false;
};

struct RunResult {
    std::string case_id;
    std::string method_semantic_id =
        "y09_ternary_variable_mutation_ga_declared_v1";
    std::string problem_semantic_id =
        "y09_multitype_mqi_fatigue_lcoe_declared_v1";
    std::string protocol_semantic_id =
        "y09_native_12case_single_run_declared_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t parallel_regions = 0;
    int population = 0;
    int generations = 0;
    std::uint64_t physical_fes = 0;
    std::string convergence_reason;
    MutationProbabilities final_mutation_probabilities;
    Evaluation best_evaluation;
    std::vector<int> best_layout;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    struct Impl;

    Problem();
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] int candidate_count() const noexcept;
    [[nodiscard]] double side_length_m() const noexcept;
    [[nodiscard]] int native_case_count() const noexcept;
    [[nodiscard]] const std::vector<Scenario>& native_scenarios() const noexcept;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<int>& layout,
        const Scenario& scenario
    ) const;
    [[nodiscard]] double ambient_speed_mps(double height_m) const noexcept;
    [[nodiscard]] double ambient_turbulence(double height_m) const noexcept;

private:
    std::unique_ptr<Impl> impl_;
    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config);
[[nodiscard]] MutationProbabilities mutation_probabilities(
    double average_five_count,
    double average_fifteen_count,
    double total_mutation_rate,
    Composition composition
);
[[nodiscard]] std::string to_string(Composition value);

}  // namespace core99::y09
