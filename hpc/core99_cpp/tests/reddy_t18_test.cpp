/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T18 equations, native-role and CPU-HPC identity tests
Paper/DOI: Reddy 2020; 10.1016/j.apenergy.2020.115090.
Fact boundary and controlling contract: include/core99/reddy_t18.hpp and
shared/contracts/core99_t18_reddy_2020.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/reddy_t18.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace core99::t18;
    Problem problem;
    const auto reference = problem.reference_layout();
    assert(reference.size() == 25U);
    const auto reference_result = problem.evaluate(
        reference, WakeModel::frandsen,
        TerrainProfile::paper_local_rbf,
        DiskSampling::paper_area_correct, 4
    );
    assert(reference_result.feasible);
    assert(reference_result.annual_energy_mwh > 0.0);
    assert(reference_result.normalized_aep > 0.0);
    assert(std::isfinite(reference_result.coe_usd_kwh));
    const auto validation = problem.validate_wind_tunnel(
        DiskSampling::paper_area_correct, 32
    );
    assert(validation.size() == 48U);
    for (const auto& item : validation) {
        assert(std::isfinite(item.predicted_velocity_mps));
        assert(std::isfinite(item.relative_error_percent));
    }
    const auto source_validation = problem.validate_wind_tunnel(
        DiskSampling::source_uniform_radius, 1000
    );
    assert(source_validation.size() == 48U);
    // Published Tables 2-3 numerical anchors spanning top-hat, Gaussian,
    // unstable Larsen and source-DWM branches.
    assert(std::abs(source_validation[0].predicted_velocity_mps - 6.34) < 0.02);
    assert(std::abs(source_validation[7].predicted_velocity_mps - 6.29) < 0.02);
    assert(std::abs(source_validation[9].predicted_velocity_mps - 5.76) < 0.02);
    assert(std::abs(source_validation[26].predicted_velocity_mps - 12.67) < 0.02);
    assert(std::abs(source_validation[34].predicted_velocity_mps - 5.25) < 0.02);
    assert(std::abs(source_validation[47].predicted_velocity_mps - 6.23) < 0.02);

    RunConfig config;
    config.seed = 18001;
    config.workers = 1;
    config.population = 4;
    config.generations = 1;
    config.stagnation_generations = 1;
    config.disk_quadrature_points = 4;
    const auto serial = run(problem, config);
    config.workers = 4;
    const auto parallel = run(problem, config);
    assert(serial.validation.size() == 48U);
    assert(serial.roles.size() == 6U);
    assert(serial.roles[0].role == "table4_frandsen_reference");
    assert(serial.roles[1].role == "table4_frandsen_case1_layout");
    assert(serial.roles[2].role == "table4_frandsen_case2_layout_turbine");
    assert(serial.roles[3].role == "table4_bp_reference");
    assert(serial.roles[5].role == "table4_bp_case2_layout_turbine");
    assert(serial.objective_evaluations == 34U);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(parallel.observed_workers >= 2);
    std::cout << "T18 C++ flexible-reproduction tests passed\n";
    return 0;
}
