/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T07 analytic and geometry invariants
Paper DOI: 10.1016/j.apenergy.2015.03.139.
Public source: no author code/data found.
Missing: exact coordinates, rotor quadrature and CVX files.
Reconstruction: test 8x10 rhombus, 5D feasibility and gradient fixture.
Claim boundary: invariant test, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/park_t07.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <stdexcept>

int main() {
    try {
        if (core99::t07::paper_case_ids().size() != 9) {
            throw std::runtime_error("T07 case count");
        }
        core99::t07::Problem problem("t07_single_0");
        if (problem.initial_layout().size() != core99::t07::turbine_count) {
            throw std::runtime_error("T07 initial layout count");
        }
        if (problem.maximum_constraint_violation(
                problem.initial_layout()) > 1.0e-9) {
            throw std::runtime_error("T07 initial feasibility");
        }
        fode::PersistentExecutor executor(2);
        const auto evaluation =
            problem.evaluate(problem.initial_layout(), true, executor);
        if (!(evaluation.efficiency > 0.0 && evaluation.efficiency <= 1.0)
            || evaluation.gradient.size() != core99::t07::variables) {
            throw std::runtime_error("T07 evaluator/gradient");
        }
        for (const double derivative : evaluation.gradient) {
            if (!std::isfinite(derivative)) {
                throw std::runtime_error("T07 finite gradient");
            }
        }
        struct InitialFixture {
            const char* case_id;
            double paper_efficiency;
            double tolerance;
        };
        constexpr InitialFixture fixtures[] = {
            {"t07_single_0", .828, .003},
            {"t07_single_41", .583, .003},
            {"t07_single_90", .432, .003},
            // The paper does not publish its wind-speed bin count/range.
            {"t07_expected_k033", .836, .025},
        };
        for (const auto& fixture : fixtures) {
            core99::t07::Problem fixture_problem(fixture.case_id);
            const auto fixture_evaluation = fixture_problem.evaluate(
                fixture_problem.initial_layout(), false, executor
            );
            if (std::abs(
                    fixture_evaluation.efficiency
                    - fixture.paper_efficiency
                ) > fixture.tolerance) {
                throw std::runtime_error(
                    std::string("T07 paper initial fixture ")
                    + fixture.case_id
                );
            }
        }
        auto plus_layout = problem.initial_layout();
        auto minus_layout = problem.initial_layout();
        constexpr double difference_step_m = 1.0e-3;
        plus_layout[0].x_m += difference_step_m;
        minus_layout[0].x_m -= difference_step_m;
        const double finite_difference = (
            problem.evaluate(plus_layout, false, executor).efficiency
            - problem.evaluate(minus_layout, false, executor).efficiency
        ) / (2.0 * difference_step_m);
        if (std::abs(finite_difference - evaluation.gradient[0]) > 2.0e-8) {
            throw std::runtime_error("T07 forward-AD finite difference");
        }
        std::cout << "t07_cpp_test_pass efficiency="
                  << evaluation.efficiency << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
