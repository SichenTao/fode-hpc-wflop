/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T01 Mosetti and T02 Grady historical 10x10 grid interface
Papers: Mosetti et al. 1994 and Grady et al. 2005.
Paper DOIs:
10.1016/0167-6105(94)90080-9 and 10.1016/j.renene.2004.05.007
Public source evidence: no author implementation was located; the no-license
WFLOPG repository at revision 05a3a6cd2e767f956dcc4a15256f7854e923624a
provides an independent later interpretation and digitized-wind cross-check
Missing/conflicts: partial-wake computation, exact GA controls, island
communication, T02 rotor-size label, and T02 case-C generation count
Resolution: paper equations plus explicitly registered deterministic
completions in shared/contracts/core99_mosetti_grady_cases.json
Contract: shared/contracts/core99_mosetti_grady_cases.json
Method/problem semantic IDs: t01_mosetti_ga_declared_v1 and
t02_grady_island_ga_declared_v1; core99_mosetti_grady_historical_grid_v1
Production backend: pure C++ persistent CPU team with precomputed wake geometry
Claim boundary: fixed academic declared reproduction; no author-source or
author-exact numerical claim
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <string>
#include <vector>

namespace core99 {

struct LayoutBits {
    std::uint64_t low = 0;
    std::uint64_t high = 0;

    auto operator<=>(const LayoutBits&) const = default;
};

struct HistoricalEvaluation {
    double objective = 0.0;
    double expected_power_kw = 0.0;
    int turbine_count = 0;
};

struct HistoricalProfile {
    std::string algorithm_id;
    std::string method_semantic_id;
    int population = 0;
    int islands = 0;
    int generations = 0;
    double crossover_probability = 0.0;
    double mutation_probability = 0.0;
};

struct HistoricalRunRequest {
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    int workers = 0;
};

struct HistoricalRunResult {
    std::string algorithm_id;
    std::string method_semantic_id;
    std::string problem_id;
    std::string problem_semantic_id;
    LayoutBits best_layout;
    HistoricalEvaluation best_evaluation;
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    std::uint64_t generations = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class HistoricalGridProblem {
public:
    explicit HistoricalGridProblem(std::string problem_id);

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] HistoricalEvaluation evaluate(LayoutBits layout) const;
    [[nodiscard]] std::vector<HistoricalEvaluation> evaluate_population(
        const std::vector<LayoutBits>& layouts,
        fode::PersistentExecutor& executor
    ) const;

private:
    struct State {
        double speed = 0.0;
        double probability = 0.0;
        int direction_index = 0;
    };

    std::string id_;
    std::string semantic_id_;
    std::vector<State> states_;
    std::vector<double> deficit_;

    void configure_states();
    void precompute_deficits();
};

[[nodiscard]] HistoricalProfile historical_profile(
    const std::string& algorithm_id
);

[[nodiscard]] std::uint64_t default_physical_fes(
    const HistoricalProfile& profile,
    const std::string& problem_id
);

[[nodiscard]] HistoricalRunResult run_historical_ga(
    const HistoricalGridProblem& problem,
    const HistoricalProfile& profile,
    const HistoricalRunRequest& request
);

[[nodiscard]] std::vector<int> layout_cells(LayoutBits layout);
[[nodiscard]] LayoutBits layout_from_cells(const std::vector<int>& cells);

}  // namespace core99
