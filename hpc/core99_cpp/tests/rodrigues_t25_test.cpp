/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T25 problem, exact-gradient, SMAST and CPU-HPC identity
tests
Paper/DOI: Rodrigues et al. 2024; 10.5194/wes-9-321-2024
Fact boundary and controlling contract: include/core99/rodrigues_t25.hpp and
shared/contracts/core99_t25_rodrigues_2024.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core99/rodrigues_t25.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace core99::t25;
    const Problem problem({ProblemFamily::iea37, 16, 36, 1});
    assert(problem.reference_layout().size() == 16U);
    assert(std::abs(problem.boundary_radius_m() - 1300.0) < 1.0e-12);

    const auto layout = problem.random_feasible_layout(25001, 0);
    assert(layout.size() == 16U);
    assert(problem.minimum_spacing(layout) >= 260.0 - 1.0e-9);
    assert(problem.maximum_boundary_violation(layout) <= 1.0e-9);

    fode::PersistentExecutor serial_executor(1);
    const Evaluation serial = problem.evaluate(
        layout, GradientMode::exact_reverse, serial_executor
    );
    fode::PersistentExecutor parallel_executor(4);
    const Evaluation parallel = problem.evaluate(
        layout, GradientMode::exact_reverse, parallel_executor
    );
    assert(serial.aep_gwh > 300.0);
    assert(serial.gradient_gwh_per_m.size() == 32U);
    assert(serial.aep_gwh == parallel.aep_gwh);
    assert(serial.gradient_gwh_per_m == parallel.gradient_gwh_per_m);
    assert(parallel.observed_workers >= 2);

    const Evaluation finite_difference = problem.evaluate(
        layout, GradientMode::central_finite_difference, parallel_executor
    );
    double maximum_absolute_error = 0.0;
    double maximum_reference = 0.0;
    for (std::size_t index = 0; index < serial.gradient_gwh_per_m.size(); ++index) {
        maximum_absolute_error = std::max(
            maximum_absolute_error,
            std::abs(
                serial.gradient_gwh_per_m[index]
                - finite_difference.gradient_gwh_per_m[index]
            )
        );
        maximum_reference = std::max(
            maximum_reference,
            std::abs(finite_difference.gradient_gwh_per_m[index])
        );
    }
    assert(maximum_absolute_error / std::max(1.0e-12, maximum_reference) < 2.0e-5);

    const SmartStartReceipt smart = problem.smart_start(
        0, 3.0, 25002, 0, parallel_executor
    );
    assert(smart.layout.size() == 16U);
    assert(smart.minimum_spacing_m >= 260.0 - 1.0e-9);
    assert(smart.grid_points_initial > 100);
    assert(smart.aep_gwh > 300.0);
    assert(smart.observed_workers >= 2);

    for (const int count : {100, 200, 300, 400, 500}) {
        const Problem horns({ProblemFamily::horns_rev, count, 12, 1});
        assert(static_cast<int>(horns.reference_layout().size()) == count);
        assert(horns.maximum_boundary_violation(horns.reference_layout()) <= 1.0e-9);
    }

    // Exercise the V80 effective-speed -> CT -> downstream-wake reverse path,
    // which is absent from the constant-CT IEA case. A slight deterministic
    // perturbation avoids testing a derivative exactly on Horns Rev symmetry
    // axes while remaining well inside the source-defined padded boundary.
    const Problem horns_gradient_problem({ProblemFamily::horns_rev, 100, 37, 7});
    auto horns_layout = horns_gradient_problem.reference_layout();
    for (std::size_t index = 0; index < horns_layout.size(); ++index) {
        horns_layout[index].x_m += .01 * static_cast<double>(index % 11U);
        horns_layout[index].y_m += .013 * static_cast<double>(index % 13U);
    }
    const Evaluation horns_exact = horns_gradient_problem.evaluate(
        horns_layout, GradientMode::exact_reverse, parallel_executor
    );
    const Evaluation horns_fd = horns_gradient_problem.evaluate(
        horns_layout, GradientMode::central_finite_difference, parallel_executor
    );
    double horns_maximum_error = 0.0;
    double horns_maximum_reference = 0.0;
    for (std::size_t index = 0;
         index < horns_exact.gradient_gwh_per_m.size();
         ++index) {
        horns_maximum_error = std::max(
            horns_maximum_error,
            std::abs(
                horns_exact.gradient_gwh_per_m[index]
                - horns_fd.gradient_gwh_per_m[index]
            )
        );
        horns_maximum_reference = std::max(
            horns_maximum_reference,
            std::abs(horns_fd.gradient_gwh_per_m[index])
        );
    }
    assert(
        horns_maximum_error / std::max(1.0e-12, horns_maximum_reference)
        < 2.0e-4
    );

    OptimizationConfig config;
    config.workers = 4;
    config.seed = 25003;
    config.maximum_evaluations = 2;
    config.grid_resolution_rotor_radii = 3.0;
    const OptimizationReceipt optimized = optimize(problem, config);
    assert(optimized.turbine_count == 16);
    assert(optimized.physical_layout_evaluations >= 2U);
    assert(optimized.final_aep_gwh > 300.0);
    assert(optimized.maximum_boundary_violation_m <= 1.0e-3);
    assert(optimized.observed_workers >= 2);

    std::cout << "T25 C++ flexible-reproduction tests passed\n";
    return 0;
}
