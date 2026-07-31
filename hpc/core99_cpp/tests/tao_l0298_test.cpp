/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0298 equations, paper roles and HPC identity tests
Paper/DOI: 10.1109/TSG.2020.3022378.
Public assets, missing fields, conflicts, reconstruction and claim boundary:
include/core99/tao_l0298.hpp.
Semantic IDs and controlling contract are defined in that header and in
shared/contracts/core99_l0298_tao_2020.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/tao_l0298.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

int main() {
    using namespace core99::l0298;
    const auto profiles = paper_profiles();
    assert(profiles.size() == 9U);
    int roles = 0;
    for (const auto profile : profiles) {
        roles += Problem(profile).expected_role_count();
    }
    assert(roles == 29);

    Problem base(ProfileId::model_comparison);
    std::vector<int> cells(60);
    std::iota(cells.begin(), cells.end(), 0);
    std::vector<CableEdge> edges;
    std::uint64_t cable_work = 0;
    const Evaluation fixture = base.evaluate_cells(
        cells, "Model 1", 29801, 8, 1, &edges, &cable_work
    );
    assert(fixture.turbine_count == 60);
    assert(std::abs(fixture.installed_capacity_mw - 180.0) < 1.0e-12);
    assert(fixture.wind_daily_energy_mwh > 0.0);
    assert(std::isfinite(fixture.profit_rate_percent));
    assert(edges.size() == 60U);
    assert(cable_work == 16U);

    RunConfig config;
    config.seed = 29801;
    config.outer_population = 8;
    config.outer_iterations = 1;
    config.inner_population = 4;
    config.inner_iterations = 1;
    config.workers = 1;
    const RunResult serial = run(base, config);
    config.workers = 4;
    const RunResult parallel = run(base, config);
    assert(serial.roles.size() == 7U);
    assert(serial.complete_outer_evaluations == 55U);
    assert(serial.cable_particle_evaluations == 440U);
    assert(serial.hourly_wake_evaluations == 1320U);
    assert(serial.roles[3].evaluation.turbine_count == 70);
    for (const auto& role : serial.roles) {
        assert(role.active_cells.size()
            == static_cast<std::size_t>(role.evaluation.turbine_count));
        assert(role.cable_edges.size() == role.active_cells.size());
        assert(std::isfinite(role.evaluation.profit_rate_percent));
        assert(role.evaluation.capacity_factor_percent > 0.0);
        assert(role.evaluation.variability_percent >= 0.0);
    }
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.complete_outer_evaluations
        == parallel.complete_outer_evaluations);
    assert(serial.cable_particle_evaluations
        == parallel.cable_particle_evaluations);
    assert(parallel.observed_workers >= 2);
    std::cout << "L0298 C++ flexible-reproduction tests passed\n";
    return 0;
}
