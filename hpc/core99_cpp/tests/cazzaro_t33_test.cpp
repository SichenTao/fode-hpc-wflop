/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T33 paper-case, objective, routing, feasibility and
deterministic parallel regression tests
Paper/DOI: Combined Layout and Cable Optimization of Offshore Wind Farms;
10.1016/j.ejor.2023.04.046
Public source: official dataset DOI 10.11583/DTU.13134731; no target code.
Missing/conflict/reconstruction completion, semantic IDs, HPC facts, and
claim boundary:
include/core99/cazzaro_t33.hpp.
Independent oracle: scripts/validate_core99_t33.py.
Contract: shared/contracts/core99_t33_cazzaro_combined_2023.json.
Claim boundary: declared academic reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/cazzaro_t33.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

int main(const int argc, char** argv) {
    assert(argc == 2);
    const std::filesystem::path root = argv[1];
    using core99::t33::Density;
    using core99::t33::Problem;
    const std::array<int, 10> available{
        3196, 6974, 7090, 10398, 11478,
        11536, 14602, 19458, 20211, 21634,
    };
    const std::array<int, 10> fixed{
        42, 15, 8, 45, 40, 12, 35, 40, 36, 75,
    };
    const std::array<int, 10> low{
        20, 49, 40, 85, 68, 79, 70, 94, 156, 117,
    };
    const std::array<int, 10> high{
        40, 99, 90, 170, 137, 158, 140, 188, 313, 235,
    };
    for (int site = 0; site < 10; ++site) {
        for (const Density density : {Density::low, Density::high}) {
            Problem problem(
                root,
                static_cast<char>('A' + site),
                density,
                4
            );
            assert(problem.info().available_positions == available[site]);
            assert(problem.info().fixed_turbines == fixed[site]);
            assert(
                problem.info().turbines
                == (density == Density::low ? low[site] : high[site])
            );
            assert(
                std::accumulate(
                    problem.info().zone_quotas.begin(),
                    problem.info().zone_quotas.end(),
                    0
                ) == problem.info().turbines
            );
            assert(problem.info().wind_states == 177);
            assert(
                problem.paper_fixed_cycles()
                == (density == Density::low ? 860U : 2064U)
            );
            assert(
                problem.paper_time_limit_seconds()
                == (density == Density::low ? 36000.0 : 86400.0)
            );
        }
    }

    Problem problem(root, 'A', Density::low, 4);
    const auto reference = problem.deterministic_reference_layout();
    assert(reference.size() == 20U);
    const auto evaluation = problem.evaluate_direct(reference, 4);
    assert(evaluation.feasible);
    assert(evaluation.spacing_violation_m == 0.0);
    assert(evaluation.cable_crossings == 0);
    assert(evaluation.aep_mwh > 1.0e6);
    assert(evaluation.foundation_cost_eur > 0.0);
    assert(evaluation.cable_cost_eur > 0.0);
    assert(std::abs(
        evaluation.lifetime_revenue_eur
        - 450.0 * evaluation.aep_mwh
    ) < 1.0e-6);
    assert(std::abs(
        evaluation.npv_eur
        - (
            evaluation.lifetime_revenue_eur
            - evaluation.foundation_cost_eur
            - evaluation.cable_cost_eur
        )
    ) < 1.0e-6);

    const auto cache =
        std::filesystem::temp_directory_path()
        / "core99_t33_cpp_test_a.pair";
    std::filesystem::remove(cache);
    core99::t33::RunConfig config;
    config.seed = 330046;
    config.workers = 4;
    config.fixed_vns_cycles = 1;
    config.matrix_cache = cache;
    const auto parallel = core99::t33::run(problem, config);
    config.workers = 1;
    const auto serial = core99::t33::run(problem, config);
    std::filesystem::remove(cache);
    assert(parallel.observed_workers >= 2);
    assert(serial.observed_workers == 1);
    assert(parallel.completed_vns_cycles == 1U);
    assert(serial.completed_vns_cycles == 1U);
    assert(parallel.scientific_hash == serial.scientific_hash);
    assert(parallel.best.feasible);
    assert(serial.best.feasible);
    assert(parallel.best.npv_eur == serial.best.npv_eur);
    assert(
        parallel.best.npv_eur + 1.0e-6
        >= parallel.initial.npv_eur
    );
    std::cout << "T33 C++ reconstruction checks passed\n";
    return EXIT_SUCCESS;
}
