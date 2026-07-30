/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T04 UWFLO continuous layout/rotor cases and constrained PSO
Paper/DOI: Unrestricted Wind Farm Layout Optimization: Investigating Key
Factors Influencing the Maximum Power Generation;
10.1016/j.renene.2011.09.017
Public source: no author implementation was located
Paper-preserved problem: Frandsen/Park analytical wakes with partial overlap,
experimental 1.68 by 0.72 m farm, 0.12 m rotor, 7.0896 or 6.2 m/s inflow,
Cases 1--3, variable-rotor Case 2, Case-3 size/count studies, and farm efficiency
Paper-preserved method: constrained PSO, inertia 0.5, local/global factors 1.4,
swarm size 5 times dimension, Deb constrained domination, paper FES budgets
Missing/conflicts: author source, exact Fig.4 power-curve samples, initial
velocity law, boundary response, and Eq.29 printing diameter sums although
the paper's 18-turbine 7D by 3D case is infeasible under a 2D center spacing
Resolution: paper Cp/induction fits, zero initial velocity, bound projection,
one-diameter center spacing required by the paper-native cases, equivalent
commercial-diameter scaling
for Eq.19, counter-keyed random events, and ordered best commits
Method/problem semantic IDs: t04_uwflo_constrained_pso_declared_v1;
t04_uwflo_cases_declared_v1
Production backend: pure C++ persistent CPU team; particle update and complete
farm evaluation are parallel, personal/global best commits are ordered
Claim boundary: academic declared reproduction, not author-source or
author-exact numerical reproduction
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t04 {

struct Evaluation {
    double farm_power_w = 0.0;
    double farm_efficiency = 0.0;
    double constraint_violation = 0.0;
};

struct RunResult {
    std::string problem_id;
    std::vector<double> best_variables;
    Evaluation best_evaluation;
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(std::string problem_id);

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int dimension() const noexcept;
    [[nodiscard]] int paper_case() const noexcept;
    [[nodiscard]] double farm_x_m() const noexcept;
    [[nodiscard]] double farm_y_m() const noexcept;
    [[nodiscard]] std::vector<double> lower_bounds() const;
    [[nodiscard]] std::vector<double> upper_bounds() const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<double>& variables
    ) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_population(
        const std::vector<std::vector<double>>& variables,
        fode::PersistentExecutor& executor
    ) const;

private:
    std::string id_;
    int turbines_ = 0;
    int paper_case_ = 0;
    double farm_x_ = 0.0;
    double farm_y_ = 0.0;
    bool variable_diameter_ = false;
};

[[nodiscard]] std::uint64_t paper_physical_fes(const Problem& problem);

[[nodiscard]] RunResult run(
    const Problem& problem,
    std::uint64_t seed,
    std::uint64_t physical_fes,
    int workers
);

}  // namespace core99::t04
