/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T58 semantic, source-anchor and deterministic-HPC tests
Paper/DOI: Rethore et al.; 10.1002/we.1667
Facts and completion boundary: include/core99/rethore_t58.hpp
Public source provenance is declared in that header. Claim boundary: these
tests validate the declared reconstruction, not an author-trajectory replay.
Controlling contract: shared/contracts/core99_t58_rethore_topfarm_2014.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/rethore_t58.hpp"

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

void require_same_science(
    const core99::t58::Evaluation& one,
    const core99::t58::Evaluation& all,
    const std::string& label
) {
    require(same_bits(one.gross_aep_mwh_per_year, all.gross_aep_mwh_per_year),
            label + " gross AEP differs");
    require(same_bits(one.net_aep_mwh_per_year, all.net_aep_mwh_per_year),
            label + " net AEP differs");
    require(same_bits(one.financial_balance_meur, all.financial_balance_meur),
            label + " balance differs");
    require(one.feasible == all.feasible, label + " feasibility differs");
}

}  // namespace

int main() {
    const std::array<core99::t58::CaseId, 3> cases{
        core99::t58::CaseId::fictitious_2x3,
        core99::t58::CaseId::stags_holt_coldham,
        core99::t58::CaseId::middelgrunden
    };
    for (const auto case_id : cases) {
        const core99::t58::Problem problem(case_id);
        require(problem.semantic_id()
            == "t58_topfarm_three_case_financial_declared_v1",
            "T58 problem semantic ID mismatch");
        require(problem.fine_wind_state_count() == 144,
            "T58 fine HAWC2-DWM grid state count mismatch");
        require(problem.candidate_count() >= 4 * problem.turbine_count(),
            "T58 SGA grid unexpectedly small");
        fode::PersistentExecutor one(1);
        fode::PersistentExecutor all(20);
        core99::t58::EvaluationSettings settings;
        const auto serial = problem.evaluate(problem.baseline_layout(), settings, one);
        const auto parallel = problem.evaluate(problem.baseline_layout(), settings, all);
        require_same_science(serial, parallel, core99::t58::case_name(case_id));
        require(parallel.observed_workers > 1,
            "T58 baseline did not engage multiple workers");
        require(serial.net_aep_mwh_per_year > 0.0,
            "T58 baseline AEP is invalid");
        require(serial.feasible, "T58 reconstructed baseline is infeasible");
        if (case_id == core99::t58::CaseId::stags_holt_coldham) {
            require(std::abs(serial.energy_efficiency_percent - 89.4) < 1.0e-8,
                "T58 Stags report baseline-efficiency calibration differs");
        }
        if (case_id == core99::t58::CaseId::middelgrunden) {
            require(std::abs(serial.energy_efficiency_percent - 83.9) < 1.0e-8,
                "T58 Middelgrunden report baseline-efficiency calibration differs");
        }
        core99::t58::EvaluationSettings no_fatigue = settings;
        no_fatigue.fatigue_scale = 0.0;
        const auto without_fatigue = problem.evaluate(
            problem.baseline_layout(), no_fatigue, all);
        require(without_fatigue.financial_balance_meur
                > serial.financial_balance_meur,
            "T58 fatigue branch is inactive");
        core99::t58::EvaluationSettings double_cable = settings;
        double_cable.cable_scale = 2.0;
        const auto costly_cable = problem.evaluate(
            problem.baseline_layout(), double_cable, all);
        require(costly_cable.financial_balance_meur
                < serial.financial_balance_meur,
            "T58 cable-cost sensitivity is inactive");
    }

    for (const auto method : {core99::t58::Method::slp_only,
                              core99::t58::Method::sga_only,
                              core99::t58::Method::sga_slp}) {
        core99::t58::RunConfig one_config;
        one_config.method = method;
        one_config.workers = 1;
        one_config.seed = 58077;
        one_config.smoke = true;
        core99::t58::RunConfig all_config = one_config;
        all_config.workers = 20;
        const core99::t58::Problem serial_problem(
            core99::t58::CaseId::fictitious_2x3);
        const core99::t58::Problem parallel_problem(
            core99::t58::CaseId::fictitious_2x3);
        const auto serial = core99::t58::run(serial_problem, one_config);
        const auto parallel = core99::t58::run(parallel_problem, all_config);
        require(serial.scientific_hash == parallel.scientific_hash,
            "T58 one/all-core optimizer hash differs");
        require(serial.final_layout == parallel.final_layout,
            "T58 one/all-core optimizer layout differs");
        require_same_science(serial.final_evaluation, parallel.final_evaluation,
                             core99::t58::method_name(method));
        require(parallel.observed_workers > 1,
            "T58 optimizer did not engage multiple workers");
        require(parallel.physical_fes > 0, "T58 optimizer FES missing");
        if (method != core99::t58::Method::sga_only) {
            require(parallel.final_evaluation.feasible,
                "T58 SLP-final smoke layout is infeasible");
        }
    }
    std::cout << "T58 semantic and deterministic-HPC tests passed\n";
    return 0;
}
