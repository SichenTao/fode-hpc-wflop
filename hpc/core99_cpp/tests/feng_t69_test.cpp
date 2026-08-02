/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T69 equations, conflict and schedule-identity regression
Paper/DOI: Feng and Shen; 10.1016/j.enconman.2017.06.005
Fact boundary and completions: include/core99/feng_t69.hpp
Claim boundary: tests the reconstruction, not author numeric replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/feng_t69.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    assert(core99::t69::paper_weights().size() == 5U);
    core99::t69::Problem serial_problem(1, 69000, 64);
    core99::t69::Problem parallel_problem(4, 69000, 64);
    assert(serial_problem.turbine_count() == 80);
    assert(serial_problem.direction_count() == 360);
    assert(serial_problem.speed_count() == 23);
    const auto layout = serial_problem.paper_initial_layout();
    assert(layout.size() == 80U);
    const auto reference = serial_problem.evaluate(layout);
    assert(reference.feasible);
    assert(reference.mean_power_mw > 0.0);
    assert(reference.variability_of_power > 0.0);
    assert(reference.long_term_std_mw > 0.0);
    assert(std::abs(reference.short_robustness - 1.0) < 1.0e-12);
    assert(std::abs(
        reference.aep_mwh_paper_8770 - 8770.0 * reference.mean_power_mw
    ) < 1.0e-8);

    core99::t69::RunConfig config;
    config.study = core99::t69::Study::overall;
    config.conflict_profile =
        core99::t69::ConflictProfile::table3_compatible;
    config.weight = 0.95;
    config.seed = 69017;
    config.physical_fes = 4;
    config.workers = 1;
    const auto serial = core99::t69::run(serial_problem, config);
    config.workers = 4;
    const auto parallel = core99::t69::run(parallel_problem, config);
    assert(serial.physical_fes == 4);
    assert(serial.final_evaluation.feasible);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.final_layout.size() == parallel.final_layout.size());
    assert(serial.accepted_moves == parallel.accepted_moves);
    assert(parallel.observed_workers >= 2);
    assert(std::abs(
        serial.reference.table3_compatible_long_robustness
        - std::sqrt(
            serial.reference.long_term_mean_mw
            / serial.reference.long_term_std_mw)
    ) < 1.0e-12);
    std::cout << "T69 C++ reconstruction tests passed\n";
    return 0;
}
