/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T80 problem, budget and multicore replay tests
Paper/source/missing/reconstruction and semantic IDs:
hpc/core99_cpp/include/core99/bai_t80.hpp.
Public source: no T80 author source; lineage is declared in the header.
Claim boundary: academic reconstruction with a figure-derived NJ proxy.
Contract: shared/contracts/core99_t80_bai_aga_mcts_2022.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/bai_t80.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    const auto cases = core99::t80::paper_case_ids();
    require(cases.size() == 13U, "T80 paper case count mismatch");
    const core99::t80::Problem grid("t80_case1_s1_medium");
    const core99::t80::Problem jersey("t80_case2_new_jersey");
    require(
        grid.evaluator().candidate_count() == 441
        && grid.evaluator().configuration().turbine_count == 60
        && jersey.evaluator().candidate_count() == 600
        && jersey.evaluator().configuration().turbine_count == 99,
        "T80 problem dimensions mismatch"
    );
    require(
        grid.paper_population_completion() == 100
        && grid.paper_generations() == 200
        && grid.paper_mcts_simulations_completion() == 200
        && grid.paper_repeats() == 10,
        "T80 paper/completion budget mismatch"
    );
    core99::t80::RunConfig one;
    one.seed = 8080;
    one.workers = 1;
    one.population = 8;
    one.generations = 2;
    one.mcts_simulations = 5;
    const auto serial = grid.optimize(one);
    auto four = one;
    four.workers = 4;
    const auto parallel = grid.optimize(four);
    require(
        serial.scientific_hash == parallel.scientific_hash,
        "T80 one/multicore scientific replay mismatch"
    );
    require(
        parallel.observed_workers >= 2,
        "T80 multicore executor not observed"
    );
    require(
        parallel.physical_fes >= 16
        && parallel.best_evaluation.feasible
        && std::isfinite(
            parallel.best_evaluation.conversion_efficiency_percent
        )
        && parallel.best_evaluation.conversion_efficiency_percent + 1.0e-12
            >= parallel.initial_best.conversion_efficiency_percent,
        "T80 FES, feasibility or retained-best mismatch"
    );
    return 0;
}
