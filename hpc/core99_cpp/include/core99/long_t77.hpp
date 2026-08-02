/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T77 ADE-GRNN and paper-native continuous WFLOP
Paper title: A Data-Driven Evolutionary Algorithm for Wind Farm Layout
Optimization
Paper DOI: 10.1016/j.energy.2020.118310
Public source: no paper-linked author code, data archive, or supplement found.
Paper-provided assets: Eqs. (1)-(21), ADE/best/1, layout sorting before GRNN,
FIFO training update, sigma=0.01, 125-generation exact stage, half-population
surrogate filter, population 40, 3750 generations, 5000 GRNN samples, five
runs, two 24-bin wind scenarios, nine turbine counts, farm sizes, GE1.5-77
parameters, and 150000 candidate proposals.
Missing/conflicts: raw Iowa wind data, source, seeds, random lifecycle,
successful-factor FIFO capacity, constraint-retry cap, wind-speed quadrature,
coordinate normalization, and exact FES accounting were not published. Raw
metre coordinates with sigma=0.01 make Eq. (19) numerically degenerate.
Table 4 twice prints an invalid [330,315) interval. Table 1 gives k=0.01 while
Eq. (2) derives k from an unreported roughness.
Reconstruction: use the paper tables directly; correct the two interval labels
to [300,315); use k=0.01; use 0.5 m/s midpoint bins from 3.5 to 14 m/s; use
farm-side normalized sorted coordinates and stable shifted GRNN weights; use
a 40-event FIFO for successful parameters; deterministic counter-keyed random
events; shrink infeasible mutation displacement at most 40 times, then retain
the parent. Count 150000 candidate proposals and every exact paper-equation
evaluation separately (77540 including the initial population).
Method semantic ID: t77_ade_grnn_paper_first_declared_v1
Problem semantic IDs: t77_long_ws1_continuous_v1;
t77_long_ws2_continuous_v1
Protocol semantic ID: t77_18_cases_5_runs_pop40_gen3750_v1
Production backend: pure C++ CPU. One persistent full-core team parallelizes
population generation, exact evaluations, GRNN inference, preprocessing, and
fixed-index reductions. GRNN samples are contiguous and coordinate-normalized;
generation selection, FIFO replacement, and adaptive-mean commits stay
ordered to preserve the paper lifecycle.
Claim boundary: academic paper-first reconstruction, not author code, raw
Iowa-data replay, identical random stream, or exact numerical replay.
Contract: shared/contracts/core99_t77_long_ade_grnn_2020.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t77 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct Evaluation {
    double expected_power_kw = 0.0;
    double constraint_violation_m = 0.0;
};

struct RunConfig {
    int population = 40;
    int generations = 3750;
    int exact_stage_generations = 125;
    int training_capacity = 5000;
    int success_history_capacity = 40;
    int workers = 20;
};

struct RunResult {
    std::string problem_semantic_id;
    std::string method_semantic_id;
    std::string protocol_semantic_id;
    int scenario = 0;
    int turbine_count = 0;
    double farm_side_m = 0.0;
    std::uint64_t seed = 0;
    int generations = 0;
    std::uint64_t candidate_proposals = 0;
    std::uint64_t physical_exact_fes = 0;
    std::uint64_t surrogate_inferences = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double initial_best_power_kw = 0.0;
    Evaluation best_evaluation;
    std::vector<Point> best_layout;
    std::vector<double> best_history_kw;
    double final_mean_f1 = 0.0;
    double final_mean_f2 = 0.0;
    double final_mean_moved_turbines = 0.0;
    double exact_evaluator_seconds = 0.0;
    double surrogate_seconds = 0.0;
    double operator_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    Problem(int scenario, int turbine_count);

    [[nodiscard]] int scenario() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] double farm_side_m() const noexcept;
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] bool feasible(const std::vector<Point>& layout) const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] std::vector<double> grnn_features(
        const std::vector<Point>& layout
    ) const;

private:
    struct WindBin {
        double direction_degrees = 0.0;
        double shape = 2.0;
        double scale = 0.0;
        double probability = 0.0;
    };

    int scenario_ = 0;
    int turbine_count_ = 0;
    double farm_side_m_ = 0.0;
    std::string semantic_id_;
    std::vector<WindBin> wind_;
};

[[nodiscard]] RunResult run(
    const Problem& problem,
    std::uint64_t seed,
    const RunConfig& config = {}
);
[[nodiscard]] std::vector<std::string> paper_case_ids();

}  // namespace core99::t77
