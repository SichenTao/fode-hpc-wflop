/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y14 equations, cases, constraints and schedule tests
Paper DOI: 10.1109/TSTE.2026.3661110
Public asset, missing information, conflict, reconstruction, semantic IDs,
production backend, controlling contract and claim boundary:
include/core99/zhang_y14.hpp
Claim boundary: flexible academic reconstruction, not author numeric replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/zhang_y14.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>

int main() {
    const auto scenarios = core99::y14::paper_scenarios();
    assert(scenarios.size()==6U);
    const core99::y14::Problem n16(scenarios[0]);
    const core99::y14::Problem n48(scenarios[2]);
    assert(n16.receiver_count()==67);
    assert(n48.receiver_count()==91);
    assert(std::abs(n16.rotor_radius_m()-56.5)<1.0e-12);
    assert(std::abs(n16.minimum_spacing_m()-std::sqrt(8.0)*56.5)<1.0e-12);
    assert(n16.wind_probabilities().size()==16U);
    assert(std::abs(std::accumulate(
        n16.wind_probabilities().begin(),n16.wind_probabilities().end(),0.0
    )-1.0)<1.0e-14);
    const auto reference = n16.reference_layout();
    const auto evaluation = n16.evaluate(reference);
    assert(reference.size()==16U);
    assert(evaluation.feasible);
    assert(evaluation.negative_aep_gwh < -50.0);
    assert(evaluation.spl_db > 20.0 && evaluation.spl_db < 80.0);

    core99::y14::RunConfig config;
    config.seed=141409;
    config.workers=1;
    config.maximum_evaluation_slots=100;
    const auto serial = core99::y14::run(n16,config);
    config.workers=4;
    const auto parallel = core99::y14::run(n16,config);
    assert(serial.nominal_evaluation_slots==100U);
    assert(serial.generations==1);
    assert(serial.physical_fes>=50U && serial.physical_fes<=100U);
    assert(!serial.front.empty());
    assert(serial.scientific_hash==parallel.scientific_hash);
    assert(serial.physical_fes==parallel.physical_fes);
    assert(serial.front.size()==parallel.front.size());
    assert(parallel.observed_workers>=2);
    std::cout << "Y14 C++ flexible-reproduction tests passed\n";
    return 0;
}
