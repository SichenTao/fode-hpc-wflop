/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE P3 problem-proxy and P4 formula scientific fixtures
Paper title: Transformer Autoencoder-Assisted Evolutionary Framework for Constrained Multiobjective 3D Wind Farm Layout Optimization
DOI: 10.1109/JAS.2026.126233
Paper SHA-256: 243dd96dfa94a3d596f375a6c62e58015c735171958778d816b6afdbf99cd35b
Public asset/source: no author source, Zhangbei numerical arrays, training corpus, or checkpoint found; evidence dossier docs/source-dossiers/Y36.json
Missing information: original Zhangbei arrays, author model checkpoint, training corpus, and author implementation
Reconstruction: P4 formula fixtures executed over taae_zhangbei_structured_declared_proxy_v1
Fixture scope: checks formula transcription, full semantic hashes, feasibility rejection, physical FES, one-team versus twenty-team bitwise equality, and tolerance-based cross-toolchain golden values
Method evidence tier: not_applicable_test_fixture
Problem evidence tier: P4_FORMULA_FIXTURE over P3_DECLARED_PROXY
Method semantic ID: not_applicable_test_fixture
Problem semantic ID: taae_zhangbei_structured_declared_proxy_v1
Controlling contracts: shared/contracts/taae_formula_fixture_contract.json and shared/contracts/taae_zhangbei_structured_declared_proxy_contract.json
Claim boundary: scientific fixtures only; no author TAAE, Zhangbei numerical-case, performance, or reported-front claim
Last evidence audit date: 2026-07-29
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "fode/case.hpp"
#include "fode/executor.hpp"
#include "wflop/taae_problem.hpp"

#include <bit>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <string_view>
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

bool close_with_tolerance(
    double observed,
    double expected,
    double absolute_tolerance,
    double relative_tolerance
) {
    return std::abs(observed - expected)
        <= absolute_tolerance
            + relative_tolerance * std::abs(expected);
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
        constexpr std::array<std::string_view, 6>
            expected_semantic_hashes{
                "fnv1a64:ffdc187163fb6d97",
                "fnv1a64:d2e0cb41e02ce9c4",
                "fnv1a64:6e9505ccedf0a578",
                "fnv1a64:369f421c373bedec",
                "fnv1a64:25c369241a20016f",
                "fnv1a64:47a7700e18311e81"
            };
        for (std::size_t index = 0; index < cases.size(); ++index) {
            require(
                wflop::taae::structured_proxy_semantic_hash(
                    cases[index]
                ) == expected_semantic_hashes[index],
                "frozen full problem-semantic hash mismatch"
            );
        }

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
                serial.problem_semantic_hash
                    == wflop::taae::structured_proxy_semantic_hash(data),
                "reported full problem-semantic hash mismatch"
            );
            require(
                serial.problem_semantic_hash
                    == parallel.problem_semantic_hash,
                "thread-team problem-semantic hash mismatch"
            );
            require(
                serial.complete_layout_evaluations == 2U
                    && parallel.complete_layout_evaluations == 2U,
                "physical FES is not one complete layout evaluation"
            );
            require(
                serial.configured_workers == 1
                    && parallel.configured_workers == 20,
                "configured thread-team accounting mismatch"
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
        const auto rss_wake = wflop::taae::evaluate_structured_proxy(
            layouts,
            2,
            cases.at(1),
            serial_executor,
            wflop::taae::WakeCombination::root_sum_square
        );
        const auto multiplicative_wake =
            wflop::taae::evaluate_structured_proxy(
                layouts,
                2,
                cases.at(1),
                serial_executor,
                wflop::taae::WakeCombination::multiplicative
            );
        require(
            rss_wake.problem_semantic_hash
                != multiplicative_wake.problem_semantic_hash,
            "wake sensitivity semantic hash did not change"
        );
        require(
            rss_wake.values[0].expected_power_kw
                != multiplicative_wake.values[0].expected_power_kw,
            "wake sensitivity physics did not change"
        );

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

        bool wrong_semantic_hash_rejected = false;
        try {
            fode::CaseData common = cases.front();
            common.velocity.front() += 0.125;
            (void)wflop::taae::evaluate_structured_proxy(
                layouts, 2, common, serial_executor
            );
        } catch (const std::invalid_argument& error) {
            wrong_semantic_hash_rejected =
                std::string(error.what()).find("semantics")
                != std::string::npos;
        }
        require(
            wrong_semantic_hash_rejected,
            "wrong full problem-semantic hash was not rejected"
        );

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
        std::cout << std::setprecision(17)
            << "taae_problem_oracle"
            << " power_kw=" << oracle.expected_power_kw
            << " reciprocal_power=" << oracle.reciprocal_expected_power_per_kw
            << " noise_dba=" << oracle.average_a_weighted_noise_dba
            << " cost=" << oracle.total_cost_units
            << " violation=" << oracle.normalized_constraint_violation
            << '\n';
        require(
            close_with_tolerance(
                    oracle.expected_power_kw,
                    3724.5592512660246,
                    1.0e-9,
                    1.0e-12
                )
                && close_with_tolerance(
                    oracle.reciprocal_expected_power_per_kw,
                    0.00026848814384147264,
                    1.0e-15,
                    1.0e-12
                )
                && close_with_tolerance(
                    oracle.average_a_weighted_noise_dba,
                    41.746465614064014,
                    1.0e-9,
                    1.0e-12
                )
                && close_with_tolerance(
                    oracle.total_cost_units,
                    460052.10252473498,
                    1.0e-7,
                    1.0e-12
                )
                && close_with_tolerance(
                    oracle.normalized_constraint_violation,
                    0.0,
                    1.0e-15,
                    0.0
                ),
            "fixed-layout complete-evaluation oracle mismatch"
        );
        std::cout << "taae_problem_fixture=pass\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "taae_problem_fixture=fail error="
                  << error.what() << '\n';
        return 1;
    }
}
