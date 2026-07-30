/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0124 Gaussian equation, case, constraint, MI-LXPM
accounting, multicore and replay tests
Paper/DOI/source/missing/reconstruction/claim:
hpc/core99_cpp/include/core99/parada_l0124.hpp
Controlling contract: shared/contracts/core99_l0124_parada_2017.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/parada_l0124.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<int> spaced_layout(
    const int grid_size,
    const int turbines,
    const int stride
) {
    std::vector<int> coordinates;
    coordinates.reserve(static_cast<std::size_t>(2 * turbines));
    for (int y = 0; y < grid_size && coordinates.size()
        < static_cast<std::size_t>(2 * turbines); y += stride) {
        for (int x = 0; x < grid_size && coordinates.size()
            < static_cast<std::size_t>(2 * turbines); x += stride) {
            coordinates.push_back(x);
            coordinates.push_back(y);
        }
    }
    require(
        coordinates.size() == static_cast<std::size_t>(2 * turbines),
        "test layout has insufficient locations"
    );
    return coordinates;
}

}  // namespace

int main() {
    constexpr double ct = 0.88;
    constexpr double k = 0.055;
    constexpr double diameter = 40.0;
    const double root = std::sqrt(1.0 - ct);
    const double beta = 0.5 * (1.0 + root) / root;
    const double epsilon = 0.2 * std::sqrt(beta);
    const double downstream = 5.0 * diameter;
    const double sigma = k * downstream / diameter + epsilon;
    const double expected = (
        1.0 - std::sqrt(1.0 - ct / (8.0 * sigma * sigma))
    );
    require(
        std::abs(
            core99::l0124::gaussian_deficit_ratio(downstream, 0.0)
            - expected
        ) < 1.0e-14,
        "Gaussian centreline equation mismatch"
    );
    require(
        core99::l0124::gaussian_deficit_ratio(119.999, 0.0) == 0.0,
        "published 3D validity boundary is not enforced"
    );

    const core99::l0124::Problem case_a("l0124_case_a_grid10");
    require(
        case_a.semantic_id() == "l0124_parada_gaussian_grid_v1",
        "L0124 problem semantic ID mismatch"
    );
    require(case_a.turbine_count() == 30, "case-A turbine count mismatch");
    require(
        std::abs(case_a.no_wake_power_kw() - 15552.0) < 1.0e-10,
        "case-A no-wake power mismatch"
    );
    const auto layout_a = spaced_layout(10, 30, 1);
    const auto evaluation_a = case_a.evaluate(layout_a);
    require(evaluation_a.feasible, "case-A test layout is infeasible");
    require(
        evaluation_a.expected_power_kw > 0.0
            && evaluation_a.expected_power_kw <= case_a.no_wake_power_kw(),
        "case-A power is outside its physical bounds"
    );
    auto duplicate = layout_a;
    duplicate[2] = duplicate[0];
    duplicate[3] = duplicate[1];
    require(
        !case_a.evaluate(duplicate).feasible,
        "duplicate-turbine constraint was not detected"
    );

    const core99::l0124::Problem case_c("l0124_case_c_grid20");
    require(case_c.turbine_count() == 39, "case-C turbine count mismatch");
    require(
        std::abs(case_c.no_wake_power_kw() - 36506.1879) < 1.0e-6,
        "case-C Fig. 6 no-wake identity mismatch"
    );
    const double table_4_no_wake_kw = 34338.0 / 0.9407;
    require(
        std::abs(case_c.no_wake_power_kw() - table_4_no_wake_kw)
            / table_4_no_wake_kw < 1.0e-4,
        "case-C Fig. 6 digitization does not close against Table 4"
    );
    const auto layout_c = spaced_layout(20, 39, 2);
    const auto evaluation_c = case_c.evaluate(layout_c);
    require(evaluation_c.feasible, "case-C test layout is infeasible");
    require(
        evaluation_c.expected_power_kw > 0.0
            && evaluation_c.efficiency <= 1.0,
        "case-C evaluation is outside its physical bounds"
    );

    core99::l0124::RunConfig config;
    config.seed = 120124;
    config.workers = 4;
    config.population = 60;
    config.generations = 3;
    const auto first = core99::l0124::run(case_a, config);
    const auto replay = core99::l0124::run(case_a, config);
    require(first.physical_fes == 240, "L0124 physical FES mismatch");
    require(first.observed_workers >= 2, "no multicore population evidence");
    require(first.best_evaluation.feasible, "smoke run found no feasible layout");
    require(
        first.scientific_hash == replay.scientific_hash,
        "fixed-seed MI-LXPM replay mismatch"
    );
    require(
        first.best_objective_history.size() == 4U,
        "MI-LXPM history length mismatch"
    );
    return 0;
}
