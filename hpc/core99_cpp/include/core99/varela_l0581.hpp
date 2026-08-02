/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0581 sparse-coloured forward-AD wind-farm gradients.
Paper/DOI: Varela and Ning, Sparsity for Gradient-Based Optimization of
Wind Farm Layouts, AIAA SciTech 2023; 10.2514/6.2023-1543.
Primary paper: local 12-page PDF SHA-256
eeee2168b9f1aa10d99c069995c5a3c9647035e527cbd0fb05ce3f70b62dcc15.
Official full text: https://scholarsarchive.byu.edu/facpub/6493/.
Public software: FLOWFarm.jl, https://github.com/byuflowlab/FLOWFarm.jl,
MIT licence, audited HEAD 9d427cd519fc67fd6e61d335969638939302c4e6.
The repository supplies the Cumulative Curl equations, NREL-5MW Cp/Ct
curve, the exact Nantucket 12-direction wind rose and a 38-turbine round
layout. It does not supply the target paper's optimization scripts. Its
first substantial unstable-sparsity implementation is later than the paper
(commit 4a830bfe6bb645b7f6dce3850aa5b0fd876e02d2, 2024-02-26), so it is used
only as same-author equation/data lineage, not represented as target code.
Paper-provided method: per-wind-state N by 2N turbine-energy Jacobians;
thresholded patterns; greedy column colouring; compressed ForwardDiff;
pattern refresh when 10 percent of retained values are at least two orders
below the threshold; threshold 1e-8 reduced by two orders after updates for
5/6 of the wind states, with floor 1e-16. Accuracy farms contain
38/63/95/133/177/228/285/349 turbines. Optimization uses the 95-turbine
farm, the 12-state Nantucket wind rose at 8 m/s, 10 common randomized starts
with independent coordinate perturbations up to plus/minus 2D, and dense
and sparse gradients.
Missing: target source revision and scripts; optimizer, tolerance, iteration
limit and line search; turbine and rotor-quadrature revision; exact random
seeds; final layouts, convergence arrays and numerical figure data; and the
numeric minimum-spacing constraint described only as "sufficient space".
Paper/source conflict: the stated radii 5.1kD and stated minimum initial
spacing 5.1D are incompatible with the paper's exact size sequence and the
public 38-turbine layout. Those assets imply ring counts floor(6.4k), whose
outer-ring chord spacing approaches 5D and is below 5.1D. This reproduction
preserves the paper's sizes and public-layout lineage and reports the actual
spacing rather than silently claiming 5.1D.
Reconstruction: pure C++20 equation-level Cumulative Curl at hub height,
zero yaw, no local-TI correction, source NREL-5MW Cp/Ct points and exact wind
rose. The paper-omitted optimizer is a declared projected gradient ascent
with Armijo backtracking, circular-boundary projection and 2D minimum-spacing
repair. The 2D value follows FLOWFarm's documented layout-optimization
constraint example and is not attributed to the target paper.
HPC realization: fixed-width forward dual numbers, block-compressed column
seeds, conflict-graph greedy colouring, persistent all-core colour-block and
wind-state execution, deterministic indexed writes and ordered reductions.
Dense and sparse paths share the same evaluator, dual arithmetic, optimizer,
starts and constraints; only the Jacobian seed compression differs.
Method semantic ID: l0581_adaptive_sparse_colored_forward_ad_v1.
Problem semantic IDs: l0581_round_n38_n349_single_direction_v1;
l0581_round_n95_nantucket12_v1.
Protocol semantic ID: l0581_accuracy_8_sizes_plus_10_paired_starts_v1.
Controlling contract: shared/contracts/core99_l0581_sparse_gradient_2023.json.
Claim boundary: flexible equation-, algorithm- and protocol-level academic
reproduction on a disclosed source-lineage Cumulative Curl implementation;
not author FLOWFarm revision, ForwardDiff/SparseDiffTools program, optimizer,
random stream, layouts, numerical figure or timing replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core99::l0581 {

struct Point {
    double x_d = 0.0;
    double y_d = 0.0;
};

using Layout = std::vector<Point>;

enum class GradientMode { dense, sparse };

struct FarmSpec {
    int turbines = 0;
    int rings = 0;
    double boundary_radius_d = 0.0;
    double actual_minimum_initial_spacing_d = 0.0;
};

struct GradientResult {
    int turbines = 0;
    int variables = 0;
    int colors = 0;
    int dual_sweeps = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double threshold = 0.0;
    double normalized_aep = 0.0;
    double seconds = 0.0;
    std::vector<double> gradient;
    std::uint64_t scientific_hash = 0;
};

struct AccuracyResult {
    int turbines = 0;
    double threshold = 0.0;
    int dense_colors = 0;
    int sparse_colors = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double color_fraction = 0.0;
    double maximum_scaled_error = 0.0;
    double dense_seconds = 0.0;
    double sparse_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

struct OptimizationConfig {
    GradientMode mode = GradientMode::sparse;
    std::uint64_t seed = 2023058101ULL;
    int workers = 20;
    int maximum_iterations = 24;
    bool smoke = false;
};

struct OptimizationResult {
    std::string method_semantic_id =
        "l0581_adaptive_sparse_colored_forward_ad_v1";
    std::string problem_semantic_id =
        "l0581_round_n95_nantucket12_v1";
    std::string protocol_semantic_id =
        "l0581_accuracy_8_sizes_plus_10_paired_starts_v1";
    GradientMode mode = GradientMode::sparse;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int iterations = 0;
    int pattern_rebuilds = 0;
    int final_colors = 0;
    double final_threshold = 0.0;
    double initial_wake_loss_percent = 0.0;
    double final_wake_loss_percent = 0.0;
    double wake_loss_reduction_points = 0.0;
    double gradient_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    Layout final_layout;
    std::vector<double> best_history;
    std::uint64_t scientific_hash = 0;
};

[[nodiscard]] std::vector<int> paper_accuracy_sizes();
[[nodiscard]] FarmSpec farm_spec(int turbines);
[[nodiscard]] Layout round_layout(int turbines);
[[nodiscard]] Layout randomized_start(int turbines, std::uint64_t seed);
[[nodiscard]] GradientResult calculate_gradient(
    const Layout& layout,
    double direction_degrees,
    GradientMode mode,
    double threshold,
    int workers
);
[[nodiscard]] AccuracyResult compare_accuracy(
    int turbines,
    double threshold,
    int workers
);
[[nodiscard]] OptimizationResult optimize(const OptimizationConfig& config);
[[nodiscard]] const char* gradient_mode_name(GradientMode mode) noexcept;

}  // namespace core99::l0581
