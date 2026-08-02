/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T48 analytical offshore-LCOE problem and gradient search
Paper/DOI: An Analytical Framework for Offshore Wind Farm Layout Optimization;
10.1260/030952407780811401
Public source: no paper-linked author code or numeric data archive was located
Paper-provided asset: LCOE and cost equations, two-turbine continuous
opportunity set, distance-from-shore wind-speed law, periodic PCHIP wind fits,
Jensen/RSS wake equations, Weibull energy integral, constants, initial/final
layouts, 190-iteration example, printed LCOE/capital/capacity-factor anchors
Missing: exact 16-sector Thompson Island values, manufacturer power-curve
table, PCHIP coefficients, gradient-search update/step/termination rules,
author numerical quadrature, random seed, and run history
Resolution: digitize the 16 Fig.1 markers and register them in the contract;
use a declared piecewise-linear 1.5MW curve normalized to the paper's 42%
isolated capacity-factor anchor; use periodic monotone cubic Hermite wind
interpolation, deterministic bounded coordinate-gradient trials, 1-degree
direction quadrature, and 0.25m/s midpoint speed quadrature
Contract: shared/contracts/core99_t48_lackner_2007.json
Problem semantic ID: t48_lackner_two_turbine_lcoe_declared_v1
Method semantic ID: t48_lackner_gradient_coordinate_reconstruction_v1
Production backend: pure C++ persistent CPU team; all candidate-turbine-
direction integrals are parallel, reductions and candidate commits are ordered
Claim boundary: academic declared reproduction of the analytical framework and
two-turbine demonstration, not author-source or exact 190-step replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <vector>

namespace core99::t48 {

struct Evaluation {
    double lcoe_dollars_per_kwh = 0.0;
    double capital_cost_dollars = 0.0;
    double annual_energy_kwh = 0.0;
    double capacity_factor = 0.0;
    double wake_loss_fraction = 0.0;
    double constraint_violation = 0.0;
};

struct RunResult {
    std::vector<double> initial_variables;
    Evaluation initial_evaluation;
    std::vector<double> best_variables;
    Evaluation best_evaluation;
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    int iterations = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    [[nodiscard]] int dimension() const noexcept;
    [[nodiscard]] std::vector<double> lower_bounds() const;
    [[nodiscard]] std::vector<double> upper_bounds() const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<double>& variables
    ) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_population(
        const std::vector<std::vector<double>>& population,
        fode::PersistentExecutor& executor
    ) const;
};

[[nodiscard]] std::vector<double> paper_initial_layout();
[[nodiscard]] std::vector<double> paper_reported_final_layout();
[[nodiscard]] RunResult run(
    const Problem& problem,
    std::uint64_t seed,
    int iterations,
    int workers
);

}  // namespace core99::t48
