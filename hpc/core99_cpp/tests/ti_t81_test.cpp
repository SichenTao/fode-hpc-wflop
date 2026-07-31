/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T81 case, wave/AEP, constraint and HPC regression
Paper DOI: 10.1016/j.apenergy.2021.117947
Public source: no target code or native wind/bathymetry/wave arrays.
Missing information and declared completion: include/core99/ti_t81.hpp
Claim boundary: tests the academic reconstruction, not author results.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/ti_t81.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    assert(core99::t81::paper_case_ids().size() == 2U);
    const core99::t81::Problem mild(core99::t81::CaseRole::mild_slope);
    const core99::t81::Problem complex(
        core99::t81::CaseRole::complex_terrain
    );
    assert(mild.turbine_count() == 16);
    assert(complex.turbine_count() == 20);
    assert(mild.wind_states().size() == 24U);
    assert(complex.wind_states().size() == 24U);
    assert(complex.boundary_polygon().size() == 6U);
    double probability = 0.0;
    for (const auto& state : complex.wind_states()) {
        probability += state.probability;
    }
    assert(std::abs(probability - 1.0) < 1.0e-12);
    const double shallow = mild.wave_load_at({0.0, 0.0});
    const double breaking = mild.wave_load_at({650.0, 0.0});
    const double nonbreaking_minimum = mild.wave_load_at({1680.0, 0.0});
    assert(breaking > shallow);
    assert(breaking > nonbreaking_minimum);
    const auto mild_reference = mild.evaluate(mild.reference_layout());
    const auto complex_reference = complex.evaluate(complex.reference_layout());
    assert(mild_reference.feasible);
    assert(complex_reference.feasible);
    assert(mild_reference.aep_gwh > 0.0);
    assert(complex_reference.aep_gwh > 0.0);

    core99::t81::RunConfig config;
    config.seed = 81019;
    config.workers = 1;
    config.multistarts = 2;
    config.maximum_evaluations_per_start = 4;
    config.alpha0_values = {0.98};
    const auto serial = core99::t81::run(mild, config);
    config.workers = 2;
    const auto parallel = core99::t81::run(mild, config);
    assert(serial.stages.size() == 2U);
    assert(serial.physical_fes == parallel.physical_fes);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.stages[0].best_evaluation.aep_gwh
        == parallel.stages[0].best_evaluation.aep_gwh);
    assert(parallel.observed_workers >= 2);
    std::cout << "T81 C++ reconstruction tests passed\n";
    return 0;
}
