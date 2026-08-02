/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T72 structural, numerical, repair, and HPC regression tests
Paper/DOI: Constrained Multi-Objective Wind Farm Layout Optimization:
Novel Constraint Handling Approach Based on Constraint Programming;
10.1016/j.renene.2018.03.053
Public source: no author source or native maps were located
Missing/conflicts/completion:
hpc/core99_cpp/include/core99/sorkhabi_t72.hpp
Reconstruction: tests all nine paper problem combinations, equation stability,
discrete repair behavior, population-parallel evaluation, exact physical FES,
and schedule-independent one-worker versus all-worker scientific outputs
Method/problem semantic IDs: t72_chcp_nsga2_declared_reconstruction_v1;
t72_energy_noise_voronoi9_declared_reconstruction_v1
Controlling contract: shared/contracts/core99_t72_sorkhabi_2018.json
HPC design: persistent workers are exercised both by direct population
evaluation and the complete NSGA-II trajectory
Claim boundary: regression of the declared academic reconstruction
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/sorkhabi_t72.hpp"

#include "fode/executor.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

namespace {

using core99::t72::Point;
using core99::t72::Problem;

double distance_squared(const Point& left, const Point& right) {
    const double dx = left.x_m - right.x_m;
    const double dy = left.y_m - right.y_m;
    return dx * dx + dy * dy;
}

std::vector<Point> feasible_layout(const Problem& problem) {
    std::vector<Point> result;
    constexpr double spacing_squared = 385.0 * 385.0;
    for (int y = 0; y <= 30; ++y) {
        for (int x = 0; x <= 30; ++x) {
            const Point candidate{
                100.0 * static_cast<double>(x),
                100.0 * static_cast<double>(y),
            };
            if (problem.regulatory_forbidden(candidate)) {
                continue;
            }
            const bool separated = std::all_of(
                result.begin(),
                result.end(),
                [&](const Point& existing) {
                    return distance_squared(candidate, existing)
                        >= spacing_squared;
                }
            );
            if (separated) {
                result.push_back(candidate);
                if (
                    static_cast<int>(result.size())
                    == problem.turbine_count()
                ) {
                    return result;
                }
            }
        }
    }
    return {};
}

void check_paper_matrix() {
    for (int availability : {70, 80, 90}) {
        for (int turbines : {5, 10, 15}) {
            const Problem problem(availability, turbines);
            assert(
                problem.id()
                == "t72_phi" + std::to_string(availability)
                    + "_n" + std::to_string(turbines)
            );
            assert(problem.turbine_count() == turbines);
            assert(!problem.receptors().empty());
            assert(problem.receptors().size() < 225U);
            assert(
                std::abs(
                    problem.measured_land_availability()
                    - static_cast<double>(availability) / 100.0
                ) < 0.02
            );
            assert(
                problem.population_size()
                == (availability == 70
                    ? 200
                    : (availability == 80 ? 150 : 100))
            );
            const auto layout = feasible_layout(problem);
            assert(layout.size() == static_cast<std::size_t>(turbines));
            const auto evaluation = problem.evaluate(layout);
            assert(evaluation.feasible);
            assert(std::isfinite(evaluation.aep_gwh));
            assert(evaluation.aep_gwh > 0.0);
            assert(std::isfinite(evaluation.maximum_spl_dba));
        }
    }
}

void check_parallel_evaluator() {
    const Problem problem(90, 5);
    const auto base = feasible_layout(problem);
    assert(base.size() == 5U);
    std::vector<std::vector<Point>> layouts(64, base);
    for (std::size_t index = 0; index < layouts.size(); ++index) {
        layouts[index][0].x_m +=
            0.01 * static_cast<double>(index);
    }
    fode::PersistentExecutor serial(1);
    fode::PersistentExecutor parallel(4);
    parallel.reset_work_receipt();
    const auto expected = problem.evaluate_population(layouts, serial);
    const auto actual = problem.evaluate_population(layouts, parallel);
    assert(expected.size() == actual.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        assert(expected[index].aep_gwh == actual[index].aep_gwh);
        assert(
            expected[index].maximum_spl_dba
            == actual[index].maximum_spl_dba
        );
        assert(
            expected[index].proximity_violation_m
            == actual[index].proximity_violation_m
        );
        assert(
            expected[index].regulatory_violation_m
            == actual[index].regulatory_violation_m
        );
    }
    assert(parallel.work_receipt().distinct_participants >= 2);
}

void check_repair() {
    const Problem problem(90, 5);
    auto layout = feasible_layout(problem);
    assert(layout.size() == 5U);
    bool found = false;
    for (int y = 0; y < 150 && !found; ++y) {
        for (int x = 0; x < 150 && !found; ++x) {
            const Point forbidden{
                3000.0 * static_cast<double>(x) / 149.0,
                3000.0 * static_cast<double>(y) / 149.0,
            };
            if (!problem.regulatory_forbidden(forbidden)) {
                continue;
            }
            bool separated = true;
            for (std::size_t other = 1; other < layout.size(); ++other) {
                if (
                    distance_squared(forbidden, layout[other])
                    < 485.0 * 485.0
                ) {
                    separated = false;
                    break;
                }
            }
            if (!separated) {
                continue;
            }
            layout[0] = forbidden;
            found = true;
        }
    }
    assert(found);
    assert(!problem.evaluate(layout).feasible);
    const auto receipt = problem.repair(layout, 50.0, 2.0);
    assert(receipt.attempted);
    assert(!receipt.timed_out);
    if (receipt.repaired) {
        assert(problem.evaluate(layout).feasible);
        assert(receipt.squared_displacement_bin2 <= 50.0 + 1.0e-9);
    }
}

void check_schedule_independent_run() {
    const Problem problem(90, 5);
    core99::t72::RunConfig config;
    config.seed = 72018;
    config.physical_fes = 200;
    config.maximum_repair_distance_bin2 = 1000.0;
    config.penalty_coefficient = 10000.0;
    config.workers = 1;
    const auto serial = core99::t72::run(problem, config);
    config.workers = 4;
    const auto parallel = core99::t72::run(problem, config);
    assert(serial.physical_fes == 200U);
    assert(parallel.physical_fes == 200U);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.front.size() == parallel.front.size());
    assert(parallel.observed_workers >= 2);
    assert(parallel.repair_seconds >= 0.0);
    assert(parallel.evaluator_seconds > 0.0);
    assert(parallel.algorithm_seconds >= 0.0);
    assert(parallel.end_to_end_seconds > 0.0);
}

}  // namespace

int main() {
    check_paper_matrix();
    check_parallel_evaluator();
    check_repair();
    check_schedule_independent_run();
    std::cout << "T72 C++ reconstruction tests passed\n";
    return 0;
}
