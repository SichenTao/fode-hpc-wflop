/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T17 complex-terrain Gaussian-streamline wake model and
double-stage optimization method
Paper title/DOI: A New Wake Model and Comparison of Eight Algorithms for
Layout Optimization of Wind Farms in Complex Terrain;
10.1016/j.apenergy.2019.114189
Public source: the paper provides no linked code or data. PyWake commit
5b07481ec9b3633a74844651648f266ba82a8b32 (MIT) supplies an independently
maintained streamline-distance implementation and public ParqueFicticio WAsP
grids. The latter are converted into a compact same-lineage proxy.
Paper-provided assets: Eqs. (1)-(14); 6000 m by 4000 m domain; 25 turbines;
93 m diameter, 67 m hub height, four-diameter spacing; 12 sectors; 7 m/s
reference speed; Gaussian growth k*=0.042; 1% exclusion; RSS wake
superposition; normalized-effective-speed objective; single- and double-stage
protocols; 20,000 s stage-2 and 5,000 s stage-1 CPU-time limits; one original
and ten random initial layouts; objective anchors 26.88 and 28.10
Target contribution: the complex-terrain wake evaluator and the double-stage
mechanism. The eight MATLAB/toolbox algorithms are comparison baselines, not
T17 target methods. Random search is retained only as the paper's demonstrated
vehicle and follows cited predecessor DOI 10.1016/j.renene.2015.01.005.
Missing/conflicts: private Northwest-China terrain, WAsP CFD grids,
streamlines, exact turbine coordinates, turbine power/CT arrays, sector
probabilities, MATLAB code, toolbox settings, seeds, histories, and timing
implementation are unavailable. Table 2 in the preprint swaps the displayed
speed/direction units. The published 20,000/25,000 s are CPU-time rather than
portable FES budgets.
Reconstruction: use Figure-2-digitized feasible coordinates; use PyWake public
WAsP grids affinely mapped to the paper domain as a declared background-flow
proxy; interpolate its 30 m/200 m grids to 67 m; use the printed CT=0.747 at
7 m/s plus a declared piecewise CT curve; calculate pseudo-3D streamlines at
25 m steps; rotor-average the Gaussian deficit with fixed equal-area
quadrature; implement predecessor RS random/remembered moves; support both
paper CPU-time protocol and fixed-work H5/H6
Problem semantic ID: t17_brogna_private_site_open_flow_proxy_v1
Method semantic ID: t17_double_stage_rs_declared_reconstruction_v1
Production backend: pure C++ CPU; 12 wind-sector calculations run on a
persistent worker team; formal independent starts are scheduled concurrently
to consume the remaining Waffle cores without changing single-run semantics
Controlling contract: shared/contracts/core99_t17_brogna_2020.json
Claim boundary: academic declared reproduction of equations, domain,
constraints, double-stage lifecycle, and open-proxy complex-terrain behavior;
not the private Northwest-China benchmark or author-numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::t17 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct Evaluation {
    double objective = 0.0;
    double constraint_violation_m = 0.0;
    bool includes_wakes = true;
};

struct SearchConfig {
    std::uint64_t stage1_fes = 0;
    std::uint64_t stage2_fes = 1000;
    bool random_initial_layout = false;
};

struct RunResult {
    std::string problem_semantic_id;
    std::string method_semantic_id;
    std::uint64_t seed = 0;
    std::uint64_t stage1_physical_fes = 0;
    std::uint64_t stage2_physical_fes = 0;
    std::uint64_t physical_fes = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    Evaluation initial_wake_evaluation;
    Evaluation stage1_evaluation;
    Evaluation final_evaluation;
    std::vector<Point> final_layout;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(const std::string& proxy_path);
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout,
        bool include_wakes,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] double constraint_violation(
        const std::vector<Point>& layout
    ) const;

private:
    struct FlowData;
    std::shared_ptr<const FlowData> data_;
    std::string semantic_id_;
};

[[nodiscard]] std::vector<Point> paper_figure_2_layout();
[[nodiscard]] double gaussian_deficit_ratio(
    double streamwise_diameters,
    double radial_diameters,
    double thrust_coefficient
);
[[nodiscard]] RunResult run_double_stage_rs(
    const Problem& problem,
    std::uint64_t seed,
    int workers,
    const SearchConfig& config
);

}  // namespace core99::t17
