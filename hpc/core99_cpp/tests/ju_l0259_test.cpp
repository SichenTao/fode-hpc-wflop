/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0259 cases, masks, evaluator, SVR and replay tests
Paper/DOI: public source, conflicts, missing facts, reconstruction
completion, semantic IDs, production backend and claim boundary:
hpc/core99_cpp/include/core99/ju_l0259.hpp.
Public source: pinned MIT author repository declared in the header.
Test scope: 117 paper problems, L0-L12 availability, D1-D3 states,
N=15/20/25, paper/source variants, fixed-layout evaluation, from-scratch
SVR, exact physical FES and schedule-independent all-core replay.
Controlling contract: shared/contracts/core99_l0259_sugga_2019.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core99/ju_l0259.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    const auto cases = core99::l0259::paper_case_ids();
    assert(cases.size() == 117U);
    assert(cases.front() == "l0259_d1_l0_n15");
    assert(cases.back() == "l0259_d3_l12_n25");
    for (const int landscape : {0, 1, 6, 7, 12}) {
        const std::string id =
            "l0259_d3_l" + std::to_string(landscape) + "_n25";
        core99::l0259::Problem problem(id);
        assert(problem.turbine_count() == 25);
        assert(problem.wind_state_count() == 6);
        const int expected_available =
            landscape == 0 ? 144 : (landscape <= 6 ? 120 : 132);
        assert(problem.available_count() == expected_available);
        assert(problem.cell_width_m() == 154.0);
        assert(problem.hub_height_m() == 88.0);
        const auto layout = core99::l0259::regular_reference_layout(problem);
        const auto evaluation = problem.evaluate(layout);
        assert(evaluation.feasible);
        assert(evaluation.turbine_power_kw.size() == 25U);
        assert(evaluation.expected_power_kw > 0.0);
        assert(evaluation.efficiency_percent > 0.0);
        assert(evaluation.efficiency_percent <= 100.0 + 1.0e-12);
    }
    core99::l0259::Problem paper("l0259_d1_l5_n15");
    core99::l0259::Problem source(
        "l0259_d1_l5_n15",
        "source_normal_threshold"
    );
    assert(source.cell_width_m() == 231.0);
    assert(source.hub_height_m() == 80.0);
    const auto surface = paper.train_surrogate(200, 259259, 4);
    assert(surface.training_targets_kw.size() == 144U);
    assert(surface.predictions_kw.size() == 144U);
    assert(surface.observed_workers == 4);
    assert(std::all_of(
        surface.predictions_kw.begin(),
        surface.predictions_kw.end(),
        [](const double value) { return std::isfinite(value); }
    ));

    core99::l0259::RunConfig one;
    one.seed = 259100;
    one.workers = 1;
    one.monte_carlo_layouts = 200;
    one.population = 24;
    one.generations = 4;
    const auto serial = paper.optimize(one);
    auto parallel_config = one;
    parallel_config.workers = 4;
    const auto parallel = paper.optimize(parallel_config);
    assert(serial.scientific_hash == parallel.scientific_hash);
    assert(serial.physical_fes == 296U);
    assert(parallel.physical_fes == 296U);
    assert(parallel.observed_workers == 4);
    assert(parallel.best_evaluation.feasible);
    assert(
        parallel.best_evaluation.efficiency_percent
        + 1.0e-12
        >= parallel.initial_best.efficiency_percent
    );
    auto source_config = parallel_config;
    source_config.variant = "source_normal_threshold";
    const auto source_result = source.optimize(source_config);
    assert(
        source_result.method_semantic_id
        == "l0259_sugga_source_normal_threshold_v1"
    );
    std::cout << "core99_l0259_cpp_pass\n";
    return 0;
}
