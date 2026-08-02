/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0623 adaptive CFD-Kriging-GA paper cases
Paper: Wang, Tu, Zhang, Han, Cao and Zhou, An optimization framework for
wind farm layout design using CFD-based Kriging model;
10.1016/j.oceaneng.2023.116644; arXiv:2309.01387v1.
Primary PDF SHA-256:
5f935a0fe7641d463c9af66baa0087c7bda95c3b3def39a7388f143800be47ea.
Open arXiv source archive: https://arxiv.org/e-print/2309.01387,
CC BY 4.0, SHA-256
5d6fd208c4ffa5750814d6830344a1f3f7800d7002e4026457cdb52898d7b507.
Paper/source-provided facts: AEP and 2D spacing formulation; 360-sample
LHS; ADM-RANS Eqs. (1)-(7); Gaussian Kriging Eqs. (8)-(12); adaptive
optimal-design infill; GA population 50, crossover 0.95, mutation 0.15
and maximum 1000 iterations; 8 turbines on 81 nodes of an 8D by 8D
square; NREL 5 MW turbine; three flat/terrain and 1/8-direction cases;
source figures including the wind rose and target layouts; and target
truth-call totals 437, 400 and 399.
No paper-linked OpenFOAM case, modified simpleFoam/ADM source, CFD meshes,
truth-response arrays, Kriging/GA code, exact LHS, MLE search, genetic
operators, convergence threshold, random seeds or repeats was published.
Reconstruction: exact paper discrete problem and adaptive lifecycle; the
truth backend is an explicitly non-CFD, terrain-aware ADM/Gaussian
response proxy calibrated only to paper AEP scale; the eight-bin wind
rose is transcribed from the CC-BY source figure and normalized; ordinary
Kriging uses one versioned Gaussian-kernel MLE completion, a nugget and
rank-one Cholesky updates; discrete LHS, tournament selection, set
crossover, feasible replacement mutation, 25 repeats and deterministic
stagnation are declared completions.
Problem semantic IDs: l0623_case1_flat_single_proxy_v1;
l0623_case2_flat_windrose_proxy_v1;
l0623_case3_gaussian_hill_windrose_proxy_v1.
Method semantic ID: l0623_adaptive_kriging_ga_completed_v1.
Protocol semantic ID: l0623_3case_25repeat_paper_truth_calls_v1.
Production backend: pure C++ CPU. Initial truth calls, kernel construction,
surrogate population inference and GA offspring are analyzed for a
persistent full-core team; formal tasks never oversubscribe Waffle.
Contract: shared/contracts/core99_l0623_wang_cfd_kriging_2024.json.
Claim boundary: academic declared reproduction of the paper problem,
adaptive Kriging-GA method and all three cases on a versioned ADM/Gaussian
truth proxy; not OpenFOAM CFD, author code/data/meshes, exact operators,
random state, AEP or numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::l0623 {

using Layout = std::array<int, 8>;

struct Evaluation {
    double aep_gwh = 0.0;
    std::array<double, 8> turbine_aep_gwh{};
    double minimum_spacing_margin_m = 0.0;
    bool feasible = false;
};

struct RunConfig {
    std::uint64_t seed = 2026062300;
    int workers = 20;
    int initial_samples = -1;
    int maximum_truth_calls = -1;
    int maximum_ga_generations = -1;
};

struct RunResult {
    std::string case_id;
    std::string problem_semantic_id;
    std::string method_semantic_id;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int initial_samples = 0;
    int truth_calls = 0;
    std::uint64_t surrogate_fes = 0;
    double selected_theta = 0.0;
    Evaluation initial_best;
    Evaluation best_evaluation;
    Layout best_layout{};
    std::vector<double> truth_best_history_gwh;
    double truth_evaluator_seconds = 0.0;
    double surrogate_training_seconds = 0.0;
    double surrogate_inference_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(std::string case_id);
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] const std::string& case_id() const noexcept;
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] int wind_direction_count() const noexcept;
    [[nodiscard]] bool has_gaussian_hill() const noexcept;
    [[nodiscard]] int paper_initial_samples() const noexcept;
    [[nodiscard]] int paper_truth_calls() const noexcept;
    [[nodiscard]] int paper_population() const noexcept;
    [[nodiscard]] int paper_maximum_ga_generations() const noexcept;
    [[nodiscard]] Evaluation evaluate_truth(const Layout& layout) const;
    [[nodiscard]] RunResult optimize(const RunConfig& config) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::vector<std::string> paper_case_ids();
[[nodiscard]] Layout paper_baseline_layout();

}  // namespace core99::l0623
