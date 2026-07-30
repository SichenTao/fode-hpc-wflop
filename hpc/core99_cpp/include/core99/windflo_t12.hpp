/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T12 WindFLO 2015 problem and four paper-method interface
Paper/DOI: Evolutionary Computation for Wind Farm Layout Optimization;
10.1016/j.renene.2018.03.052
Public source: https://github.com/d9w/WindFLO at revision
9e85a67bb2ca019768ea51dd0b634a46c8406ba2, MIT license
Provided assets: five competition XML scenarios, C++ 3s-MDE and Goldman
entries, Java SSHH entry, a MATLAB archive labelled CMA-ES, and WindFLO
evaluators in C++, Java, MATLAB, and Python
Missing/conflicts: the archive labelled CMA-ES contains a different grid
fill/refinement routine and no identifiable covariance-matrix adaptation;
source RNGs and language runtimes differ; the competition fixes 2000 physical
WindFLO evaluations per scenario but some entries cache or pre-screen layouts
Reconstruction and resolution: the common evaluator follows the released C++
equations and XML
values; 3s-MDE, SSHH, and Goldman preserve the paper plus released source
semantics; CMA-ES uses the paper's five-variable geometric decoder and
standard active-style rank-mu covariance adaptation, with the archive conflict
recorded rather than silently relabelled; the 3s-MDE lattice surrogate
analytically aggregates its translation-invariant pair terms instead of
materializing a full layout at every one of millions of surrogate-only local
steps, while all physical evaluations use the exact decoded layout; physical
FES counts only calls to the common WindFLO evaluator; when the analytically
aggregated surrogate's absolute calibration admits fewer than half of a DE
generation under the paper's 1.20 filter, the best surrogate-ranked trials
deterministically fill half the batch so the declared physical budget advances
Method/problem semantic IDs: t12_four_competition_methods_v1;
t12_windflo_2015_five_scenarios_v1
Controlling contract: shared/contracts/core99_t12_windflo_2015.json
Production backend: pure C++ CPU-HPC with persistent all-core candidate-batch
and wind-direction execution; deterministic paper-order state commits
Claim boundary: academic declared reproduction, not author-exact bit-stream or
author-runtime reproduction
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t12 {

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Evaluation {
    double energy_cost = 0.0;
    double wake_free_ratio = 0.0;
    double energy_output_kw = 0.0;
    double constraint_violation_m = 0.0;
    std::vector<double> turbine_fitness;
};

struct RunResult {
    std::string algorithm_id;
    std::string problem_id;
    std::vector<Point> best_layout;
    Evaluation best_evaluation;
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    std::uint64_t physical_fes_limit = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(int scenario_index);

    [[nodiscard]] int scenario_index() const noexcept;
    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] double width() const noexcept;
    [[nodiscard]] double height() const noexcept;
    [[nodiscard]] double radius() const noexcept;
    [[nodiscard]] double minimum_spacing() const noexcept;
    [[nodiscard]] int nominal_turbines() const noexcept;
    [[nodiscard]] std::vector<double> obstacle_coordinates() const;
    [[nodiscard]] bool valid_point(const Point& point) const noexcept;
    [[nodiscard]] double constraint_violation(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_candidates(
        const std::vector<std::vector<Point>>& layouts,
        fode::PersistentExecutor& executor
    ) const;

private:
    int scenario_index_ = 0;
    std::string id_;
};

[[nodiscard]] std::vector<std::string> algorithm_ids();

[[nodiscard]] RunResult run(
    const Problem& problem,
    const std::string& algorithm_id,
    std::uint64_t seed,
    std::uint64_t physical_fes_limit,
    int workers
);

}  // namespace core99::t12
