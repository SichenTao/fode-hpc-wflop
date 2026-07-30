/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T16 equation, AD-gradient, constraint and replay tests
Paper/DOI: Comparison of Wind Farm Layout Optimization Results Using a
Simple Wake Model and Gradient-Based Optimization to Large Eddy Simulations;
10.2514/6.2019-0538
Public source/missing/reconstruction: include/core99/thomas_t16.hpp
Claim boundary: structural and numerical semantic tests, not author
SNOPT/Tapenade or SOWFA replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/thomas_t16.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main(int argc, char** argv) {
    require(argc == 2, "expected one T16 data path");
    const core99::t16::Problem problem(argv[1]);
    require(
        problem.semantic_id()
            == "t16_nantucket38_author_lineage_reconstructed_v1",
        "problem semantic ID mismatch"
    );
    require(
        problem.baseline_layout().size() == core99::t16::turbine_count,
        "baseline turbine count mismatch"
    );
    require(
        problem.maximum_constraint_violation(
            problem.baseline_layout()
        ) < 1.0e-8,
        "baseline infeasible"
    );
    const double smooth = core99::t16::smooth_max(0.1, 0.1, 700.0);
    require(
        std::abs(smooth - (0.1 + std::log(2.0) / 700.0)) < 1.0e-14,
        "smooth maximum equation mismatch"
    );

    const auto random = problem.reconstructed_start(17, 16017);
    require(
        random.size() == core99::t16::turbine_count,
        "reconstructed start turbine count mismatch"
    );
    require(
        problem.maximum_constraint_violation(random) < 1.0e-10,
        "reconstructed start infeasible"
    );

    fode::PersistentExecutor executor(4);
    core99::t16::EvaluationSettings settings;
    settings.calculate_gradient = true;
    const auto analytical = problem.evaluate(
        problem.baseline_layout(), settings, executor
    );
    require(std::isfinite(analytical.aep_gwh), "baseline AEP is not finite");
    require(
        analytical.aep_gwh > 480.0 && analytical.aep_gwh < 510.0,
        "baseline AEP is inconsistent with paper Table 2"
    );
    require(
        analytical.gradient_gwh_per_m.size() == 76U,
        "objective gradient dimension mismatch"
    );
    require(
        analytical.observed_workers >= 2,
        "multicore evaluator participation not observed"
    );

    constexpr double step = 1.0e-3;
    auto plus = problem.baseline_layout();
    auto minus = problem.baseline_layout();
    plus[0].x_m += step;
    minus[0].x_m -= step;
    settings.calculate_gradient = false;
    const double plus_value = problem.evaluate(
        plus, settings, executor
    ).aep_gwh;
    const double minus_value = problem.evaluate(
        minus, settings, executor
    ).aep_gwh;
    const double finite_difference =
        (plus_value - minus_value) / (2.0 * step);
    const double exact = analytical.gradient_gwh_per_m[0];
    require(
        std::isfinite(finite_difference),
        "finite-difference gradient is not finite"
    );
    require(
        std::abs(finite_difference - exact)
            < 2.0e-4 * std::max(1.0, std::abs(finite_difference)),
        "exact gradient disagrees with central finite difference"
    );

    core99::t16::EvaluationSettings assessment_settings;
    assessment_settings.turbulence_mode =
        core99::t16::TurbulenceMode::hard_local;
    assessment_settings.rotor_sample_points = 100;
    const auto assessment = problem.evaluate(
        problem.baseline_layout(), assessment_settings, executor
    );
    require(
        std::isfinite(assessment.aep_gwh),
        "100-point assessment AEP is not finite"
    );
    require(
        assessment.aep_gwh > 475.0 && assessment.aep_gwh < 490.0,
        "100-point assessment AEP is inconsistent with paper Table 2"
    );
    std::cout << "T16 semantic test passed\n";
    return 0;
}
