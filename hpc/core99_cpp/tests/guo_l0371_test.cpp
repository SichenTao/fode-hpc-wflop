/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0371 grids, stability direction, equation, replay and
physical-FES C++ tests
Paper/DOI/source/missing/reconstruction/claim:
hpc/core99_cpp/include/core99/guo_l0371.hpp
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/guo_l0371.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<int> ideal_three_columns() {
    std::vector<int> result;
    for (const int x : {0, 4, 9}) {
        for (int y = 0; y < 10; ++y) result.push_back(y * 10 + x);
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    require(argc == 2, "L0371 test requires fixture path");
    const std::string fixture = argv[1];
    const core99::l0371::Problem ideal(
        "l0371_ideal_a_n", fixture, 4
    );
    require(
        ideal.semantic_id() == "l0371_ideal_ietgwm_grid_v1",
        "ideal semantic ID mismatch"
    );
    require(ideal.candidates().size() == 100U, "ideal grid mismatch");
    require(ideal.turbine_count() == 30, "ideal turbine count mismatch");
    require(ideal.states().size() == 1U, "ideal case-a state mismatch");
    const auto layout = ideal_three_columns();
    const auto evaluation = ideal.evaluate(layout);
    require(evaluation.feasible, "paper-scale three-column layout infeasible");
    require(
        evaluation.average_power_kw > 13000.0
            && evaluation.average_power_kw < 15552.0,
        "ideal neutral power is outside paper scale"
    );
    require(
        evaluation.efficiency > 0.8 && evaluation.efficiency < 1.0,
        "ideal efficiency bounds failed"
    );
    auto duplicate = layout;
    duplicate[1] = duplicate[0];
    require(
        !ideal.evaluate(duplicate).feasible,
        "duplicate candidate was not rejected"
    );

    const core99::l0371::Problem case_c(
        "l0371_ideal_c_vs", fixture, 4
    );
    require(case_c.states().size() == 108U, "case-c states mismatch");
    double case_c_sum = 0.0;
    for (const auto& state : case_c.states()) {
        case_c_sum += state.probability;
    }
    require(
        std::abs(case_c_sum - 1.0) < 1.0e-6,
        "case-c probability mismatch"
    );

    const core99::l0371::Problem horns(
        "l0371_horns_actual", fixture, 4
    );
    require(
        horns.semantic_id() == "l0371_horns_ietgwm_grid_proxy_v1",
        "Horns semantic ID mismatch"
    );
    require(horns.candidates().size() == 531U, "Horns grid mismatch");
    require(horns.turbine_count() == 80, "Horns turbine count mismatch");
    require(horns.states().size() == 504U, "Horns actual states mismatch");
    require(
        horns.observed_precomputation_workers() >= 2,
        "Horns precomputation has no multicore evidence"
    );

    core99::l0371::RunConfig serial;
    serial.seed = 371371;
    serial.workers = 1;
    serial.max_physical_fes = 300;
    const auto first = case_c.optimize(serial);
    auto parallel = serial;
    parallel.workers = 4;
    const auto second = case_c.optimize(parallel);
    require(
        first.physical_fes == 300U && second.physical_fes == 300U,
        "physical-FES accounting mismatch"
    );
    require(
        first.scientific_hash == second.scientific_hash,
        "serial/multicore scientific replay mismatch"
    );
    require(
        second.best_evaluation.feasible
            && second.best_evaluation.average_power_kw
                + 1.0e-9
                >= second.initial_evaluation.average_power_kw,
        "DEEM smoke result regressed or became infeasible"
    );
    return 0;
}
