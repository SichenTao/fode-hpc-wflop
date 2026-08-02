/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y13 semantic, physics and deterministic-HPC tests
Paper/DOI: Du et al.; 10.1109/TSTE.2025.3609006.
Public source: no target code is public; cited-source details are in
include/core99/du_y13.hpp.
Missing and Reconstruction: all absent assets and declared completion choices
are listed in that header.
Semantic IDs: y13_four_grid_fg36_declared_v1,
y13_l2box_consensus_admm_highs_declared_v1 and
y13_native_four_case_single_run_v1.
Claim boundary: equation-level flexible academic reproduction; full boundary
is recorded in include/core99/du_y13.hpp.
Controlling contract: shared/contracts/core99_y13_du_grid_admm_2026.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/du_y13.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool same_bits(const double left, const double right) {
    return std::memcmp(&left, &right, sizeof(double)) == 0;
}

}  // namespace

int main() {
    const std::array<core99::y13::CaseId, 4> cases{
        core99::y13::CaseId::grid_6,
        core99::y13::CaseId::grid_10,
        core99::y13::CaseId::grid_16,
        core99::y13::CaseId::grid_20,
    };
    for (const auto case_id : cases) {
        const core99::y13::Problem problem(case_id, 20);
        const int side = static_cast<int>(case_id);
        require(problem.grid_side() == side, "Y13 grid side differs");
        require(problem.cell_count() == side * side, "Y13 cell count differs");
        require(problem.turbine_count() == side * side / 2,
                "Y13 turbine count differs");
        require(problem.wind_scenario_count() == 36,
                "Y13 wind scenario count differs");
        require(std::abs(problem.cell_pitch_m() - 630.0) < 1.0e-12,
                "Y13 5D pitch differs");
        require(static_cast<int>(problem.warm_start().size())
                    == problem.turbine_count(),
                "Y13 warm-start cardinality differs");
        fode::PersistentExecutor executor(20);
        const auto evaluation = problem.evaluate(problem.warm_start(), executor);
        require(evaluation.net_aep_gwh > 0.0, "Y13 AEP is not positive");
        require(evaluation.net_aep_gwh <= evaluation.gross_aep_gwh,
                "Y13 AEP exceeds gross power");
        require(evaluation.observed_workers > 1,
                "Y13 evaluator did not use multiple workers");
    }

    core99::y13::RunConfig one_config;
    one_config.workers = 1;
    one_config.smoke = true;
    core99::y13::RunConfig all_config = one_config;
    all_config.workers = 20;
    const core99::y13::Problem one_problem(core99::y13::CaseId::grid_6, 1);
    const core99::y13::Problem all_problem(core99::y13::CaseId::grid_6, 20);
    const auto one = core99::y13::run(one_problem, one_config);
    const auto all = core99::y13::run(all_problem, all_config);
    require(one.scientific_hash == all.scientific_hash,
            "Y13 one/all-core scientific hash differs");
    require(one.selected_cells == all.selected_cells,
            "Y13 one/all-core layout differs");
    require(same_bits(one.final_evaluation.net_aep_gwh,
                      all.final_evaluation.net_aep_gwh),
            "Y13 one/all-core AEP differs");
    require(all.observed_workers > 1,
            "Y13 optimizer did not use multiple workers");
    require(all.scenario_subproblem_solves == 72,
            "Y13 two-iteration scenario-subproblem work differs");
    std::cout << "Y13 semantic and deterministic-HPC tests passed\n";
    return 0;
}
