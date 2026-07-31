/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T77 evaluator, GRNN lifecycle, and deterministic HPC
invariants
Paper title: A Data-Driven Evolutionary Algorithm for Wind Farm Layout
Optimization
Paper DOI: 10.1016/j.energy.2020.118310
Public source: no paper-linked author code or data archive found.
Missing/conflicts/reconstruction: include/core99/long_t77.hpp.
Claim boundary: equation and lifecycle invariant test, not numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/long_t77.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    try {
        if (core99::t77::paper_case_ids().size() != 18) {
            throw std::runtime_error("T77 paper case count");
        }
        core99::t77::Problem problem(1, 15);
        if (
            problem.farm_side_m() != 2000.0
            || problem.semantic_id() != "t77_long_ws1_continuous_v1"
        ) {
            throw std::runtime_error("T77 problem identity");
        }
        std::vector<core99::t77::Point> layout;
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 5; ++column) {
                layout.push_back({
                    100.0 + 350.0 * column,
                    100.0 + 350.0 * row,
                });
            }
        }
        if (!problem.feasible(layout)) {
            throw std::runtime_error("T77 fixture feasibility");
        }
        const auto evaluation = problem.evaluate(layout);
        if (
            !(evaluation.expected_power_kw > 0.0)
            || evaluation.constraint_violation_m != 0.0
        ) {
            throw std::runtime_error("T77 evaluator");
        }
        const auto first_features = problem.grnn_features(layout);
        std::reverse(layout.begin(), layout.end());
        const auto second_features = problem.grnn_features(layout);
        if (first_features != second_features) {
            throw std::runtime_error("T77 permutation-invariant GRNN input");
        }
        core99::t77::RunConfig config;
        config.generations = 4;
        config.exact_stage_generations = 2;
        config.training_capacity = 80;
        config.workers = 1;
        const auto serial = core99::t77::run(problem, 771, config);
        config.workers = 4;
        const auto parallel = core99::t77::run(problem, 771, config);
        if (
            serial.scientific_hash != parallel.scientific_hash
            || serial.physical_exact_fes != 160
            || serial.candidate_proposals != 160
            || serial.surrogate_inferences != 80
            || serial.best_history_kw.size() != 4
            || serial.best_evaluation.constraint_violation_m > 1.0e-9
            || serial.best_evaluation.expected_power_kw
                < serial.initial_best_power_kw
        ) {
            throw std::runtime_error("T77 bounded lifecycle/replay");
        }
        std::cout << "t77_cpp_test_pass power_kw="
                  << evaluation.expected_power_kw << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
