/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T21 density-based wind-farm topology optimization
Paper/DOI: Topology Optimization of Wind Farm Layouts;
10.1016/j.renene.2022.06.019
Public source:
https://github.com/byuflowlab/iea37-wflo-casestudies at revision
af88908d22795030ac2dfbe37bc38e912aee8ed6
Provided assets: 16-bin IEA37 wind rose and 3.35 MW turbine/wake reference
files; official Svanberg GCMMA-MMA 1.5 MATLAB code is currently public under
GPLv3; the IEA37 source repository has no root license file
Missing assets: the author's MATLAB wind-farm/topology wrapper, exact MMA
control loop, random states, and raw optimization results are not public
Paper/source conflicts: the paper specifies a 3.37 MW rating and modified
2016 Gaussian wake with near-wake offset, whereas the public IEA37 source
specifies 3.35 MW and a simplified Gaussian model without that offset
Resolution: paper equations and constants control; the public source supplies
the wind frequencies; the modified Gaussian near-wake completion follows the
paper-cited Stanley--Ning lineage; pinned MIT-licensed NLopt MMA is the
production replacement for the GPLv3 MATLAB solver; restarting it at each
ten-evaluation continuation stage cannot preserve the author's wrapper state,
and its 0.1 initial step cannot enforce the paper's unavailable per-iteration
moving-limit wrapper exactly
Optimizer-performance completion: the NLopt dual tolerance is 1e-8 and its
dual budget is 500, matching the paper-level requested precision and preventing
the default 1e-14/100000 serial dual solve from dominating the HPC evaluator
Target method: RAMP density interpolation, analytical objective gradients,
minimum/maximum volume constraints, local spacing constraints, q continuation,
and conservative moving bounds
Target problems: radius-1300 m 124-site and radius-3000 m 709-site circular
grids, 200 m pitch, 16--64 and 64--256 turbines, respectively
Postprocessing completion: because the paper says only that final densities
are near-discrete and gives no numerical classification tolerance, values
strictly above 0.5 are classified as installed turbines
Method/problem semantic IDs: t21_ramp_mma_declared_reconstruction_v1;
t21_pollini_two_circle_density_wflop_v1
Controlling contract: shared/contracts/core99_t21_pollini_2022.json
Production backend: pure C++20 CPU-HPC with pinned NLopt MMA; pairwise wake
terms are precomputed, objective-gradient target blocks are parallel, and the
paper's independent-start campaign is hierarchically parallelized across runs
Claim boundary: academic declared reproduction of the paper method/problem,
not author-code, author-random-state, or exact numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace core99::t21 {

enum class CaseId {
    radius_1300,
    radius_3000,
};

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct Evaluation {
    double aep_gwh = 0.0;
    double objective = 0.0;
    double minimum_count_constraint = 0.0;
    double maximum_count_constraint = 0.0;
    double maximum_spacing_constraint = 0.0;
    int observed_workers = 0;
    std::vector<double> objective_gradient;
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    int start_index = 0;
    int workers = 20;
    int maximum_objective_evaluations = 1000;
    bool random_start = false;
    bool linear_interpolation = false;
};

struct RunResult {
    std::string problem_id;
    std::string problem_semantic_id =
        "t21_pollini_two_circle_density_wflop_v1";
    std::string method_semantic_id =
        "t21_ramp_mma_declared_reconstruction_v1";
    std::string optimizer_backend = "nlopt_ld_mma_declared_replacement";
    std::uint64_t seed = 0;
    int start_index = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int potential_sites = 0;
    int discrete_turbines = 0;
    int objective_evaluations = 0;
    int gradient_evaluations = 0;
    int optimizer_status = 0;
    std::string optimizer_status_name;
    double initial_q = 0.0;
    double q_increment = 0.0;
    double final_q = 0.0;
    double relaxed_aep_gwh = 0.0;
    double discrete_aep_gwh = 0.0;
    double relaxed_constraint_violation = 0.0;
    double evaluator_seconds = 0.0;
    double optimizer_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<double> densities;
};

class Problem {
public:
    explicit Problem(CaseId id, int preprocessing_workers = 1);

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] int potential_sites() const noexcept;
    [[nodiscard]] int minimum_turbines() const noexcept;
    [[nodiscard]] int maximum_turbines() const noexcept;
    [[nodiscard]] double paper_initial_density() const noexcept;
    [[nodiscard]] const std::vector<Point>& grid() const noexcept;
    [[nodiscard]] const std::vector<std::pair<int, int>>&
    spacing_pairs() const noexcept;

    [[nodiscard]] Evaluation evaluate(
        const std::vector<double>& densities,
        double ramp_penalty,
        fode::PersistentExecutor& executor,
        bool need_gradient
    ) const;

    [[nodiscard]] double maximum_constraint_violation(
        const std::vector<double>& densities
    ) const;

private:
    CaseId case_id_;
    std::string id_;
    int minimum_turbines_ = 0;
    int maximum_turbines_ = 0;
    double initial_density_ = 0.0;
    std::vector<Point> grid_;
    std::vector<std::pair<int, int>> spacing_pairs_;
    std::vector<double> squared_pair_deficits_;
};

[[nodiscard]] RunResult run(
    const Problem& problem,
    const RunConfig& config
);

}  // namespace core99::t21
