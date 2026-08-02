/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0245 semantic, numerical and deterministic-HPC tests.
Paper/DOI, public assets, missing data, conflicts, reconstruction decisions,
semantic IDs, HPC design and claim boundary:
hpc/core99_cpp/include/core99/padron_l0245.hpp.
Controlling contract: shared/contracts/core99_l0245_padron_2019.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/padron_l0245.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_same_double(
    const double first, const double second, const std::string& message
) {
    require(
        std::bit_cast<std::uint64_t>(first)
            == std::bit_cast<std::uint64_t>(second),
        message
    );
}

}  // namespace

int main(const int argc, char** argv) {
    require(argc == 2, "L0245 test requires public-data path");
    core99::l0245::Problem problem(argv[1]);
    for (const auto layout : {
             core99::l0245::LayoutId::grid,
             core99::l0245::LayoutId::amalia,
             core99::l0245::LayoutId::optimized,
             core99::l0245::LayoutId::random,
         }) {
        require(problem.layout(layout).size() == core99::l0245::turbine_count,
                "L0245 public layout cardinality differs");
    }
    require(problem.method(core99::l0245::MethodId::pcr_coarse)
                .physical_wind_states == 231,
            "L0245 PC-R coarse sample count differs");
    require(problem.method(core99::l0245::MethodId::pcr_fine)
                .physical_wind_states == 630,
            "L0245 PC-R fine sample count differs");
    require(problem.method(core99::l0245::MethodId::rectangle_coarse)
                .physical_wind_states == 225,
            "L0245 rectangle coarse sample count differs");
    require(problem.method(core99::l0245::MethodId::rectangle_fine)
                .physical_wind_states == 625,
            "L0245 rectangle fine sample count differs");
    require(problem.method(core99::l0245::MethodId::monte_carlo_reference)
                .physical_wind_states == 200000,
            "L0245 Monte-Carlo reference count differs");

    const auto& amalia = problem.layout(core99::l0245::LayoutId::amalia);
    const auto serial = problem.evaluate(
        amalia, core99::l0245::MethodId::rectangle_coarse,
        2019024501ULL, 1, true
    );
    const auto parallel = problem.evaluate(
        amalia, core99::l0245::MethodId::rectangle_coarse,
        2019024501ULL, 20, true
    );
    require_same_double(serial.aep_gwh, parallel.aep_gwh,
                        "L0245 one/all-core AEP differs");
    require(serial.gradient_gwh_per_m == parallel.gradient_gwh_per_m,
            "L0245 one/all-core gradient differs");
    require(parallel.observed_workers > 1,
            "L0245 all-core evaluator did not participate");
    require(parallel.feasible, "L0245 Amalia layout unexpectedly infeasible");
    require(parallel.gradient_gwh_per_m.size() == 120,
            "L0245 gradient cardinality differs");
    for (const double derivative : parallel.gradient_gwh_per_m) {
        require(std::isfinite(derivative), "L0245 gradient is non-finite");
    }

    auto displaced_plus = amalia;
    auto displaced_minus = amalia;
    constexpr double step = 0.05;
    displaced_plus[0].x_m += step;
    displaced_minus[0].x_m -= step;
    const double plus = problem.evaluate(
        displaced_plus, core99::l0245::MethodId::rectangle_coarse,
        2019024501ULL, 20, false
    ).aep_gwh;
    const double minus = problem.evaluate(
        displaced_minus, core99::l0245::MethodId::rectangle_coarse,
        2019024501ULL, 20, false
    ).aep_gwh;
    const double finite_difference = (plus - minus) / (2.0 * step);
    const double automatic = parallel.gradient_gwh_per_m[0];
    const double scaled_error = std::abs(finite_difference - automatic)
        / std::max({1.0e-6, std::abs(finite_difference), std::abs(automatic)});
    require(scaled_error < 2.0e-3,
            "L0245 automatic derivative differs from finite difference");

    const auto pcr = problem.evaluate(
        amalia, core99::l0245::MethodId::pcr_coarse,
        2019024501ULL, 20, false
    );
    require(pcr.selected_polynomial_degree >= 1
                && pcr.selected_polynomial_degree <= 11,
            "L0245 cross-validation degree differs");
    require(pcr.physical_wake_simulations == 231,
            "L0245 PC-R physical simulations differ");

    core99::l0245::RunConfig config;
    config.starting_layout = core99::l0245::LayoutId::amalia;
    config.method = core99::l0245::MethodId::pcr_coarse;
    config.seed = 2019024501ULL;
    config.workers = 20;
    config.maximum_evaluations = 2;
    config.smoke = true;
    const auto optimized = problem.optimize(config);
    require(optimized.observed_workers > 1,
            "L0245 optimizer did not engage multiple workers");
    require(optimized.final_evaluation.feasible,
            "L0245 optimizer returned infeasible layout");
    require(optimized.final_evaluation.aep_gwh + 1.0e-9
                >= optimized.initial_evaluation.aep_gwh,
            "L0245 optimizer reduced AEP");
    require(optimized.final_layout.size() == core99::l0245::turbine_count,
            "L0245 optimized layout cardinality differs");
    std::cout << "L0245 semantic, derivative and deterministic-HPC tests passed\n";
    return 0;
}
