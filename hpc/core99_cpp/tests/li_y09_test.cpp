/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y09 equations, native-case and schedule-identity tests
Paper/DOI: Li et al.; 10.1016/j.renene.2025.124386
Public source/data, missing information, paper/patent conflicts, completion,
semantic IDs, production backend, controlling contract and claim boundary:
include/core99/li_y09.hpp
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/li_y09.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    const core99::y09::Problem problem;
    assert(problem.candidate_count() == 100);
    assert(problem.native_case_count() == 12);
    assert(std::abs(problem.side_length_m() - 5000.0) < 1.0e-12);
    assert(std::abs(problem.ambient_speed_mps(150.0) - 12.0) < 1.0e-12);
    assert(problem.ambient_speed_mps(90.0) < 12.0);
    assert(problem.ambient_turbulence(90.0) > 0.0);

    const auto probabilities = core99::y09::mutation_probabilities(
        30.0, 20.0, 0.01, core99::y09::Composition::multi_type
    );
    const double weighted = (
        50.0 * probabilities.zero
        + 30.0 * probabilities.five
        + 20.0 * probabilities.fifteen
    ) / 100.0;
    assert(std::abs(weighted - 0.01) < 1.0e-15);

    std::vector<int> single(100, 0);
    single[0] = 1;
    const auto evaluation = problem.evaluate(single, problem.native_scenarios()[0]);
    assert(evaluation.feasible);
    assert(evaluation.five_mw_turbines == 1);
    assert(evaluation.fifteen_mw_turbines == 0);
    assert(evaluation.total_power_mw > 0.0 && evaluation.total_power_mw <= 5.0);
    assert(evaluation.lcoe_units_per_mw > 0.0);

    core99::y09::RunConfig config;
    config.scenario = problem.native_scenarios()[1];
    config.seed = 90917;
    config.population = 20;
    config.maximum_generations = 2;
    config.no_improvement_generations = 100;
    config.enable_convergence = false;
    config.workers = 1;
    const auto serial = core99::y09::run(problem, config);
    config.workers = 4;
    const auto parallel = core99::y09::run(problem, config);
    assert(serial.generations == 2);
    assert(serial.physical_fes == 60);
    assert(serial.best_evaluation.feasible);
    assert(serial.best_layout.size() == 100U);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.best_layout == parallel.best_layout);
    assert(parallel.observed_workers >= 2);
    std::cout << "Y09 C++ flexible-reproduction tests passed\n";
    return 0;
}
