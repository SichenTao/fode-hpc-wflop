/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T03 Kusiak-Song continuous circular WFLOP and ES
Paper/DOI: Design of Wind Farm Layout for Maximum Wind Energy Capture;
10.1016/j.renene.2009.08.019
Public source: no author implementation was located
Paper-preserved problem: fixed 2--6 turbines, 500 m circular farm, 8R
spacing, 24 direction bins, direction-conditioned Weibull distributions,
Kusiak wake equations, linear/rated turbine power, and expected power
Paper-preserved method: (20,120)-ES, log-normal strategy mutation, two-parent
arithmetic recombination, tournament size 4, archive size 50, 100 generations
Missing/conflicts: SPEA tie/density/clustering edge cases and random generator
are not executable in the paper
Reconstruction and resolution: deterministic strength-Pareto scoring, farthest-spread archive
truncation, counter-keyed random events, and ordered commits
Contract: shared/contracts/core99_t03_kusiak_cases.json
Method/problem semantic IDs: t03_kusiak_spea_es_declared_v1;
t03_kusiak_circular_expected_power_v1
Production backend: pure C++ persistent CPU team; independent layouts and
offspring mutations are parallel, Pareto/archive commits are ordered
Claim boundary: academic declared reproduction, not author-source or
author-exact numerical reproduction
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t03 {

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Evaluation {
    double expected_power_kw = 0.0;
    double inverse_power = 0.0;
    double constraint_violation = 0.0;
};

struct RunResult {
    std::string problem_id;
    std::vector<Point> best_layout;
    Evaluation best_evaluation;
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(std::string problem_id);

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int scenario() const noexcept;
    [[nodiscard]] Evaluation evaluate(const std::vector<Point>& layout) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_population(
        const std::vector<std::vector<Point>>& layouts,
        fode::PersistentExecutor& executor
    ) const;

private:
    struct WindBin {
        double direction_degrees = 0.0;
        double weibull_shape = 2.0;
        double weibull_scale = 0.0;
        double probability = 0.0;
    };

    std::string id_;
    int turbine_count_ = 0;
    int scenario_ = 0;
    std::vector<WindBin> wind_;
};

[[nodiscard]] RunResult run(
    const Problem& problem,
    std::uint64_t seed,
    std::uint64_t physical_fes,
    int workers
);

}  // namespace core99::t03
