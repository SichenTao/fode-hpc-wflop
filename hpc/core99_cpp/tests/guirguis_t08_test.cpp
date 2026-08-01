/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T08 equation, derivative, feasibility, worker-identity
and optimizer smoke tests
Paper/DOI: 10.1016/j.apenergy.2016.06.101
Public source: no paper-linked author implementation was found.
Missing fields, declared reconstruction, semantic IDs and Claim boundary:
hpc/core99_cpp/include/core99/guirguis_t08.hpp
Controlling contract: shared/contracts/core99_t08_guirguis_2016.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "core99/guirguis_t08.hpp"

#include "fode/executor.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

int main() {
    using core99::t08::Problem;
    using core99::t08::StartPolicy;

    const auto cases = core99::t08::paper_case_ids();
    assert(cases.size() == 20U);
    for (const std::string& case_id : cases) {
        const Problem problem(case_id);
        const auto usl = problem.initial_layout(
            StartPolicy::uniform_staggered, 201606101ULL, 0
        );
        const auto lhs = problem.initial_layout(
            StartPolicy::latin_hypercube_feasible, 201606101ULL, 1
        );
        assert(problem.feasible(usl));
        assert(problem.feasible(lhs));
        assert(problem.minimum_spacing(usl)
               > problem.paper_case().minimum_spacing_m);
    }

    const Problem problem("t08_benchmark_c2_n10");
    auto layout = problem.initial_layout(
        StartPolicy::latin_hypercube_feasible, 801ULL, 0
    );
    fode::PersistentExecutor one_worker(1);
    fode::PersistentExecutor four_workers(4);
    const auto serial = problem.evaluate(layout, true, one_worker);
    const auto parallel = problem.evaluate(layout, true, four_workers);
    assert(std::bit_cast<std::uint64_t>(serial.efficiency_percent)
           == std::bit_cast<std::uint64_t>(parallel.efficiency_percent));
    assert(serial.gradient_percent_per_m.size()
           == parallel.gradient_percent_per_m.size());
    for (std::size_t index = 0; index < serial.gradient_percent_per_m.size(); ++index) {
        assert(std::bit_cast<std::uint64_t>(serial.gradient_percent_per_m[index])
               == std::bit_cast<std::uint64_t>(parallel.gradient_percent_per_m[index]));
    }

    constexpr double step = 1.0e-3;
    double largest_error = 0.0;
    for (std::size_t variable = 0; variable < 6U; ++variable) {
        auto plus = layout;
        auto minus = layout;
        double* plus_value = variable % 2U == 0U
            ? &plus[variable / 2U].x_m : &plus[variable / 2U].y_m;
        double* minus_value = variable % 2U == 0U
            ? &minus[variable / 2U].x_m : &minus[variable / 2U].y_m;
        *plus_value += step;
        *minus_value -= step;
        const double finite_difference = (
            problem.evaluate(plus, false, one_worker).efficiency_percent
            - problem.evaluate(minus, false, one_worker).efficiency_percent
        ) / (2.0 * step);
        largest_error = std::max(
            largest_error,
            std::abs(finite_difference - serial.gradient_percent_per_m[variable])
        );
    }
    assert(largest_error < 2.0e-7);

    const auto serial_barrier = problem.barrier(layout, 0.05, true, one_worker);
    const auto parallel_barrier = problem.barrier(layout, 0.05, true, four_workers);
    assert(std::bit_cast<std::uint64_t>(serial_barrier.barrier_value)
           == std::bit_cast<std::uint64_t>(parallel_barrier.barrier_value));
    for (std::size_t index = 0;
         index < serial_barrier.barrier_gradient_per_m.size(); ++index) {
        assert(std::bit_cast<std::uint64_t>(
            serial_barrier.barrier_gradient_per_m[index]
        ) == std::bit_cast<std::uint64_t>(
            parallel_barrier.barrier_gradient_per_m[index]
        ));
    }

    core99::t08::OptimizationConfig config;
    config.start_policy = StartPolicy::uniform_staggered;
    config.starts = 1;
    config.workers = 4;
    config.maximum_evaluations_per_start = 50;
    config.barrier_phases = 2;
    const auto optimized = core99::t08::optimize(problem, config);
    assert(optimized.maximum_constraint_violation < 0.0);
    assert(optimized.minimum_spacing_m > problem.paper_case().minimum_spacing_m);
    assert(optimized.best_efficiency_percent > 0.0);
    assert(optimized.physical_layout_evaluations > 0U);

    std::cout << "T08 exact-gradient interior-point tests passed\n";
    return 0;
}
