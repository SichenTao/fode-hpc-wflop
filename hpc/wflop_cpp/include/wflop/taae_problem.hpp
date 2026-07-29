#pragma once

#include "fode/case.hpp"
#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace wflop::taae {

struct CompleteEvaluation {
    double reciprocal_expected_power_per_kw = 0.0;
    double expected_power_kw = 0.0;
    double average_a_weighted_noise_dba = 0.0;
    double total_cost_units = 0.0;
    double normalized_constraint_violation = 0.0;
};

struct BatchEvaluation {
    std::vector<CompleteEvaluation> values;
    std::uint64_t complete_layout_evaluations = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double elapsed_seconds = 0.0;
    std::string problem_manifest_hash;
};

BatchEvaluation evaluate_structured_proxy(
    const std::vector<int>& layouts_1based,
    int batch_size,
    const fode::CaseData& data,
    fode::PersistentExecutor& executor
);

std::string structured_proxy_manifest_hash(const fode::CaseData& data);
void check_formula_fixture();

}  // namespace wflop::taae
