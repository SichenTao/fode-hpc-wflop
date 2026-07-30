/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T20 four-problem WFLOP comparison benchmark
Paper/DOI: Comparative Performance of Twelve Metaheuristics for Wind Farm
Layout Optimisation; 10.1007/s11831-021-09586-7
Public source: no paper-linked author code or data archive was located
Paper-provided asset: four 2000 m by 2000 m, 10 by 10 grid problems; Jensen
wake equations; hub-only and partial-overlap wake concepts; variable-count
binary and fixed-39 permutation-like decoders; turbine constants; 25,000
physical FES and ten independent runs for each comparison algorithm
Target contribution: problem definitions and comparison protocol. The twelve
metaheuristics are comparison baselines, not new target algorithms, and are
therefore not reimplemented as T20-owned methods.
Missing/conflicts: no source, seeds, histories, exact numerical precision, or
complete result layouts; Algorithm 2 step 5 prints Gc(i), while its prose and
input-dependent intent specify Gc(round(x_i)); the paper does not explicitly
state whether the wake-decay value is recomputed or fixed to the preceding
0.086 example
Resolution: follow the prose decoder Gc(round(x_i)); use the stated
k=0.5/log(h/h0) surface-roughness relation with Table-1 h and h0; count every
complete 36-direction farm evaluation as one physical FES; preserve all
conflicts in the semantic contract
Problem semantic ID: t20_kunakote_four_case_benchmark_declared_v1
Method semantic ID: t20_comparison_protocol_no_target_optimizer_v1
Production backend: pure C++ CPU; candidate-batch evaluation uses a persistent
full-resource worker team and deterministic indexed result writes
Claim boundary: academic declared reproduction of the four proposed problems
and comparison interface, not author-code or twelve-baseline numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t20 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct Evaluation {
    double objective = 0.0;
    double average_power_kw = 0.0;
    double cost = 0.0;
    double constraint_violation = 0.0;
    int turbine_count = 0;
};

struct BatchReceipt {
    std::string problem_id;
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    Evaluation best_evaluation;
    std::vector<double> best_variables;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(std::string problem_id);

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] int paper_case() const noexcept;
    [[nodiscard]] int dimension() const noexcept;
    [[nodiscard]] bool uses_partial_overlap() const noexcept;
    [[nodiscard]] bool has_fixed_turbine_count() const noexcept;
    [[nodiscard]] std::vector<double> lower_bounds() const;
    [[nodiscard]] std::vector<double> upper_bounds() const;
    [[nodiscard]] std::vector<Point> decode(
        const std::vector<double>& variables
    ) const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<double>& variables
    ) const;
    [[nodiscard]] Evaluation evaluate_layout(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_population(
        const std::vector<std::vector<double>>& population,
        fode::PersistentExecutor& executor
    ) const;

private:
    std::string id_;
    int paper_case_ = 0;
    bool partial_overlap_ = false;
    bool fixed_count_ = false;
};

[[nodiscard]] std::vector<Point> paper_figure_5_layout();
[[nodiscard]] BatchReceipt run_batch_profile(
    const Problem& problem,
    std::uint64_t seed,
    std::uint64_t physical_fes,
    int workers
);

}  // namespace core99::t20
