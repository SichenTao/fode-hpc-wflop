/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T76 case, turbine, physics, FES, and HPC regression
Paper DOI: 10.1016/j.energy.2018.11.073
Public source: no target MATLAB source or native arrays located.
Missing information and declared completion: include/core99/sun_t76.hpp
Claim boundary: tests the academic reconstruction, not author results.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/sun_t76.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <set>
#include <string>

int main() {
    const auto roles = core99::t76::paper_case_ids();
    assert(roles.size() == 6U);
    assert(std::set<std::string>(roles.begin(), roles.end()).size() == 6U);
    const auto& catalog = core99::t76::turbine_catalog();
    assert(catalog.size() == 5U);
    for (const auto& turbine : catalog) {
        assert(
            std::abs(
                core99::t76::completed_power_kw(
                    turbine, turbine.anchor_speed_mps
                ) - turbine.anchor_power_kw
            ) < 1.0e-12
        );
        assert(
            core99::t76::completed_power_kw(
                turbine, turbine.rated_speed_mps
            ) == turbine.rated_power_kw
        );
    }
    const core99::t76::Problem aligned_omni(
        core99::t76::CaseRole::case1_omnidirectional_aligned
    );
    const core99::t76::Problem aligned_directional(
        core99::t76::CaseRole::case1_directional_aligned
    );
    const auto omni = aligned_omni.evaluate(aligned_omni.aligned_layout());
    const auto directional = aligned_directional.evaluate(
        aligned_directional.aligned_layout()
    );
    assert(omni.feasible && directional.feasible);
    assert(std::abs(
        omni.theoretical_no_wake_power_mw - 48.0 * 0.837
    ) < 1.0e-12);
    assert(std::abs(
        directional.theoretical_no_wake_power_mw - 48.0 * 0.837
    ) < 1.0e-12);
    assert(directional.expected_power_mw > omni.expected_power_mw);

    const core99::t76::Problem case4(
        core99::t76::CaseRole::case4_sha_chau_multitype_mpga
    );
    double probability = 0.0;
    for (const auto& state : case4.wind_states()) {
        probability += state.probability;
    }
    assert(case4.wind_states().size() == 36U * 27U);
    assert(std::abs(probability - 1.0) < 1.0e-12);

    const core99::t76::Problem optimized(
        core99::t76::CaseRole::case2_directional_mpga
    );
    core99::t76::RunConfig config;
    config.seed = 76019;
    config.workers = 1;
    config.demes = 2;
    config.individuals_per_deme = 4;
    config.unchanged_generations = 10;
    config.maximum_generations = 5;
    config.migration_period = 2;
    const auto serial = core99::t76::run(optimized, config);
    config.workers = 4;
    const auto parallel = core99::t76::run(optimized, config);
    assert(serial.generations == 5);
    assert(serial.physical_fes == 8U + 5U * 6U);
    assert(serial.physical_fes == parallel.physical_fes);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(
        serial.best_evaluation.expected_power_mw
        == parallel.best_evaluation.expected_power_mw
    );
    assert(parallel.observed_workers >= 2);
    std::cout << "T76 C++ reconstruction tests passed\n";
    return 0;
}
