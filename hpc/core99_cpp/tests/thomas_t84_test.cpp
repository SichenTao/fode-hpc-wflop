/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T84 data, equation, AD, boundary and deterministic-HPC tests
Paper/DOI: Wake Expansion Continuation: Multi-Modality Reduction in the Wind
Farm Layout Optimization Problem; 10.1002/we.2692
Public source, conflicts, reconstruction and claim boundary:
include/core99/thomas_t84.hpp
Semantic IDs: t84_wec_four_case_author_data_v1 and four t84_* method IDs.
Production backend under test: pure C++ CPU-HPC deterministic evaluator,
gradient and optimizer kernels.
Controlling contract: shared/contracts/core99_t84_thomas_2022.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/thomas_t84.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool same_bits(const double left, const double right) {
    return std::memcmp(&left, &right, sizeof(double)) == 0;
}

void gradient_check(
    const core99::t84::Problem& problem,
    const core99::t84::WakeModel wake,
    const int turbine
) {
    fode::PersistentExecutor executor(4);
    core99::t84::EvaluationSettings settings;
    settings.wake_model = wake;
    settings.calculate_gradient = true;
    const auto analytical = problem.evaluate(problem.start(0), settings, executor);
    const int variables = 2 * problem.turbine_count();
    require(
        analytical.gradient_gwh_per_m.size() == static_cast<std::size_t>(variables),
        "T84 gradient dimension mismatch"
    );
    constexpr double step = 1.0e-3;
    auto plus = problem.start(0);
    auto minus = problem.start(0);
    plus[static_cast<std::size_t>(turbine)].x_m += step;
    minus[static_cast<std::size_t>(turbine)].x_m -= step;
    settings.calculate_gradient = false;
    const double plus_value = problem.evaluate(plus, settings, executor).aep_gwh;
    const double minus_value = problem.evaluate(minus, settings, executor).aep_gwh;
    const double finite_difference = (plus_value - minus_value) / (2.0 * step);
    const double exact = analytical.gradient_gwh_per_m[
        static_cast<std::size_t>(turbine)
    ];
    require(
        std::abs(finite_difference - exact)
            < 5.0e-4 * std::max(1.0, std::abs(finite_difference)),
        "T84 exact gradient disagrees with central finite difference"
    );
}

}  // namespace

int main(int argc, char** argv) {
    require(argc == 2, "expected one T84 data path");
    const std::vector<int> turbines = {16, 38, 38, 60};
    const std::vector<int> wind_states = {20, 12, 36, 72};
    for (int case_id = 1; case_id <= 4; ++case_id) {
        const core99::t84::Problem problem(argv[1], case_id);
        require(
            problem.semantic_id() == "t84_wec_four_case_author_data_v1",
            "T84 problem semantic ID mismatch"
        );
        require(problem.turbine_count() == turbines[case_id - 1],
            "T84 turbine count mismatch");
        require(problem.wind_state_count() == wind_states[case_id - 1],
            "T84 wind-state count mismatch");
        require(problem.start(0).size() == static_cast<std::size_t>(turbines[case_id - 1]),
            "T84 planned layout size mismatch");
        require(problem.start(199).size() == problem.start(0).size(),
            "T84 final public start missing");
        require(problem.maximum_constraint_violation(problem.start(0)) < 1.0e-9,
            "T84 planned layout violates final paper constraints");
    }

    const core99::t84::Problem case1(argv[1], 1);
    fode::PersistentExecutor one_worker(1);
    fode::PersistentExecutor all_workers(20);
    core99::t84::EvaluationSettings bastankhah;
    bastankhah.wake_model = core99::t84::WakeModel::bastankhah;
    bastankhah.turbulence_mode = core99::t84::TurbulenceMode::hard_local;
    bastankhah.calculate_gradient = true;
    const auto serial = case1.evaluate(case1.start(0), bastankhah, one_worker);
    const auto parallel = case1.evaluate(case1.start(0), bastankhah, all_workers);
    require(same_bits(serial.aep_gwh, parallel.aep_gwh),
        "T84 one/all-worker Bastankhah AEP differs");
    require(serial.gradient_gwh_per_m == parallel.gradient_gwh_per_m,
        "T84 one/all-worker Bastankhah gradient differs");
    require(parallel.observed_workers > 1,
        "T84 multicore wind-state participation not observed");
    require(serial.wake_loss_percent > 0.0 && serial.wake_loss_percent < 100.0,
        "T84 Bastankhah wake loss is invalid");

    core99::t84::EvaluationSettings expanded = bastankhah;
    expanded.calculate_gradient = false;
    expanded.turbulence_mode = core99::t84::TurbulenceMode::ambient_only;
    expanded.wec_factor = 3.0;
    core99::t84::EvaluationSettings physical = expanded;
    physical.wec_factor = 1.0;
    const auto expanded_value = case1.evaluate(case1.start(0), expanded, all_workers);
    const auto physical_value = case1.evaluate(case1.start(0), physical, all_workers);
    require(expanded_value.aep_gwh < physical_value.aep_gwh,
        "T84 WEC factor does not alter the multimodal search landscape");

    gradient_check(case1, core99::t84::WakeModel::bastankhah, 5);
    const core99::t84::Problem case2(argv[1], 2);
    gradient_check(case2, core99::t84::WakeModel::jensen_cosine, 7);

    const core99::t84::Problem case4(argv[1], 4);
    const auto coordinates = [&]() {
        std::vector<double> values(120U);
        for (int turbine = 0; turbine < 60; ++turbine) {
            values[static_cast<std::size_t>(turbine)] =
                case4.start(0)[static_cast<std::size_t>(turbine)].x_m;
            values[static_cast<std::size_t>(60 + turbine)] =
                case4.start(0)[static_cast<std::size_t>(turbine)].y_m;
        }
        return values;
    }();
    std::vector<double> constraints;
    std::vector<double> jacobian;
    case4.normalized_constraints(coordinates, constraints, &jacobian);
    require(static_cast<int>(constraints.size()) == case4.constraint_count(),
        "T84 Amalia constraint count mismatch");
    require(jacobian.size() == constraints.size() * coordinates.size(),
        "T84 Amalia Jacobian size mismatch");

    std::cout << "T84 semantic and HPC tests passed\n";
    return 0;
}
