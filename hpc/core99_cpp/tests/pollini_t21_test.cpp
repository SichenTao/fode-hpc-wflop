/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T21 C++ structural, determinism, and gradient tests
Paper/DOI: 10.1016/j.renene.2022.06.019
Public source: https://github.com/byuflowlab/iea37-wflo-casestudies
revision af88908d22795030ac2dfbe37bc38e912aee8ed6
Missing/conflicts/completion:
hpc/core99_cpp/include/core99/pollini_t21.hpp
Method/problem semantic IDs: t21_ramp_mma_declared_reconstruction_v1;
t21_pollini_two_circle_density_wflop_v1
Controlling contract: shared/contracts/core99_t21_pollini_2022.json
Test boundary: structural and numerical derivative checks; the independent
paper-equation oracle is scripts/validate_core99_t21.py
Claim boundary: these tests establish implementation structure, deterministic
parallel equivalence, and gradient correctness; they do not establish exact
author-code or author-random-state replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/pollini_t21.hpp"

#include "fode/executor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool close(double left, double right, double relative = 1.0e-11) {
    return std::abs(left - right)
        <= relative * std::max({1.0, std::abs(left), std::abs(right)});
}

}  // namespace

int main() {
    try {
        const core99::t21::Problem small(
            core99::t21::CaseId::radius_1300,
            4
        );
        const core99::t21::Problem large(
            core99::t21::CaseId::radius_3000,
            4
        );
        require(small.potential_sites() == 124, "small grid cardinality");
        require(large.potential_sites() == 709, "large grid cardinality");
        require(small.minimum_turbines() == 16, "small minimum count");
        require(small.maximum_turbines() == 64, "small maximum count");
        require(large.minimum_turbines() == 64, "large minimum count");
        require(large.maximum_turbines() == 256, "large maximum count");
        require(!small.spacing_pairs().empty(), "small spacing pairs");
        require(!large.spacing_pairs().empty(), "large spacing pairs");
        for (const auto& point : small.grid()) {
            require(
                point.x_m * point.x_m + point.y_m * point.y_m
                    <= 1300.0 * 1300.0 + 1.0e-9,
                "small point outside circle"
            );
        }
        for (const auto& point : large.grid()) {
            require(
                point.x_m * point.x_m + point.y_m * point.y_m
                    <= 3000.0 * 3000.0 + 1.0e-9,
                "large point outside circle"
            );
        }

        std::vector<double> densities(
            static_cast<std::size_t>(small.potential_sites()),
            0.2
        );
        for (std::size_t index = 0; index < densities.size(); ++index) {
            densities[index] += 0.03
                * std::sin(static_cast<double>(index + 1));
        }
        fode::PersistentExecutor serial(1);
        fode::PersistentExecutor parallel(4);
        const auto serial_value =
            small.evaluate(densities, 1.25, serial, true);
        const auto parallel_value =
            small.evaluate(densities, 1.25, parallel, true);
        require(
            close(serial_value.aep_gwh, parallel_value.aep_gwh),
            "parallel AEP drift"
        );
        require(
            serial_value.objective_gradient.size() == densities.size(),
            "gradient cardinality"
        );
        for (std::size_t index = 0; index < densities.size(); ++index) {
            require(
                close(
                    serial_value.objective_gradient[index],
                    parallel_value.objective_gradient[index],
                    3.0e-11
                ),
                "parallel gradient drift"
            );
        }

        constexpr double step = 1.0e-6;
        for (const std::size_t variable : {0U, 17U, 61U, 123U}) {
            std::vector<double> plus = densities;
            std::vector<double> minus = densities;
            plus[variable] += step;
            minus[variable] -= step;
            const double finite_difference = (
                small.evaluate(plus, 1.25, serial, false).objective
                - small.evaluate(minus, 1.25, serial, false).objective
            ) / (2.0 * step);
            require(
                close(
                    finite_difference,
                    serial_value.objective_gradient[variable],
                    2.0e-6
                ),
                "analytical objective gradient mismatch"
            );
        }

        core99::t21::RunConfig smoke;
        smoke.seed = 20260731;
        smoke.start_index = 0;
        smoke.workers = 4;
        smoke.maximum_objective_evaluations = 12;
        const auto run = core99::t21::run(small, smoke);
        require(run.objective_evaluations > 0, "MMA made no progress");
        require(
            run.objective_evaluations <= smoke.maximum_objective_evaluations,
            "MMA exceeded physical objective budget"
        );
        require(std::isfinite(run.relaxed_aep_gwh), "nonfinite relaxed AEP");
        require(std::isfinite(run.discrete_aep_gwh), "nonfinite discrete AEP");
        require(run.densities.size() == densities.size(), "run density size");
        require(run.observed_workers > 0, "no worker participation recorded");
        std::cout << "T21 C++ structural and gradient checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T21 test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
