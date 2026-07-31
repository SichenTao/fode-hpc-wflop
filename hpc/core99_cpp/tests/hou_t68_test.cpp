/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T68 paper-role, LPC-anchor, APSO and HPC regression
Paper DOI: 10.1109/TSTE.2016.2614266
Public source, missing information, conflicts and completion:
include/core99/hou_t68.hpp
Claim boundary: tests the academic reconstruction, not author results.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/hou_t68.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using core99::t68::Problem;
    using core99::t68::Role;
    assert(core99::t68::paper_case_ids().size() == 5U);
    const Problem direction(Role::direction_only);
    const Problem first(Role::scenario_i_spacing);
    const Problem second(Role::scenario_ii_spacing_direction);
    const Problem third(Role::scenario_iii_pitch);
    const Problem fourth(Role::scenario_iv_codesign);
    assert(direction.dimensions() == 1);
    assert(first.dimensions() == 16);
    assert(second.dimensions() == 17);
    assert(third.dimensions() == 4800);
    assert(fourth.dimensions() == 4817);
    assert(direction.paper_repeats() == 10);
    assert(fourth.paper_repeats() == 20);
    double probability = 0.0;
    for (const auto& state : direction.wind_states()) {
        probability += state.probability;
    }
    assert(direction.wind_states().size() == 60U);
    assert(std::abs(probability - 1.0) < 1.0e-12);
    const auto anchor = direction.evaluate(direction.reference_decision());
    assert(anchor.feasible);
    assert(std::abs(anchor.gross_energy_gwh - 1972.9) < 1.0e-8);
    assert(std::abs(anchor.cable_loss_gwh - 34.24) < 1.0e-8);
    assert(std::abs(anchor.cable_cost_mdkk - 345.25) < 1.0e-8);
    assert(std::abs(anchor.lpc_dkk_per_mwh - 178.14) < 1.0e-8);

    core99::t68::RunConfig config;
    config.seed = 68019;
    config.workers = 1;
    config.population_override = 6;
    config.iteration_override = 2;
    config.unchanged_iterations = 2;
    const auto serial = core99::t68::run(direction, config);
    config.workers = 4;
    const auto parallel = core99::t68::run(direction, config);
    assert(serial.physical_fes == parallel.physical_fes);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.best_decision == parallel.best_decision);
    assert(serial.best_evaluation.lpc_dkk_per_mwh
        == parallel.best_evaluation.lpc_dkk_per_mwh);
    assert(parallel.observed_workers >= 2);
    std::cout << "T68 C++ reconstruction tests passed\n";
    return 0;
}
