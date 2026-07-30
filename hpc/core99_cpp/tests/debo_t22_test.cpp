/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T22 C++ semantic and deterministic-parallel tests
Paper DOI: 10.5194/wes-8-865-2023
Public source: paper-linked archive DOI 10.5281/zenodo.7125349
Missing information and reconstruction decisions:
include/core99/debo_t22.hpp
Method/problem semantic IDs: t22_debo_paper_reconstruction_v1;
t22_iea37_cs4_gaussian_aep_v1
Controlling contract: shared/contracts/core99_t22_iea37_cs4.json
Claim boundary: semantic and deterministic test, not author reproduction claim
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/debo_t22.hpp"

#include "fode/executor.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

int main() {
    const core99::t22::Problem problem;
    if (problem.id() != "t22_iea37_cs4"
        || problem.turbine_count() != 81) {
        std::cerr << "T22 identity mismatch\n";
        return EXIT_FAILURE;
    }
    if (!problem.inside({9361.5, 126.9})
        || problem.inside({-1000.0, -1000.0})) {
        std::cerr << "T22 boundary predicate mismatch\n";
        return EXIT_FAILURE;
    }
    const auto base = problem.author_base_layout();
    const auto debo = problem.author_debo_layout();
    if (base.size() != 81 || debo.size() != 81) {
        std::cerr << "T22 author layout size mismatch\n";
        return EXIT_FAILURE;
    }
    fode::PersistentExecutor serial(1);
    fode::PersistentExecutor parallel(4);
    const auto serial_value = problem.evaluate(debo, serial);
    const auto parallel_value = problem.evaluate(debo, parallel);
    if (
        std::abs(serial_value.aep_mwh - parallel_value.aep_mwh)
            > 1.0e-8
        || std::abs(
            serial_value.constraint_violation_m
            - parallel_value.constraint_violation_m
        ) > 1.0e-10
        || serial_value.constraint_violation_m > 1.0e-4
    ) {
        std::cerr << "T22 deterministic evaluation mismatch\n";
        return EXIT_FAILURE;
    }
    if (!(serial_value.aep_mwh > 2.0e6)
        || !(serial_value.wake_loss_fraction > 0.0)
        || !(serial_value.wake_loss_fraction < 0.5)) {
        std::cerr << "T22 evaluation magnitude mismatch\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
