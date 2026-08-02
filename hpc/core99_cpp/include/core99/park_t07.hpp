/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T07 Park-Law continuous-wake SCP package
Paper: Jinkyoo Park and Kincho H. Law, Layout Optimization for Maximizing
Wind Farm Power Production Using Sequential Convex Programming,
Applied Energy 151 (2015) 320-334,
DOI 10.1016/j.apenergy.2015.03.139.
Primary PDF SHA-256:
3cb643c1aa8585ce539bb170c78dc50f0da36a2cd8dd10734e880e70eba1ced3.
Public source: no paper-linked author code or data archive was found. A
legally accessible Elsevier API author manuscript was consumed privately.
Paper-provided facts: Eqs. (1)-(30), continuous Gaussian radial wake,
rotor-disc average, root-sum-square deficits, 0.9 SOWFA power calibration,
k=0.033, exact objective gradient, damped BFGS Hessian, Algorithm 1 trust
SCP with acceptance 0.2 and expansion/contraction 1.1/0.5, 5D spacing,
Horns Rev 1 rhombus with 80 NREL-5MW turbines, three single-direction cases,
the twelve-bin Horns Rev distribution, and five k-sensitivity cases.
Missing: author MATLAB/CVX files, CFD/SOWFA arrays, exact Horns Rev numeric
coordinates and polygon, rotor quadrature, wind-speed discretization, initial
trust radius, initial Hessian, epsilon, iteration cap and solver tolerances
were not published.
Reconstruction: Figure-10 digitized 8x10 rhombus with 7D basis vectors,
97.35-degree row orientation and a diagonal that rounds to the reported 10.4D;
64-point equal-area deterministic polar rotor quadrature; Figure-13 0-30 m/s
one-m/s Weibull
bins; q0=0.25D, B0=-1e-7I, epsilon=1e-3D and 100-iteration cap; explicit
Algorithm-1 trust-region SCP and damped BFGS, with pinned open NLopt SLSQP
used only to solve each linear-constraint concave QP in place of CVX.
Method semantic ID: t07_explicit_scp_open_qp_declared_v1.
Problem semantic IDs: t07_hornsrev80_single_direction_v1;
t07_hornsrev80_expected_wind_v1; t07_hornsrev80_k_sensitivity_v1.
Protocol semantic ID: t07_nine_paper_cases_one_deterministic_run_v1.
Production backend: pure C++ CPU. Fixed-width forward AD computes all 160
objective derivatives in one traversal; one persistent full-core team
parallelizes independent direction-turbine wake reductions without nesting.
Claim boundary: academic paper-equation reconstruction with declared
quadrature, discretization, geometry and open-QP completions; not author
CVX/SCP, exact Horns Rev coordinates, CFD/SOWFA replay or numerical replay.
Contract: shared/contracts/core99_t07_park_scp_2015.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::t07 {

constexpr int turbine_count = 80;
constexpr int variables = 2 * turbine_count;

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct Evaluation {
    double efficiency = 0.0;
    std::vector<double> gradient;
    int requested_workers = 0;
    int observed_workers = 0;
    double seconds = 0.0;
};

struct ScpStage {
    int iteration = 0;
    bool accepted = false;
    int qp_status = 0;
    int qp_evaluations = 0;
    double trust_radius_m = 0.0;
    double initial_efficiency = 0.0;
    double proposed_efficiency = 0.0;
    double actual_predicted_ratio = 0.0;
    double step_norm_m = 0.0;
    double maximum_constraint_violation_m = 0.0;
    double seconds = 0.0;
};

struct RunConfig {
    int workers = 20;
    int maximum_scp_iterations = 100;
    int maximum_qp_evaluations = 300;
    double epsilon_m = 0.126;
};

struct RunResult {
    std::string case_id;
    std::string problem_semantic_id;
    std::string method_semantic_id;
    int requested_workers = 0;
    int observed_workers = 0;
    double wake_expansion = 0.0;
    double published_initial_efficiency = 0.0;
    double published_optimized_efficiency = 0.0;
    Evaluation initial;
    Evaluation final;
    std::vector<Point> final_layout;
    std::vector<ScpStage> stages;
    double maximum_constraint_violation_m = 0.0;
    double evaluator_seconds = 0.0;
    double qp_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(std::string case_id);

    [[nodiscard]] const std::string& case_id() const noexcept;
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] const std::vector<Point>& initial_layout() const noexcept;
    [[nodiscard]] double wake_expansion() const noexcept;
    [[nodiscard]] double published_initial_efficiency() const noexcept;
    [[nodiscard]] double published_optimized_efficiency() const noexcept;
    [[nodiscard]] double maximum_constraint_violation(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout,
        bool gradient,
        fode::PersistentExecutor& executor
    ) const;

private:
    struct Data;
    std::string case_id_;
    std::string semantic_id_;
    std::shared_ptr<const Data> data_;
};

[[nodiscard]] RunResult run(const Problem&, const RunConfig&);
[[nodiscard]] std::vector<std::string> paper_case_ids();

}  // namespace core99::t07
