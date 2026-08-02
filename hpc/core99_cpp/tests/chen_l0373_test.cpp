/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0373 equations, paper roles and HPC identity tests
Paper DOI: 10.1016/j.renene.2021.10.032
Public assets, missing information, conflicts, corrections, reconstruction,
semantic IDs, backend, controlling contract and claim boundary:
include/core99/chen_l0373.hpp
Claim boundary: flexible academic reconstruction, not author target code,
private arrays, exact MATLAB/FLORISSE-M trajectory or numerical replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/chen_l0373.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

double no_wake_turbine_power_mw(
    const double yaw_degrees,
    const double axial_induction
) {
    constexpr double pi = 3.141592653589793238462643383279502884;
    constexpr double density = 1.29;
    constexpr double diameter_m = 126.0;
    constexpr double speed_mps = 9.0;
    constexpr double yaw_exponent = 1.88;
    const double yaw = yaw_degrees * pi / 180.0;
    const double cp = 4.0 * axial_induction
        * (1.0 - axial_induction) * (1.0 - axial_induction);
    const double area = 0.25 * pi * diameter_m * diameter_m;
    return 0.5 * density * area * cp
        * std::pow(std::cos(yaw), yaw_exponent)
        * speed_mps * speed_mps * speed_mps / 1.0e6;
}

}  // namespace

int main() {
    using namespace core99::l0373;
    const auto profiles = paper_profiles();
    assert(profiles.size() == 6U);

    const Problem directions36(ProfileId::turbines16_directions36);
    assert(directions36.turbine_count() == 16);
    assert(directions36.winds().size() == 36U);
    const double probability = std::accumulate(
        directions36.winds().begin(), directions36.winds().end(), 0.0,
        [](const double sum, const WindState& wind) {
            return sum + wind.probability;
        }
    );
    assert(std::abs(probability - 1.0) < 2.0e-15);
    assert(std::abs(directions36.minimum_spacing_m() - 504.0) < 1.0e-12);
    const Problem horns(ProfileId::turbines80_directions12);
    assert(std::abs(horns.minimum_spacing_m() - 504.0) < 1.0e-12);
    const auto horns_initial = horns.initial_layout();
    assert(std::abs(
        std::hypot(
            horns_initial[1].x_m - horns_initial[0].x_m,
            horns_initial[1].y_m - horns_initial[0].y_m
        ) - 7.0 * 126.0
    ) < 1.0e-10);

    const Problem illustration(ProfileId::illustrative_unrestricted);
    std::vector<Controls> schedule(1);
    schedule.front().yaw_degrees = {0.0, 10.0, -20.0};
    schedule.front().axial_induction = {1.0 / 3.0, 0.25, 0.2};
    const Evaluation varied = illustration.evaluate(
        illustration.initial_layout(), schedule
    );
    const double independent_no_wake =
        no_wake_turbine_power_mw(0.0, 1.0 / 3.0)
        + no_wake_turbine_power_mw(10.0, 0.25)
        + no_wake_turbine_power_mw(-20.0, 0.2);
    assert(std::abs(varied.no_wake_power_mw - independent_no_wake) < 1.0e-12);
    assert(varied.expected_power_mw > 0.0);
    assert(varied.expected_power_mw < varied.no_wake_power_mw);

    RunConfig config;
    config.seed = 37301;
    config.pso_trials = 1;
    config.pso_population = 8;
    config.pso_iterations = 1;
    config.control_passes = 1;
    config.dbhm_iterations = 1;
    config.workers = 1;
    const RunResult serial = run(directions36, config);
    config.workers = 4;
    const RunResult parallel = run(directions36, config);
    assert(serial.cases.size() == 5U);
    assert(serial.cases.front().role == "case1_initial_layout_greedy_control");
    assert(serial.cases.back().role == "case5_joint_layout_control_dbhm");
    for (const CaseResult& role : serial.cases) {
        assert(role.evaluation.feasible);
        assert(role.evaluation.aep_gwh > 0.0);
    }
    assert(serial.cases[1].evaluation.aep_gwh
        >= serial.cases[0].evaluation.aep_gwh);
    assert(serial.cases[3].evaluation.aep_gwh
        >= serial.cases[2].evaluation.aep_gwh);
    assert(serial.cases[4].evaluation.aep_gwh
        >= serial.cases[3].evaluation.aep_gwh);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.single_wind_state_evaluations
        == parallel.single_wind_state_evaluations);
    assert(serial.complete_layout_evaluations
        == parallel.complete_layout_evaluations);
    assert(parallel.observed_workers >= 2);
    std::cout << "L0373 C++ flexible-reproduction tests passed\n";
    return 0;
}
