/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0341 model, diagnostic, case, budget and replay tests
Paper: Tao et al., 10.1016/j.renene.2020.06.003.
Public source: no target code/data; recovered predecessor facts and missing
fields are registered in the contract.
Reconstruction: declared 3-D wake/JPDF/curve and MDPSO completions.
Semantic IDs: l0341_three_farm_3d_gaussian_v1;
l0341_mdpso_predecessor_completed_v1.
Contract: shared/contracts/core99_l0341_tao_3d_mdpso_2020.json.
Claim boundary: semantic, physical and replay tests, not numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/tao_l0341.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    const auto cases = core99::l0341::paper_case_ids();
    require(cases.size() == 10U, "L0341 case count mismatch");
    const core99::l0341::Problem uniform("l0341_uniform_wfa_c");
    require(!uniform.nonuniform(), "uniform class mismatch");
    require(uniform.maximum_slots() == 25, "WFA slot mismatch");
    require(uniform.decision_dimension() == 51, "uniform dimension mismatch");
    require(uniform.paper_population() == 1020, "uniform population mismatch");
    require(uniform.paper_generations() == 15000, "uniform budget mismatch");
    const core99::l0341::Problem nonuniform(
        "l0341_nonuniform_wfc_c"
    );
    require(nonuniform.nonuniform(), "nonuniform class mismatch");
    require(nonuniform.maximum_slots() == 60, "WFC slot mismatch");
    require(nonuniform.decision_dimension() == 180, "nonuniform dimension");
    require(nonuniform.paper_population() == 3600, "nonuniform population");
    require(
        nonuniform.paper_generations() == 20000,
        "nonuniform budget mismatch"
    );
    const auto low = core99::l0341::evaluate_diagnostic_4x4(2.0, 0.0);
    const auto normal = core99::l0341::evaluate_diagnostic_4x4(8.0, 0.0);
    const auto oblique = core99::l0341::evaluate_diagnostic_4x4(8.0, 30.0);
    const auto cutout = core99::l0341::evaluate_diagnostic_4x4(25.0, 0.0);
    require(low.feasible && normal.feasible, "diagnostic layout infeasible");
    require(low.expected_power_mw == 0.0, "cut-in diagnostic mismatch");
    require(cutout.expected_power_mw == 0.0, "cut-out diagnostic mismatch");
    require(
        normal.expected_power_mw > 0.0
            && oblique.expected_power_mw > normal.expected_power_mw,
        "diagnostic direction response mismatch"
    );

    const core99::l0341::Problem smoke("l0341_uniform_wfa_a");
    core99::l0341::RunConfig serial;
    serial.seed = 341341;
    serial.workers = 1;
    serial.generations = 2;
    serial.population_override = 64;
    const auto first = smoke.optimize(serial);
    auto parallel = serial;
    parallel.workers = 4;
    const auto second = smoke.optimize(parallel);
    require(
        first.physical_fes == 192U && second.physical_fes == 192U,
        "L0341 physical-FES mismatch"
    );
    require(
        first.scientific_hash == second.scientific_hash,
        "L0341 one/multicore replay mismatch"
    );
    require(
        second.observed_workers >= 2,
        "L0341 multicore backend not observed"
    );
    require(
        second.best_evaluation.feasible
            && second.best_evaluation.expected_power_mw + 1.0e-9
                >= second.initial_best.expected_power_mw,
        "L0341 smoke optimization regressed or stayed infeasible"
    );
    return 0;
}
