/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T12 semantic and deterministic-parallel C++ tests
Paper DOI: 10.1016/j.renene.2018.03.052
Public source: https://github.com/d9w/WindFLO revision
9e85a67bb2ca019768ea51dd0b634a46c8406ba2, MIT license
Missing/conflicts and reconstruction: include/core99/windflo_t12.hpp
Method/problem semantic IDs: t12_four_competition_methods_v1;
t12_windflo_2015_five_scenarios_v1
Controlling contract: shared/contracts/core99_t12_windflo_2015.json
Claim boundary: structural and deterministic tests only
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/windflo_t12.hpp"

#include "fode/executor.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

int main() {
    const auto algorithms = core99::t12::algorithm_ids();
    if (algorithms.size() != 4) {
        std::cerr << "T12 algorithm registry mismatch\n";
        return EXIT_FAILURE;
    }
    for (int scenario = 0; scenario < 5; ++scenario) {
        const core99::t12::Problem problem(scenario);
        if (
            problem.width() <= 0.0
            || problem.height() <= 0.0
            || problem.minimum_spacing() != 308.0
            || problem.id() != "t12_windflo_s" + std::to_string(scenario + 1)
        ) {
            std::cerr << "T12 problem registry mismatch\n";
            return EXIT_FAILURE;
        }
        const std::vector<core99::t12::Point> layout{
            {0.0, 0.0},
            {problem.width(), problem.height()}
        };
        fode::PersistentExecutor serial(1);
        fode::PersistentExecutor parallel(4);
        const auto first = problem.evaluate(layout, serial);
        const auto second = problem.evaluate(layout, parallel);
        if (
            !std::isfinite(first.energy_cost)
            || first.constraint_violation_m != 0.0
            || std::abs(first.energy_cost - second.energy_cost) > 1.0e-14
            || std::abs(
                first.energy_output_kw - second.energy_output_kw
            ) > 1.0e-10
        ) {
            std::cerr << "T12 evaluator deterministic mismatch\n";
            return EXIT_FAILURE;
        }
    }
    const core99::t12::Problem smoke_problem(0);
    for (const auto& algorithm : algorithms) {
        const auto serial = core99::t12::run(
            smoke_problem, algorithm, 17, 16, 1
        );
        const auto parallel = core99::t12::run(
            smoke_problem, algorithm, 17, 16, 4
        );
        if (
            serial.physical_fes > 16
            || parallel.physical_fes > 16
            || serial.scientific_hash != parallel.scientific_hash
        ) {
            std::cerr << "T12 optimizer deterministic mismatch: "
                      << algorithm << '\n';
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
