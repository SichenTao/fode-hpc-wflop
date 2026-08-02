/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T11 structure, evaluator, incremental and replay tests
Paper/DOI: BlockCopy-Based Operators for Evolving Efficient Wind Farm
Layouts; 10.1109/CEC.2016.7743909
Public source, missing/conflicting facts and completion policy:
hpc/core99_cpp/include/core99/blockcopy_t11.hpp
Test scope: four paper cases, four target methods, official counts and block
grids, feasible initialization, serial/all-core full identity, incremental
versus full identity, exact physical FES and schedule-independent replay.
Method/problem semantic IDs: t11_blockcopy_four_es_methods_v1;
t11_kusiak_and_2014_competition_four_cases_v1
Controlling contract: shared/contracts/core99_t11_blockcopy_2016.json
Claim boundary: regression of the declared flexible academic reproduction.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core99/blockcopy_t11.hpp"

#include "fode/executor.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

namespace {

void require_close(
    const double left,
    const double right,
    const double tolerance
) {
    assert(
        std::abs(left - right)
        <= tolerance * std::max({1.0, std::abs(left), std::abs(right)})
    );
}

}  // namespace

int main() {
    const auto problems = core99::t11::paper_problem_ids();
    const auto algorithms = core99::t11::paper_algorithm_ids();
    assert(problems.size() == 4U);
    assert(algorithms.size() == 4U);
    const int expected_counts[4]{100,100,220,710};
    const int expected_columns[4]{4,4,3,16};
    const int expected_rows[4]{4,4,16,11};
    for (std::size_t index = 0; index < problems.size(); ++index) {
        core99::t11::Problem problem(problems[index]);
        assert(problem.turbine_count() == expected_counts[index]);
        assert(problem.block_columns() == expected_columns[index]);
        assert(problem.block_rows() == expected_rows[index]);
        assert(problem.direction_count() == 24);
        const auto layout = problem.random_feasible_layout(1100 + index);
        assert(layout.size() == static_cast<std::size_t>(expected_counts[index]));
        assert(problem.constraint_violation(layout) <= 1.0e-10);
        const auto serial = problem.evaluate_full(layout);
        assert(serial.feasible);
        assert(serial.energy_cost > 0.0);
        fode::PersistentExecutor executor(4);
        executor.reset_work_receipt();
        const auto parallel = problem.evaluate_parallel(layout, executor);
        require_close(serial.energy_output_kw, parallel.energy_output_kw, 0.0);
        require_close(serial.energy_cost, parallel.energy_cost, 0.0);
        assert(executor.work_receipt().distinct_participants == 4);
    }

    core99::t11::Problem ks1("t11_ks1_n100");
    const auto parent_layout = ks1.random_feasible_layout(1111);
    auto child_layout = parent_layout;
    child_layout[0].x_m += 0.25;
    if (ks1.constraint_violation(child_layout) > 1.0e-10) {
        child_layout[0].x_m -= 0.5;
    }
    assert(ks1.constraint_violation(child_layout) <= 1.0e-10);
    fode::PersistentExecutor executor(4);
    const auto parent = ks1.make_state(parent_layout, executor);
    const auto child = ks1.update_state(*parent, child_layout, executor);
    const auto child_full = ks1.evaluate_full(child_layout);
    const auto& child_incremental = ks1.state_evaluation(*child);
    require_close(
        child_full.energy_output_kw,
        child_incremental.energy_output_kw,
        2.0e-12
    );
    require_close(
        child_full.energy_cost,
        child_incremental.energy_cost,
        2.0e-12
    );

    for (const std::string& algorithm : algorithms) {
        const core99::t11::RunConfig config{
            .algorithm_id = algorithm,
            .seed = 1117,
            .physical_fes = 25,
            .workers = 1
        };
        const auto first = core99::t11::run(ks1, config);
        const auto replay = core99::t11::run(ks1, config);
        assert(first.physical_fes == 25U);
        assert(first.final_evaluation.feasible);
        assert(first.scientific_hash == replay.scientific_hash);
        assert(
            first.final_evaluation.energy_cost
            <= first.initial_energy_cost + 1.0e-15
            || algorithm.starts_with("t11_5comma10_")
        );
    }
    const auto one = core99::t11::run(ks1, {
        .algorithm_id = "t11_1plus1_blockcopy_mutation",
        .seed = 1121,
        .physical_fes = 30,
        .workers = 1
    });
    const auto four = core99::t11::run(ks1, {
        .algorithm_id = "t11_1plus1_blockcopy_mutation",
        .seed = 1121,
        .physical_fes = 30,
        .workers = 4
    });
    assert(one.scientific_hash == four.scientific_hash);
    assert(four.observed_workers == 4);

    std::cout << "core99_t11_test: pass\n";
    return 0;
}
