#include "fode/case.hpp"
#include "fode/executor.hpp"
#include "wflop/taae_problem.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

bool same_bits(double lhs, double rhs) {
    return std::bit_cast<std::uint64_t>(lhs)
        == std::bit_cast<std::uint64_t>(rhs);
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void compare_science(
    const wflop::taae::CompleteEvaluation& lhs,
    const wflop::taae::CompleteEvaluation& rhs
) {
    require(
        same_bits(
            lhs.reciprocal_expected_power_per_kw,
            rhs.reciprocal_expected_power_per_kw
        ),
        "worker-count reciprocal-power mismatch"
    );
    require(
        same_bits(lhs.expected_power_kw, rhs.expected_power_kw),
        "worker-count expected-power mismatch"
    );
    require(
        same_bits(
            lhs.average_a_weighted_noise_dba,
            rhs.average_a_weighted_noise_dba
        ),
        "worker-count acoustic-objective mismatch"
    );
    require(
        same_bits(lhs.total_cost_units, rhs.total_cost_units),
        "worker-count total-cost mismatch"
    );
    require(
        same_bits(
            lhs.normalized_constraint_violation,
            rhs.normalized_constraint_violation
        ),
        "worker-count constraint-violation mismatch"
    );
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::invalid_argument(
                "usage: taae_problem_test PROXY_CASES"
            );
        }
        wflop::taae::check_formula_fixture();

        const std::vector<fode::CaseData> cases = fode::load_cases(argv[1]);
        require(cases.size() == 6U, "expected six proxy cases");

        const std::vector<int> layouts{
            2, 5, 8, 11, 14, 42, 65, 88, 111, 134, 157, 220, 263, 306, 349,
            3, 26, 49, 72, 95, 118, 141, 164, 187, 230, 253, 276, 299, 322, 345
        };

        fode::PersistentExecutor serial_executor(1);
        fode::PersistentExecutor parallel_executor(20);
        for (const fode::CaseData& data : cases) {
            const auto serial = wflop::taae::evaluate_structured_proxy(
                layouts, 2, data, serial_executor
            );
            const auto parallel = wflop::taae::evaluate_structured_proxy(
                layouts, 2, data, parallel_executor
            );
            require(
                serial.problem_manifest_hash
                    == wflop::taae::structured_proxy_manifest_hash(data),
                "reported manifest hash mismatch"
            );
            require(
                serial.problem_manifest_hash == parallel.problem_manifest_hash,
                "worker-count manifest mismatch"
            );
            require(
                serial.complete_layout_evaluations == 2U
                    && parallel.complete_layout_evaluations == 2U,
                "physical FES is not one complete layout evaluation"
            );
            require(
                serial.requested_workers == 1
                    && serial.observed_workers == 1
                    && parallel.requested_workers == 20
                    && parallel.observed_workers == 20,
                "worker accounting mismatch"
            );
            for (std::size_t i = 0; i < serial.values.size(); ++i) {
                compare_science(serial.values[i], parallel.values[i]);
                const auto& value = serial.values[i];
                require(
                    std::isfinite(value.reciprocal_expected_power_per_kw)
                        && value.expected_power_kw > 0.0
                        && std::isfinite(value.average_a_weighted_noise_dba)
                        && value.total_cost_units > 0.0
                        && value.normalized_constraint_violation >= 0.0,
                    "non-finite or physically invalid complete evaluation"
                );
                require(
                    std::abs(
                        value.reciprocal_expected_power_per_kw
                        - 1.0 / value.expected_power_kw
                    ) <= 1.0e-18,
                    "reciprocal-power objective contract mismatch"
                );
            }
        }

        const auto budget600 = wflop::taae::evaluate_structured_proxy(
            layouts, 2, cases.at(0), serial_executor
        );
        const auto budget800 = wflop::taae::evaluate_structured_proxy(
            layouts, 2, cases.at(1), serial_executor
        );
        const auto budget1000 = wflop::taae::evaluate_structured_proxy(
            layouts, 2, cases.at(2), serial_executor
        );
        for (std::size_t i = 0; i < 2U; ++i) {
            require(
                budget600.values[i].normalized_constraint_violation
                    >= budget800.values[i].normalized_constraint_violation
                    && budget800.values[i].normalized_constraint_violation
                    >= budget1000.values[i].normalized_constraint_violation,
                "budget violation is not monotone"
            );
        }

        bool wrong_manifest_rejected = false;
        try {
            fode::CaseData common = cases.front();
            common.velocity.front() += 0.125;
            (void)wflop::taae::evaluate_structured_proxy(
                layouts, 2, common, serial_executor
            );
        } catch (const std::invalid_argument& error) {
            wrong_manifest_rejected =
                std::string(error.what()).find("manifest")
                != std::string::npos;
        }
        require(wrong_manifest_rejected, "wrong manifest was not rejected");

        bool duplicate_rejected = false;
        try {
            std::vector<int> invalid = layouts;
            invalid[1] = invalid[0];
            (void)wflop::taae::evaluate_structured_proxy(
                invalid, 2, cases.front(), serial_executor
            );
        } catch (const std::invalid_argument&) {
            duplicate_rejected = true;
        }
        require(duplicate_rejected, "duplicate turbine cell was not rejected");

        bool monitor_rejected = false;
        try {
            std::vector<int> invalid = layouts;
            invalid[0] = 10;
            (void)wflop::taae::evaluate_structured_proxy(
                invalid, 2, cases.front(), serial_executor
            );
        } catch (const std::invalid_argument&) {
            monitor_rejected = true;
        }
        require(monitor_rejected, "monitor cell was not rejected");

        const auto& oracle = budget800.values.front();
        require(
            same_bits(oracle.expected_power_kw, 3288.41053054854)
                && same_bits(
                    oracle.reciprocal_expected_power_per_kw,
                    0.00030409828417414476
                )
                && same_bits(
                    oracle.average_a_weighted_noise_dba,
                    40.999881463426384
                )
                && same_bits(oracle.total_cost_units, 460052.10252473498)
                && same_bits(oracle.normalized_constraint_violation, 0.0),
            "fixed-layout complete-evaluation oracle mismatch"
        );
        std::cout << std::setprecision(17)
            << "taae_problem_fixture=pass"
            << " power_kw=" << oracle.expected_power_kw
            << " reciprocal_power=" << oracle.reciprocal_expected_power_per_kw
            << " noise_dba=" << oracle.average_a_weighted_noise_dba
            << " cost=" << oracle.total_cost_units
            << " violation=" << oracle.normalized_constraint_violation
            << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "taae_problem_fixture=fail error="
                  << error.what() << '\n';
        return 1;
    }
}
