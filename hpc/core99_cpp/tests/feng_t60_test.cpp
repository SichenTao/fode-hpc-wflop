/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T60 structural, incremental-equivalence and HPC tests
Paper/DOI: Solving the Wind Farm Layout Optimization Problem Using Random
Search Algorithm; 10.1016/j.renene.2015.01.005
Public source, missing/conflicting fields and completion policy:
hpc/core99_cpp/include/core99/feng_t60.hpp
Test scope: all six paper problems, 30/39/39/80 turbine identities,
Figure-1/public-Horns starts, full versus moved-turbine incremental equality,
fixed-order all-core evaluation, exact FES and schedule-independent replay.
Method/problem semantic IDs: t60_improved_rs_incremental_v1;
t60_ideal_continuous_jensen_v1; t60_hornsrev_jensen_v80_v1
Controlling contract: shared/contracts/core99_t60_feng_shen_2015.json
Claim boundary: regression of the declared flexible academic reproduction
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/feng_t60.hpp"

#include "fode/executor.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

void require_close(
    const double left,
    const double right,
    const double relative_tolerance
) {
    assert(
        std::abs(left - right)
        <= relative_tolerance * std::max({1.0, std::abs(left), std::abs(right)})
    );
}

}  // namespace

int main() {
    const auto ids = core99::t60::paper_problem_ids();
    assert(ids.size() == 6U);
    for (const std::string& id : ids) {
        core99::t60::Problem problem(id);
        const int expected = id == "t60_ideal_case1"
            ? 30 : (id.starts_with("t60_ideal_") ? 39 : 80);
        assert(problem.turbine_count() == expected);
        const auto layout = problem.paper_initial_layout();
        assert(layout.size() == static_cast<std::size_t>(expected));
        const auto evaluation = problem.evaluate_full(layout);
        assert(evaluation.feasible);
        assert(evaluation.expected_power_kw > 0.0);
        assert(evaluation.efficiency > 0.0);
        assert(evaluation.efficiency <= 1.0 + 1.0e-12);
    }

    core99::t60::Problem horns("t60_horns_case1");
    const auto horns_layout = horns.paper_initial_layout();
    fode::PersistentExecutor executor(4);
    executor.reset_work_receipt();
    const auto full = horns.evaluate_full(horns_layout);
    const auto parallel = horns.evaluate_parallel(horns_layout, executor);
    require_close(full.expected_power_kw, parallel.expected_power_kw, 1.0e-13);
    assert(executor.work_receipt().distinct_participants == 4);

    std::vector<core99::t60::Point> candidate = horns_layout;
    candidate[17].x_m += 8.0;
    candidate[17].y_m += 4.0;
    const auto candidate_full = horns.evaluate_full(candidate);
    const auto candidate_incremental =
        horns.evaluate_incremental_candidate(horns_layout, candidate, 17);
    assert(candidate_full.feasible);
    require_close(
        candidate_full.expected_power_kw,
        candidate_incremental.expected_power_kw,
        2.0e-13
    );

    const auto random_layout = horns.random_feasible_layout(6001);
    assert(horns.evaluate_full(random_layout).feasible);

    core99::t60::Problem ideal("t60_ideal_case1");
    const core99::t60::RunConfig config{
        .seed = 6015,
        .physical_fes = 25,
        .random_initial_layout = false,
    };
    const auto first = core99::t60::run(ideal, config);
    const auto replay = core99::t60::run(ideal, config);
    assert(first.physical_fes == 25U);
    assert(first.feasible_proposals == 24U);
    assert(first.scientific_hash == replay.scientific_hash);
    assert(
        first.final_evaluation.expected_power_kw
        >= first.initial_power_kw
    );

    std::cout << "core99_t60_test: pass\n";
    return 0;
}
