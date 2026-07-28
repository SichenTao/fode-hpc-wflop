#pragma once

#include "fode/case.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace fode {

struct RunConfig {
    std::uint64_t seed = 20260728;
    std::uint64_t physical_fes_budget = 24000;
    int workers = 0;
    bool profile_phases = false;
};

struct RunResult {
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
    bool profiling_enabled = false;
    std::array<double, 17> phase_seconds{};
};

RunResult optimize_fode_hpc(const CaseData& data, const RunConfig& config);

}  // namespace fode
