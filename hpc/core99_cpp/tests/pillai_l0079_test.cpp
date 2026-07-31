/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0079 equation, role and schedule-identity regression
Paper/DOI: Pillai et al.; 10.1016/j.oceaneng.2017.04.049
Public asset and source provenance: target repository, thesis and pinned data
are declared in include/core99/pillai_l0079.hpp.
Fact boundary and completions: include/core99/pillai_l0079.hpp
Missing target arrays, paper/source conflicts and deterministic completion
decisions are declared in that header and controlling contract.
Method semantic IDs: l0079_adaptive_ga_three_encoding_declared_v1;
l0079_gbest_pso_three_encoding_declared_v1.
Controlling contract:
shared/contracts/core99_l0079_pillai_middelgrunden_2017.json
Claim boundary: tests the flexible reconstruction, not author numeric replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/pillai_l0079.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    core99::l0079::Problem journal(core99::l0079::CandidateProfile::journal_628);
    core99::l0079::Problem thesis(core99::l0079::CandidateProfile::thesis_658);
    assert(journal.turbine_count() == 20);
    assert(journal.wind_state_count() == 276);
    assert(journal.candidate_count() == 628);
    assert(thesis.candidate_count() == 658);
    assert(std::abs(journal.domain_area_km2() - 5.7) < 1.0e-12);
    assert(std::abs(journal.minimum_spacing_m() - 175.0) < 1.0e-12);
    const auto reference = journal.evaluate_layout(journal.as_built_layout());
    assert(reference.feasible);
    assert(std::abs(reference.net_aep_mwh_8766 - 95410.0 / 0.93) < 1.0e-7);
    assert(std::abs(reference.net_aep_mwh_8760
                    - reference.net_aep_mwh_8766 * 8760.0 / 8766.0) < 1.0e-8);
    assert(std::abs(reference.lifetime_cost_gbp - 91500000.0) < 1.0e-6);
    assert(std::abs(reference.lcoe_gbp_per_mwh - 86.63) < 1.0e-10);

    core99::l0079::RunConfig config;
    config.optimizer = core99::l0079::Optimizer::adaptive_ga;
    config.mode = core99::l0079::ConstraintMode::array;
    config.candidate_profile = core99::l0079::CandidateProfile::journal_628;
    config.population = 20;
    config.maximum_generations = 2;
    config.no_improvement_generations = 50;
    config.enable_convergence = false;
    config.seed = 79017;
    config.workers = 1;
    const auto serial = core99::l0079::run(journal, config);
    config.workers = 4;
    const auto parallel = core99::l0079::run(journal, config);
    assert(serial.generations == 2);
    assert(serial.physical_fes == 84);
    assert(serial.best_layout.size() == 20U);
    assert(serial.best_evaluation.feasible);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(parallel.observed_workers >= 2);

    config.optimizer = core99::l0079::Optimizer::gbest_pso;
    config.mode = core99::l0079::ConstraintMode::binary;
    config.maximum_generations = 1;
    config.workers = 4;
    const auto binary = core99::l0079::run(journal, config);
    assert(binary.physical_fes == 40);
    assert(binary.best_layout.size() == 20U);
    assert(binary.best_evaluation.minimum_spacing_m >= 175.0 - 1.0e-9);
    std::cout << "L0079 C++ reconstruction tests passed\n";
    return 0;
}
