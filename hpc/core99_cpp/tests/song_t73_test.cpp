/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T73 equations, role coverage and HPC identity tests
Paper/DOI: Song et al.; 10.1016/j.cie.2018.04.051.
Public assets, missing fields, conflicts, reconstruction and claim boundary:
include/core99/song_t73.hpp.
Semantic IDs and controlling contract:
shared/contracts/core99_t73_song_2018.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/song_t73.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace core99::t73;
    Problem problem;
    assert(problem.candidate_count() == 342);
    assert(problem.candidate_points().size() == 342U);
    assert(paper_inspection_intervals()
        == std::vector<int>({200, 250, 332, 350, 400}));

    std::vector<Point> fixture(
        problem.candidate_points().begin(), problem.candidate_points().begin() + 20
    );
    const auto layout = problem.evaluate_layout(fixture);
    assert(layout.feasible);
    assert(layout.turbine_count == 20);
    assert(layout.minimum_spacing_m >= 100.0);
    assert(layout.annual_energy_mwh > 0.0);
    assert(std::isfinite(layout.pre_maintenance_profit_usd));

    std::vector<int> clusters(20, 0);
    const auto maintenance = problem.evaluate_maintenance(
        fixture, clusters, 332, 5, 73001, 2
    );
    assert(maintenance.inspection_interval_days == 332);
    assert(maintenance.mean_cost_usd > 0.0);
    assert(maintenance.inspection_cost_usd > 0.0);

    RunConfig config;
    config.seed = 73001;
    config.ga_population = 8;
    config.ga_generations = 1;
    config.pattern_iterations = 1;
    config.maintenance_replications = 5;
    config.workers = 1;
    const auto serial = run(problem, config);
    config.workers = 4;
    const auto parallel = run(problem, config);
    assert(serial.roles.size() == 12U);
    assert(serial.roles[0].role == "table3_discrete_premaintenance");
    assert(serial.roles[1].role == "table3_continuous_premaintenance");
    assert(serial.roles.back().role == "table5_continuous_V400");
    assert(serial.layout_evaluations == 36U);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.discrete_layout.size() == parallel.discrete_layout.size());
    assert(serial.continuous_layout.size() == parallel.continuous_layout.size());
    assert(serial.component_life_events == parallel.component_life_events);
    assert(parallel.observed_workers >= 2);
    std::cout << "T73 C++ flexible-reproduction tests passed\n";
    return 0;
}
