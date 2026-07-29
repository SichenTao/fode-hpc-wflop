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
    std::uint64_t maximum_generations = 0;
};

class FractionalOrderController {
public:
    virtual ~FractionalOrderController() = default;

    // Called once before each complete FODE generation. The controller may
    // update its own state and returns the fractional order used by that
    // generation. best_expected_power_kw includes the evaluated initial
    // population and all completed search evaluations.
    virtual double begin_generation(
        std::uint64_t generation,
        double best_expected_power_kw
    ) = 0;

    // Exact-work extension used by controllers whose schedule depends on
    // completed physical evaluations. Existing controllers inherit the
    // legacy behavior through this default forwarding implementation.
    virtual double begin_generation_with_fes(
        std::uint64_t generation,
        double best_expected_power_kw,
        std::uint64_t physical_fes_completed_before_generation
    ) {
        (void)physical_fes_completed_before_generation;
        return begin_generation(generation, best_expected_power_kw);
    }

    // Called after a complete generation, including the inherited local
    // search. Formal FQFODE does not use this hook; its declared offline
    // training environment uses it to observe the action consequence.
    virtual void end_generation(
        std::uint64_t generation,
        double best_expected_power_kw
    ) {
        (void)generation;
        (void)best_expected_power_kw;
    }

    // Called once after the last complete generation so a controller can
    // consume the final transition without adding a physical evaluation.
    virtual void finish(double best_expected_power_kw) = 0;
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
RunResult optimize_fode_hpc_controlled(
    const CaseData& data,
    const RunConfig& config,
    FractionalOrderController& controller
);

}  // namespace fode
