/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T78 paper-role, anchor, PSO and HPC regression
Paper DOI: 10.1016/j.apenergy.2020.114896
Public source, missing information, conflicts and declared completion:
include/core99/wu_t78.hpp
Claim boundary: tests the academic reconstruction, not author results.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/wu_t78.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using core99::t78::Problem;
    using core99::t78::Role;
    assert(core99::t78::paper_case_ids().size() == 2U);
    const Problem strict(Role::strict_noise_control);
    const Problem economic(Role::economic_compensation);
    assert(strict.dimensions() == 160);
    assert(strict.population_size() == 100);
    assert(strict.maximum_iterations() == 200);
    assert(strict.paper_repeats() == 10);
    assert(strict.wind_states().size() == 60U);
    double probability = 0.0;
    for (const auto& state : strict.wind_states()) {
        probability += state.probability;
    }
    assert(std::abs(probability - 1.0) < 1.0e-12);
    const auto strict_anchor = strict.evaluate(strict.reference_decision());
    const auto economic_anchor = economic.evaluate(economic.reference_decision());
    assert(std::abs(strict_anchor.annual_energy_gwh - 4015.17) < 1.0e-8);
    assert(std::abs(strict_anchor.maximum_l10_dba - 48.60) < 1.0e-10);
    assert(strict_anchor.minimum_spacing_m >= 713.2 - 5.0);
    assert(!strict_anchor.feasible);
    assert(economic_anchor.feasible);
    assert(std::abs(strict_anchor.hard_noise_violation_dba - 3.60) < 1.0e-10);
    assert(economic_anchor.hard_noise_violation_dba == 0.0);
    assert(std::abs(economic_anchor.noise_penalty_gwh - 0.036) < 1.0e-10);

    core99::t78::RunConfig config;
    config.seed = 78019;
    config.workers = 1;
    config.population_override = 6;
    config.iteration_override = 2;
    const auto serial = core99::t78::run(economic, config);
    config.workers = 4;
    const auto parallel = core99::t78::run(economic, config);
    assert(serial.physical_fes == 18U);
    assert(serial.physical_fes == parallel.physical_fes);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.best_decision == parallel.best_decision);
    assert(serial.best_evaluation.objective_gwh
        == parallel.best_evaluation.objective_gwh);
    assert(parallel.observed_workers >= 2);
    std::cout << "T78 C++ reconstruction tests passed\n";
    return 0;
}
