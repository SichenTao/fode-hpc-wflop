/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T15 common-evaluator and paper-ranking unit tests
Paper DOI: 10.2514/6.2019-0540
Public source: https://github.com/byuflowlab/iea37-wflo-casestudies
revision af88908d22795030ac2dfbe37bc38e912aee8ed6
Missing/conflicts and reconstruction: include/core99/iea37_t15.hpp
Method/problem semantic IDs: t15_iea37_comparison_protocol_v1;
t15_iea37_cs1_three_farms_cs2_cross_model_v1
Controlling contract: shared/contracts/core99_t15_iea37_2019.json
Claim boundary: paper fixed-layout comparison, not participant methods
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/iea37_t15.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>

int main() {
    const auto records = core99::t15::paper_records();
    if (records.size() != 33) {
        std::cerr << "T15 record matrix must contain 33 layouts\n";
        return EXIT_FAILURE;
    }
    for (const auto& record : records) {
        const auto value = core99::t15::evaluate(record);
        if (std::abs(value.aep_mwh - record.archived_aep_mwh) > 1.0e-5) {
            std::cerr << "T15 archived AEP mismatch " << record.id << '\n';
            return EXIT_FAILURE;
        }
        if (!std::isfinite(value.constraint_violation_m)) {
            std::cerr << "T15 archived violation is not finite " << record.id << '\n';
            return EXIT_FAILURE;
        }
    }
    const auto comparison = core99::t15::run_comparison(4);
    std::map<int, int> winning_participant;
    for (const auto& row : comparison.rows) {
        if (row.rank == 1) {
            winning_participant[row.turbines] = row.participant;
        }
    }
    for (const int turbines : {16, 36, 64}) {
        if (winning_participant[turbines] != 4) {
            std::cerr << "T15 paper winner mismatch\n";
            return EXIT_FAILURE;
        }
    }
    bool detected_archived_infeasibility = false;
    for (const auto& row : comparison.rows) {
        detected_archived_infeasibility = detected_archived_infeasibility
            || row.evaluation.constraint_violation_m > 1.0;
    }
    if (!detected_archived_infeasibility) {
        std::cerr << "T15 must preserve disclosed archived infeasibility\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
