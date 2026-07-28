#pragma once

#include "fode/case.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace wflop {

struct RunConfig {
    std::string algorithm_id;
    std::uint64_t seed = 20260728;
    std::uint64_t physical_fes_budget = 24000;
    int workers = 20;
    std::string sugga_model_root = "shared/models/sugga_cpp";
};

struct RunResult {
    std::string algorithm_id;
    std::string method_id;
    std::string algorithm_provenance;
    std::string effective_semantics_id;
    std::string case_id;
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    std::uint64_t generations = 0;
    int initial_population = 0;
    int final_population = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double best_expected_power_kw = 0.0;
    std::vector<int> best_layout_1based;
    double total_seconds = 0.0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    std::string pso_update_semantics;
};

const std::vector<std::string>& algorithm_ids();
RunResult optimize(const fode::CaseData& data, const RunConfig& config);

}  // namespace wflop
