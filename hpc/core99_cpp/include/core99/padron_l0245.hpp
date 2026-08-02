/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0245 polynomial-chaos AEP and continuous-layout OUU
Paper/DOI: Polynomial chaos to efficiently compute the annual energy
production in wind farm layout optimization; 10.5194/wes-4-211-2019.
Primary paper: local 21-page PDF SHA-256
2c772d4306fa176afe9ff0e343d7918f2c93f20ff09a77982a37adc1846c6c8d.
Paper-linked public data: https://doi.org/10.5281/zenodo.2667424, CC BY 4.0;
archive SHA-256
747a642397b577cf45277e59f8365d973d3055db29f8851d2063c99967be1c29.
It provides the four 60-turbine layouts and 72-bin direction distribution.
Author public source: https://github.com/pjstanle/OUUoptimizations commit
6944b6b65d9806ce28ac0686944386828ed1643f. It confirms the Amalia 14-edge
convex boundary, 2D spacing, NREL-5MW constants, FLORIS/OpenMDAO/SNOPT
lifecycle, direction remapping, rectangle integration and DAKOTA coupling.
No license file or repository license is published, so it is used as
read-only semantic evidence and no source is copied.
Licensed source-lineage dependency: byuflowlab/PlantEnergy Apache-2.0 commit
000f68d85163c57fda4b501c8028f5d03db72d9e supplies the July/August-2016
continuous three-zone FLORIS equations and recommended parameter defaults.
Paper facts: 60 NREL-5MW turbines; D=126.4 m; direction/speed independence;
72 five-degree direction probabilities; truncated Weibull speed on [3,25]
m/s with shape 1.8 and scale 12.55; Grid, Amalia, Optimized and Random
layouts; 200000-sample reference; PC-R with Latin-hypercube samples,
least-squares coefficients, total-order basis and ten-fold order selection;
231/630 PC-R versus 225/625 rectangle states; Amalia/Grid/Random starts;
ten sample sets; a 14-edge Amalia hull; 2D separation; analytic statistic
gradients; SNOPT tolerance 1e-4 in the paper and 2e-6 in the public script.
Missing/conflicts: the public source predates the 2019 two-uncertainty
experiments and hard-codes one uncertain variable and ten points; the exact
231/630 Latin-hypercube arrays, 200000 Monte-Carlo arrays, final two-variable
DAKOTA files, optimized layouts, random states and SNOPT histories are absent.
The paper truncates speed to [3,25], while public code uses [0,30]. The paper
states 1e-4 function precision, while the script requests 2e-6 optimality.
Reconstruction: use the paper's [3,25] distribution and sample counts; create
deterministic seed-indexed centered Latin hypercubes; construct distribution-specific
orthonormal polynomials after the paper's cyclic linear interpolation and
50-equal-width-bin direction discretization by a discrete Stieltjes recurrence; choose total
degree 1..11 or 1..19 by deterministic ten-fold validation at each starting
layout; solve least squares by pivot-free Cholesky with a disclosed small
ridge; and reuse the resulting mean weights for Eq. (35) gradients during
one optimization. Rectangle rules use 15x15 and 25x25 midpoint products.
The modified three-zone FLORIS value path is independently translated from
the licensed 2016 source lineage. Fixed-width forward automatic
differentiation supplies exact derivatives of the translated value path;
open NLopt SLSQP replaces proprietary SNOPT. The paper's source-data AEP
numbers remain anchors and are not claimed as numeric replay.
The author driver instantiates the NREL-5MW abstraction with constant
Ct=4a(1-a), Cp=(0.7737/0.944)4a(1-a)^2, generator efficiency 0.944 and a
5 MW rated-power cap; this source-backed branch is used instead of importing
a turbine curve from another paper. PlantEnergy exposes a ``spline_shift``
input but the audited source and author driver do not publish a nonzero value;
this reconstruction therefore records and uses 0.0.
Method semantic ID: l0245_pcr_cv_gradient_slsqp_declared_v1.
Problem semantic ID: l0245_amalia60_two_uncertainty_floris_declared_v1.
Protocol semantic ID: l0245_four_layout_convergence_three_start_10set_v1.
Production backend: pure C++20 CPU-HPC. A persistent full-core executor
parallelizes independent wind states inside every AEP/gradient call; each
state uses fixed-width automatic differentiation without nested teams.
Controlling contract: shared/contracts/core99_l0245_padron_2019.json.
Claim boundary: source-backed flexible academic reproduction of the target
PC-R method, paper-native problem and protocol; not author DAKOTA/SNOPT
execution, original random-state replay or numerical table reproduction.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::l0245 {

constexpr int turbine_count = 60;

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

enum class LayoutId { grid, amalia, optimized, random };
enum class MethodId {
    pcr_coarse,
    pcr_fine,
    rectangle_coarse,
    rectangle_fine,
    monte_carlo_reference,
};

struct MethodSpec {
    MethodId id = MethodId::pcr_coarse;
    std::string name;
    int physical_wind_states = 0;
    int maximum_polynomial_degree = 0;
    int rectangle_points_per_dimension = 0;
};

struct Evaluation {
    double aep_gwh = 0.0;
    double expected_power_mw = 0.0;
    double minimum_spacing_margin_m = 0.0;
    double maximum_boundary_violation_m = 0.0;
    bool feasible = false;
    int selected_polynomial_degree = 0;
    int physical_wake_simulations = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double scenario_seconds = 0.0;
    double regression_seconds = 0.0;
    std::vector<double> gradient_gwh_per_m;
};

struct RunConfig {
    LayoutId starting_layout = LayoutId::amalia;
    MethodId method = MethodId::pcr_coarse;
    std::uint64_t seed = 2019024501ULL;
    int workers = 20;
    int maximum_evaluations = 1000;
    double relative_x_tolerance = 1.0e-6;
    double maximum_seconds = 0.0;
    bool smoke = false;
    bool evaluate_monte_carlo_reference = false;
};

struct RunResult {
    std::string problem_semantic_id =
        "l0245_amalia60_two_uncertainty_floris_declared_v1";
    std::string method_semantic_id =
        "l0245_pcr_cv_gradient_slsqp_declared_v1";
    std::string protocol_semantic_id =
        "l0245_four_layout_convergence_three_start_10set_v1";
    std::string starting_layout;
    std::string method;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int objective_calls = 0;
    int gradient_calls = 0;
    int optimizer_status = 0;
    std::string optimizer_status_name;
    Evaluation initial_evaluation;
    Evaluation final_evaluation;
    Evaluation reference_evaluation;
    std::uint64_t reference_seed = 0;
    std::vector<Point> final_layout;
    std::vector<double> best_history_gwh;
    double evaluator_seconds = 0.0;
    double regression_seconds = 0.0;
    double optimizer_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

struct ProfileReceipt {
    std::string method;
    std::string layout;
    int repeats = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int physical_wake_simulations = 0;
    double aep_checksum_gwh = 0.0;
    double seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    struct Impl;

    explicit Problem(const std::string& data_path);
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] const std::vector<Point>& layout(LayoutId id) const;
    [[nodiscard]] MethodSpec method(MethodId id) const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout,
        MethodId method,
        std::uint64_t seed,
        int workers,
        bool gradient
    ) const;
    [[nodiscard]] RunResult optimize(const RunConfig& config) const;
    [[nodiscard]] ProfileReceipt profile(
        LayoutId layout,
        MethodId method,
        std::uint64_t seed,
        int workers,
        int repeats
    ) const;

private:
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::string to_string(LayoutId id);
[[nodiscard]] std::string to_string(MethodId id);
[[nodiscard]] LayoutId parse_layout(const std::string& value);
[[nodiscard]] MethodId parse_method(const std::string& value);

}  // namespace core99::l0245
