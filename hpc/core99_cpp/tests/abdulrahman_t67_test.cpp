/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T67 paper-role, catalog, physical, and HPC regression
Paper DOI: 10.1016/j.renene.2016.10.038
Public source: no target MATLAB source or complete commercial-turbine arrays.
Missing information and declared completion:
include/core99/abdulrahman_t67.hpp
Test scope: 162 paper roles, monotone 61-turbine completion with six exact
paper anchors, feasible TIL/SWF physics, physical FES accounting, and
one/all-worker scientific identity.
Claim boundary: tests the academic reconstruction, not author results.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/abdulrahman_t67.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <set>
#include <string>

namespace {

void check_roles_and_catalog() {
    const auto roles = core99::t67::paper_case_ids();
    assert(roles.size() == 162U);
    assert(std::set<std::string>(roles.begin(), roles.end()).size() == 162U);
    const auto& catalog = core99::t67::turbine_catalog();
    assert(catalog.size() == 61U);
    for (std::size_t index = 0; index < catalog.size(); ++index) {
        assert(catalog[index].code == static_cast<int>(index + 1));
        assert(catalog[index].diameter_m >= 66.0);
        assert(catalog[index].diameter_m <= 115.0);
        assert(catalog[index].rated_speed_mps >= 11.0);
        assert(catalog[index].rated_speed_mps <= 17.0);
        if (index > 0) {
            assert(
                catalog[index - 1].rated_power_mw
                <= catalog[index].rated_power_mw
            );
        }
    }
    assert(catalog[0].rated_power_mw == 1.5);
    assert(catalog[0].diameter_m == 77.0);
    assert(catalog[5].rated_power_mw == 1.5);
    assert(catalog[5].diameter_m == 82.0);
    assert(catalog[18].rated_power_mw == 1.8);
    assert(catalog[18].diameter_m == 100.0);
    assert(catalog[44].rated_power_mw == 2.5);
    assert(catalog[44].diameter_m == 115.0);
    assert(catalog[55].rated_power_mw == 3.0);
    assert(catalog[60].rated_power_mw == 3.075);
}

void check_reference_physics() {
    for (const auto layout : {
        core99::t67::LayoutKind::turbine_in_line,
        core99::t67::LayoutKind::array,
        core99::t67::LayoutKind::staggered,
    }) {
        const core99::t67::Problem problem(
            layout,
            4,
            10.0,
            core99::t67::Terrain::offshore,
            core99::t67::Objective::minimum_tciop
        );
        const auto evaluation =
            problem.evaluate(problem.reference_decision());
        assert(evaluation.feasible);
        assert(evaluation.total_power_mw > 0.0);
        assert(evaluation.rated_power_mw > 0.0);
        assert(evaluation.capacity_factor > 0.0);
        assert(evaluation.capacity_factor <= 1.0);
        assert(evaluation.total_cost_index > 0.0);
        assert(evaluation.total_cost_index_per_output_power > 0.0);
    }
}

void check_schedule_independence() {
    const core99::t67::Problem problem(
        core99::t67::LayoutKind::staggered,
        4,
        10.0,
        core99::t67::Terrain::offshore,
        core99::t67::Objective::minimum_tciop
    );
    core99::t67::RunConfig config;
    config.seed = 67017;
    config.workers = 1;
    config.population_size = 64;
    config.maximum_generations = 20;
    config.stall_generations = 100;
    const auto serial = core99::t67::run(problem, config);
    config.workers = 4;
    const auto parallel = core99::t67::run(problem, config);
    assert(serial.generations == 20);
    assert(parallel.generations == 20);
    assert(serial.physical_fes == 64U + 20U * 60U);
    assert(serial.physical_fes == parallel.physical_fes);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(
        std::abs(
            serial.best_evaluation.total_power_mw
            - parallel.best_evaluation.total_power_mw
        ) < 1.0e-12
    );
    assert(parallel.observed_workers >= 2);
}

}  // namespace

int main() {
    check_roles_and_catalog();
    check_reference_physics();
    check_schedule_independence();
    std::cout << "T67 C++ reconstruction tests passed\n";
    return 0;
}
