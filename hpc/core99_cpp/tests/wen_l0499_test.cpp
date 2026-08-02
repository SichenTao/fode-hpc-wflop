/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0499 grids, DM/CVaR, fixed-count and replay tests
Paper/DOI/source/missing/reconstruction/claim:
hpc/core99_cpp/include/core99/wen_l0499.hpp
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/wen_l0499.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<int> spread_layout(const int width) {
    std::vector<int> result;
    for (int index = 0; index < width * width && result.size() < 50U; ++index) {
        result.push_back(index);
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    require(argc == 2, "L0499 test requires fixture path");
    const std::string fixture = argv[1];
    const core99::l0499::Problem case_a(
        "l0499_case_a_so", fixture, 4
    );
    require(
        case_a.semantic_id() == "l0499_case_a_dm_cvar_grid_v1",
        "case-A semantic ID mismatch"
    );
    require(case_a.candidates().size() == 144U, "case-A grid mismatch");
    require(case_a.sector_count() == 5, "case-A sector mismatch");
    require(case_a.turbine_count() == 50, "case-A turbine mismatch");
    require(
        std::abs(
            std::accumulate(
                case_a.wind_mean().begin(),
                case_a.wind_mean().end(),
                0.0
            ) - 1.0
        ) < 1.0e-12,
        "case-A wind mean normalization failed"
    );
    const auto evaluation = case_a.evaluate(spread_layout(12));
    require(evaluation.feasible, "case-A fixed-count layout infeasible");
    require(
        evaluation.expected_aep_mwh > 100000.0
            && evaluation.expected_aep_mwh < 265000.0,
        "case-A AEP is outside physical scale"
    );
    require(
        evaluation.cvar_mwh <= evaluation.expected_aep_mwh,
        "CVaR exceeds expected AEP"
    );
    require(
        std::abs(evaluation.objective - evaluation.cvar_mwh) < 1.0e-9,
        "SO objective is not CVaR"
    );

    const core99::l0499::Problem case_b(
        "l0499_case_b_station_41_ro", fixture, 4
    );
    require(
        case_b.semantic_id()
            == "l0499_case_b_ndawn41_proxy_dm_cvar_grid_v1",
        "case-B semantic ID mismatch"
    );
    require(case_b.candidates().size() == 100U, "case-B grid mismatch");
    require(case_b.sector_count() == 12, "case-B sector mismatch");
    require(case_b.station_index() == 40, "case-B station mismatch");
    require(
        case_b.observed_precomputation_workers() >= 2,
        "case-B precomputation has no multicore evidence"
    );
    const auto case_ids = core99::l0499::paper_case_ids();
    require(case_ids.size() == 126U, "paper case registry mismatch");

    core99::l0499::RunConfig serial;
    serial.seed = 499499;
    serial.workers = 1;
    serial.max_physical_fes = 320;
    const auto first = case_b.optimize(serial);
    auto parallel = serial;
    parallel.workers = 4;
    const auto second = case_b.optimize(parallel);
    require(
        first.physical_fes == 320U && second.physical_fes == 320U,
        "physical-FES accounting mismatch"
    );
    require(
        first.scientific_hash == second.scientific_hash,
        "one/multicore scientific replay mismatch"
    );
    require(
        second.observed_workers >= 2,
        "population evaluation did not activate multicore backend"
    );
    require(
        second.best_evaluation.feasible
            && second.best_evaluation.objective + 1.0e-9
                >= second.initial_best.objective,
        "GA smoke result regressed or became infeasible"
    );
    return 0;
}
