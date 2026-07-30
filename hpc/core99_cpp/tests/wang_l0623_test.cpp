/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0623 model, case, budget and replay tests
Paper: Wang et al., 10.1016/j.oceaneng.2023.116644.
Source/reconstruction/claim:
hpc/core99_cpp/include/core99/wang_l0623.hpp
Contract: shared/contracts/core99_l0623_wang_cfd_kriging_2024.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/wang_l0623.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    const auto cases = core99::l0623::paper_case_ids();
    require(cases.size() == 3U, "L0623 case count mismatch");
    const core99::l0623::Problem case1(cases[0]);
    const core99::l0623::Problem case2(cases[1]);
    const core99::l0623::Problem case3(cases[2]);
    require(
        case1.wind_direction_count() == 1
        && case2.wind_direction_count() == 8
        && case3.wind_direction_count() == 8,
        "L0623 wind contract mismatch"
    );
    require(
        !case1.has_gaussian_hill()
        && !case2.has_gaussian_hill()
        && case3.has_gaussian_hill(),
        "L0623 terrain contract mismatch"
    );
    require(
        case1.paper_initial_samples() == 360
        && case1.paper_population() == 50
        && case1.paper_maximum_ga_generations() == 1000,
        "L0623 framework settings mismatch"
    );
    require(
        case1.paper_truth_calls() == 437
        && case2.paper_truth_calls() == 400
        && case3.paper_truth_calls() == 399,
        "L0623 truth-call contract mismatch"
    );
    const auto baseline = core99::l0623::paper_baseline_layout();
    const auto first = case1.evaluate_truth(baseline);
    const auto second = case2.evaluate_truth(baseline);
    const auto third = case3.evaluate_truth(baseline);
    require(
        first.feasible && second.feasible && third.feasible,
        "L0623 source-figure baseline infeasible"
    );
    require(
        first.aep_gwh > 0.0 && second.aep_gwh > 0.0
        && third.aep_gwh > second.aep_gwh,
        "L0623 physical case response mismatch"
    );

    core99::l0623::RunConfig serial;
    serial.seed = 623623;
    serial.workers = 1;
    serial.initial_samples = 32;
    serial.maximum_truth_calls = 34;
    serial.maximum_ga_generations = 4;
    const auto one = case1.optimize(serial);
    auto parallel = serial;
    parallel.workers = 4;
    const auto four = case1.optimize(parallel);
    require(
        one.truth_calls == 34 && four.truth_calls == 34,
        "L0623 truth-call accounting mismatch"
    );
    require(
        one.scientific_hash == four.scientific_hash,
        "L0623 one/multicore replay mismatch"
    );
    require(
        four.observed_workers >= 2,
        "L0623 multicore backend was not observed"
    );
    require(
        four.best_evaluation.feasible
        && std::isfinite(four.best_evaluation.aep_gwh)
        && four.best_evaluation.aep_gwh + 1.0e-9
            >= four.initial_best.aep_gwh,
        "L0623 optimization infeasible, non-finite or regressed"
    );
    return 0;
}
