/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0368 equations, cases, corrections and HPC tests
Paper DOI: 10.1016/j.enconman.2021.114610
Public asset, missing information, conflicts, corrections, reconstruction,
semantic IDs, backend, controlling contract and claim boundary:
include/core99/liu_l0368.hpp
Claim boundary: flexible academic reconstruction, not author target code,
private Nanao arrays, exact MATLAB defaults/random trajectory or replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/liu_l0368.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

int main() {
    const auto scenarios = core99::l0368::paper_scenarios();
    assert(scenarios.size() == 20U);
    assert(scenarios.front().case_id == "L0368_S1W1");
    assert(scenarios.back().case_id == "L0368_S5W4");
    assert(scenarios.front().paper_turbine_anchor == 18);
    assert(scenarios.back().paper_turbine_anchor == 19);

    const core99::l0368::Problem w1(scenarios.front());
    assert(w1.wind_states().size() == 1U);
    assert(std::abs(w1.wind_states().front().probability - 1.0) < 1.0e-12);
    const core99::l0368::Problem w4(scenarios.back());
    double probability = 0.0;
    for (const auto& state : w4.wind_states()) probability += state.probability;
    assert(w4.wind_states().size() == 180U);
    assert(std::abs(probability - 1.0) < 1.0e-12);

    auto slope_scenario = scenarios[1];
    const core99::l0368::Problem slope(slope_scenario);
    assert(std::abs(slope.water_depth_m({0.0, 1000.0}) - 16.60) < 1.0e-12);
    assert(std::abs(slope.water_depth_m({2000.0, 1000.0}) - 40.27) < 1.0e-12);

    const std::vector<core99::l0368::Point> diagonal{{0.0, 0.0}, {400.0, 400.0}};
    const auto corrected = w1.evaluate(diagonal);
    assert(corrected.feasible);
    assert(corrected.minimum_distance_m > 500.0);
    auto literal_scenario = scenarios.front();
    literal_scenario.spacing = core99::l0368::SpacingKind::paper_linf_sensitivity;
    const core99::l0368::Problem literal(literal_scenario);
    assert(!literal.evaluate(diagonal).feasible);

    core99::l0368::RunConfig config;
    config.population = 20;
    config.generations = 2;
    config.elite_count = 2;
    config.workers = 1;
    const auto serial = core99::l0368::run(w1, config);
    config.workers = 4;
    const auto parallel = core99::l0368::run(w1, config);
    assert(serial.best_evaluation.feasible);
    assert(serial.best_evaluation.turbine_count >= 1);
    assert(serial.best_evaluation.turbine_count <= 25);
    assert(serial.best_evaluation.minimum_distance_m >= 500.0 - 1.0e-7
        || serial.best_evaluation.turbine_count == 1);
    assert(serial.physical_fes == 60U);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.best_layout.size() == parallel.best_layout.size());
    assert(parallel.observed_workers >= 2);
    std::cout << "L0368 C++ flexible-reproduction tests passed\n";
    return 0;
}
