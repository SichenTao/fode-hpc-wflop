/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T74 problem, budget, variant and deterministic replay tests
Paper/source/reconstruction: hpc/core99_cpp/include/core99/ju_t74.hpp.
Public source: pinned MIT WFLOP_Python asset declared in the header.
Missing/completion and semantic IDs: declared in the header and contract.
Claim boundary: academic reconstruction, not author numerical replay.
Contract: shared/contracts/core99_t74_ju_siga_2019.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/ju_t74.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    const auto cases = core99::t74::paper_case_ids();
    require(cases.size() == 15U, "T74 paper case count mismatch");
    const core99::t74::Problem small("t74_case1_small");
    const core99::t74::Problem medium("t74_case1_medium");
    const core99::t74::Problem complex("t74_case5_large");
    require(
        small.cell_width_m() == 231.0
        && medium.cell_width_m() == 308.0
        && complex.cell_width_m() == 385.0,
        "T74 cell widths mismatch"
    );
    require(
        small.wind_state_count() == 1
        && complex.wind_state_count() == 36,
        "T74 wind states mismatch"
    );
    require(
        small.paper_population() == 100
        && small.paper_generations() == 200
        && small.paper_monte_carlo_layouts() == 10000,
        "T74 paper budget mismatch"
    );
    const auto reference = core99::t74::regular_reference_layout();
    const auto small_evaluation = small.evaluate(reference);
    const auto medium_evaluation = medium.evaluate(reference);
    require(
        small_evaluation.feasible && medium_evaluation.feasible,
        "T74 reference layout infeasible"
    );
    require(
        medium_evaluation.efficiency_percent
            > small_evaluation.efficiency_percent,
        "T74 farm-size physical response mismatch"
    );

    core99::t74::RunConfig one;
    one.seed = 7474;
    one.workers = 1;
    one.monte_carlo_layouts = 80;
    one.generations = 3;
    const auto serial = medium.optimize(one);
    auto four = one;
    four.workers = 4;
    const auto parallel = medium.optimize(four);
    require(
        serial.scientific_hash == parallel.scientific_hash,
        "T74 one/multicore replay mismatch"
    );
    require(
        parallel.observed_workers >= 2,
        "T74 multicore executor was not observed"
    );
    require(
        parallel.physical_fes == 380
        && parallel.best_evaluation.feasible
        && std::isfinite(parallel.best_evaluation.efficiency_percent)
        && parallel.best_evaluation.efficiency_percent + 1.0e-12
            >= parallel.initial_best.efficiency_percent,
        "T74 budget, feasibility or monotonic best mismatch"
    );
    auto source = one;
    source.variant = "source_normal_threshold";
    const auto source_result = medium.optimize(source);
    require(
        source_result.method_semantic_id
            == "t74_siga_source_normal_threshold_v1",
        "T74 conflict variant identity mismatch"
    );
    return 0;
}
