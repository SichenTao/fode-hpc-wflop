/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T87 fixture, equation, constraint, IGA-PSO accounting,
multicore and replay tests
Paper/DOI/source/missing/reconstruction/claim:
hpc/core99_cpp/include/core99/hu_t87.hpp
Controlling contract: shared/contracts/core99_t87_hu_iga_pso_2024.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/hu_t87.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<int> greedy_spaced_layout(
    const std::vector<core99::t87::Candidate>& candidates
) {
    std::vector<int> selected;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        bool feasible = true;
        for (const int other : selected) {
            if (
                std::hypot(
                    candidates[index].x_d
                        - candidates[static_cast<std::size_t>(other)].x_d,
                    candidates[index].y_d
                        - candidates[static_cast<std::size_t>(other)].y_d
                ) < 4.0
            ) {
                feasible = false;
                break;
            }
        }
        if (feasible) selected.push_back(static_cast<int>(index));
    }
    return selected;
}

}  // namespace

int main(int argc, char** argv) {
    require(argc == 2, "T87 test requires proxy path");
    const core99::t87::Problem problem(
        "t87_case1_jensen_aep", argv[1], 4
    );
    require(
        problem.semantic_id() == "t87_qianjiang_figure_proxy_v1",
        "T87 problem semantic ID mismatch"
    );
    require(problem.candidates().size() == 522U, "candidate count mismatch");
    require(problem.wind_states().size() == 49U, "wind-state count mismatch");
    require(
        problem.turbine_curve_point_count() == 38,
        "turbine-curve count mismatch"
    );
    require(
        std::abs(problem.wind_probability_sum() - 1.0) < 1.0e-6,
        "wind probability sum mismatch"
    );
    double minimum_aeh = 1.0e9;
    double maximum_aeh = 0.0;
    for (const auto& candidate : problem.candidates()) {
        minimum_aeh = std::min(minimum_aeh, candidate.aeh_h);
        maximum_aeh = std::max(maximum_aeh, candidate.aeh_h);
        require(
            candidate.speed_multiplier > 0.0,
            "candidate speed calibration failed"
        );
    }
    require(
        std::abs(minimum_aeh - 2000.0) < 1.0e-5
            && std::abs(maximum_aeh - 2500.0) < 1.0e-5,
        "AEH proxy anchors mismatch"
    );
    const auto single = problem.evaluate_candidate_indices({0});
    require(
        std::abs(
            single.aep_mwh / 3.3 - problem.candidates().front().aeh_h
        ) < 1.0e-5,
        "candidate AEH-to-speed calibration mismatch"
    );

    const auto layout = greedy_spaced_layout(problem.candidates());
    require(
        layout.size() >= 15U && layout.size() <= 25U,
        "4D candidate packing is inconsistent with paper scale"
    );
    const auto evaluation = problem.evaluate_candidate_indices(layout);
    require(evaluation.feasible, "greedy oracle layout is infeasible");
    require(
        evaluation.aep_mwh > 0.0
            && evaluation.wake_efficiency > 0.0
            && evaluation.wake_efficiency <= 1.0,
        "T87 physical bounds failed"
    );
    auto duplicate = layout;
    duplicate.insert(duplicate.begin() + 1, duplicate.front());
    require(
        !problem.evaluate_candidate_indices(duplicate).feasible,
        "duplicate/spacing violation was not detected"
    );

    core99::t87::RunConfig serial;
    serial.seed = 870087;
    serial.workers = 1;
    serial.iga_population = 50;
    serial.iga_generations = 3;
    serial.pso_population = 20;
    serial.pso_iterations = 2;
    const auto first = core99::t87::run(problem, serial);
    auto parallel = serial;
    parallel.workers = 4;
    const auto second = core99::t87::run(problem, parallel);
    require(
        first.proposed_fes == 260U && second.proposed_fes == 260U,
        "T87 proposed FES mismatch"
    );
    require(
        first.physical_unique_fes <= first.proposed_fes
            && second.physical_unique_fes <= second.proposed_fes,
        "unique evaluation accounting mismatch"
    );
    require(
        first.scientific_hash == second.scientific_hash,
        "one-worker/multicore scientific replay mismatch"
    );
    require(second.observed_workers >= 2, "no multicore execution evidence");
    require(
        second.best_grid_evaluation.feasible
            && second.best_continuous_evaluation.feasible,
        "IGA-PSO smoke run did not preserve feasibility"
    );
    require(
        second.best_continuous_evaluation.fitness + 1.0e-8
            >= second.best_grid_evaluation.fitness,
        "PSO did not retain the IGA starting point"
    );
    return 0;
}
