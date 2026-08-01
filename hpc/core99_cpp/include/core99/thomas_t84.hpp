/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T84 four-case WEC evaluator and optimization package
Paper title/DOI: Wake Expansion Continuation: Multi-Modality Reduction in
the Wind Farm Layout Optimization Problem; 10.1002/we.2692
Primary paper assets: Sections 3-7, Eqs. (1)-(21), Tables 1-9, Vestas V80,
four 16/38/38/60-turbine cases with 20/12/36/72 wind states, 200 common
starts per case, Bastankhah-Niayifar and Jensen-cosine/Katic evaluators,
SNOPT and ALPSO controls, WEC-D factors 3.0/2.6/2.2/1.8/1.4/1.0 followed by
the factor-1 smooth-local-TI stage for Bastankhah, and the paper ALPSO
population/inner/outer schedules.
Public source: https://github.com/byuflowlab/thomas2021-wec at commit
8ff27d66079591f25619abeedbfc970d70e2b520 supplies the paper numeric input
arrays and run drivers. No LICENSE or COPYING file is present, so it is used
as a formula/behavior/data oracle and is not redistributed as executable
production source. Factual numeric inputs are encoded by
scripts/prepare_core99_t84_public_data.py in a provenance-pinned fixture.
Open replacement dependency: NLopt v2.11.0 commit
88c424d4f458412787df96fcc95218acbca224fd supplies SLSQP.
Model-lineage sources: https://github.com/byuflowlab/PlantEnergy at commit
356fafd95d0ff6396f531f8cc05e4526041df12c (Apache-2.0) supplies the
Bastankhah/local-TI behavior oracle; https://github.com/byuflowlab/jensen3d
at commit 08c105334991617eea07f74a8118c9ff6e23c31c supplies the exact
Jensen-cosine/Katic behavior oracle (setup.py declares Apache-2.0).
Missing/conflicts: SNOPT is proprietary; the Tapenade build, pyOptSparse
environment and exact random states are not complete; the source case-1 wind
file says 8 m/s while paper Section 5.1 says 10 m/s; older source drivers use
ALPSO swarm 25 while paper Table 3/final drivers use 30. We follow paper-first
semantics: 10 m/s and swarm 30. Jensen3D consumes tabulated wind speed
directly, whereas the PlantEnergy Bastankhah lineage applies 80-to-70 m shear;
the implementation retains this model distinction. The source/public starts
use the paper's 1D minimum, while optimized layouts enforce 2D. Each
discrepancy and resolution is frozen in the contract and semantic ledger.
Reconstruction: independent pure-C++ evaluators; fixed-order reductions;
fixed-width forward automatic differentiation for all active coordinate
derivatives; analytic spacing and boundary Jacobians; NLopt SLSQP in place
of SNOPT; independent augmented-Lagrangian PSO matching the paper population
and function-call schedules in place of unavailable pyOptSparse ALPSO.
Problem semantic ID: t84_wec_four_case_author_data_v1
Method semantic IDs: t84_slsqp_control_v1, t84_slsqp_wec_v1,
t84_alpso_control_v1, and t84_alpso_wec_v1
Production backend: pure C++ CPU. Gradient runs parallelize wind states;
ALPSO parallelizes complete particle evaluations; formal campaigns
parallelize the 200 independent starts when that gives higher throughput.
Nested oversubscription is forbidden and reductions remain deterministic.
Controlling contract: shared/contracts/core99_t84_thomas_2022.json
Claim boundary: source-backed flexible academic reproduction of the paper's
proposed WEC method, four problems, two physical model families, two
optimizer families and final comparison protocol; not author SNOPT,
Tapenade, PlantEnergy or exact random-state numerical replay
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::t84 {

constexpr int maximum_turbines = 60;
constexpr int maximum_variables = 2 * maximum_turbines;

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

enum class WakeModel {
    bastankhah,
    jensen_cosine,
};

enum class TurbulenceMode {
    ambient_only,
    smooth_local,
    hard_local,
};

enum class OptimizerFamily {
    slsqp_open_snopt_replacement,
    augmented_lagrangian_pso,
};

struct EvaluationSettings {
    WakeModel wake_model = WakeModel::bastankhah;
    double wec_factor = 1.0;
    TurbulenceMode turbulence_mode = TurbulenceMode::ambient_only;
    bool calculate_gradient = false;
};

struct Evaluation {
    double aep_gwh = 0.0;
    double gross_aep_gwh = 0.0;
    double wake_loss_percent = 0.0;
    double maximum_constraint_violation_m = 0.0;
    std::vector<double> directional_power_mw;
    std::vector<double> gradient_gwh_per_m;
    int requested_workers = 0;
    int observed_workers = 0;
    double seconds = 0.0;
};

struct StageReceipt {
    double wec_factor = 1.0;
    TurbulenceMode turbulence_mode = TurbulenceMode::ambient_only;
    int objective_calls = 0;
    int gradient_calls = 0;
    int population_evaluations = 0;
    int optimizer_status = 0;
    std::string optimizer_status_name;
    double start_aep_gwh = 0.0;
    double end_aep_gwh = 0.0;
    double seconds = 0.0;
};

struct RunConfig {
    int workers = 20;
    int start_index = 0;
    std::uint64_t seed = 20260731;
    WakeModel wake_model = WakeModel::bastankhah;
    OptimizerFamily optimizer =
        OptimizerFamily::slsqp_open_snopt_replacement;
    bool use_wec = true;
    bool smoke = false;
    int maximum_slsqp_evaluations_per_stage = 220;
};

struct RunResult {
    std::string problem_semantic_id;
    std::string method_semantic_id;
    int case_id = 0;
    int start_index = 0;
    std::uint64_t seed = 0;
    WakeModel wake_model = WakeModel::bastankhah;
    OptimizerFamily optimizer =
        OptimizerFamily::slsqp_open_snopt_replacement;
    bool use_wec = false;
    int requested_workers = 0;
    int observed_workers = 0;
    Evaluation initial_assessment;
    Evaluation final_assessment;
    std::vector<StageReceipt> stages;
    std::vector<Point> final_layout;
    int paper_function_call_budget = 0;
    int executed_function_calls = 0;
    double evaluator_seconds = 0.0;
    double optimizer_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    // Exposed as an opaque type only so translation-unit-local equation
    // kernels can be independently instantiated for double and AD scalars.
    struct Data;
    Problem(const std::string& data_path, int case_id);
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] int case_id() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int wind_state_count() const noexcept;
    [[nodiscard]] const std::vector<Point>& start(int start_index) const;
    [[nodiscard]] double maximum_constraint_violation(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] std::vector<double> lower_bounds() const;
    [[nodiscard]] std::vector<double> upper_bounds() const;
    [[nodiscard]] int constraint_count() const noexcept;
    void normalized_constraints(
        const std::vector<double>& coordinates,
        std::vector<double>& values,
        std::vector<double>* row_major_jacobian = nullptr
    ) const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout,
        const EvaluationSettings& settings,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_population(
        const std::vector<std::vector<Point>>& layouts,
        const EvaluationSettings& settings,
        fode::PersistentExecutor& executor
    ) const;

private:
    std::shared_ptr<const Data> data_;
    std::string semantic_id_;
    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] const char* wake_model_name(WakeModel model) noexcept;
[[nodiscard]] const char* turbulence_name(TurbulenceMode mode) noexcept;
[[nodiscard]] const char* optimizer_name(OptimizerFamily optimizer) noexcept;
[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config);

}  // namespace core99::t84
