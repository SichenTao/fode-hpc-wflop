/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T25 large-farm exact-gradient, flow-parallel and
Smart-Start (SMAST) academic reproduction
Paper title/DOI: Speeding Up Large-Wind-Farm Layout Optimization Using
Gradients, Parallelization, and a Heuristic Algorithm for the Initial Layout;
10.5194/wes-9-321-2024
Primary paper assets: Eqs. (1)-(9), Algorithm 1, Tables 1-2, Horns Rev 1
100/200/300/400/500-turbine scaling cases, IEA-37
16/36/64/130/279/566-turbine cases, SLSQP, 360 wind directions, 23 V80
speed bins for Horns Rev, exact/finite-difference/complex-step gradient
comparison, flow-case and top-level multi-start parallelization, and SMAST.
Paper data/source: CC-BY-4.0 Zenodo record 10.5281/zenodo.10402450,
GBWFLO_dataset.zip MD5 da3192a1b7467ba038c611ec656536b8, embedded Git
revision a417a88b2e2a309961397ed0fa093370d65a0662. It supplies two NetCDF
result arrays, Hornsrev1_xl.py, utils.py and the post-processing notebook.
Pinned model source: PyWake v2.5.0, Git tag
cd5ff8363ae2615a92860d409e748b4a0431f33d (MIT), supplies the exact
Bastankhah/IEA-37 equations, Horns Rev coordinates, V80 tables and wind
resources. The paper cites TOPFARM but does not pin its revision.
Missing/conflicts: the public archive contains processed results and helper
code, but no environment lock, complete launch scripts, TOPFARM revision,
SLSQP trajectory, optimized coordinates, per-iteration gradients, Horns Rev
timing arrays or exact hardware/runtime state. The paper says IEA-37 uses
constant CT approximately 8/9 and also describes the simplified wake as
equivalent to CT approximately 0.964; PyWake v2.5.0 resolves this by using
physical turbine CT=8/9 and D/sqrt(8) initial wake width. The paper calls
Autograd algorithmic differentiation (AD); this pure-C++ version implements
the same exact chain rule as a fixed reverse accumulation, verified against
central finite differences, including the Horns Rev effective-speed to V80 CT
to downstream-wake dependency, rather than embedding Python Autograd. Published
10,000/5,000-start result arrays are validation evidence, not silently
relabelled as reruns.
Declared reconstruction: exact source formulas and numeric arrays; stable
counter-keyed initialization; deterministic fixed-order state reduction;
open NLopt Kraft SLSQP in place of the unpinned SciPy/TOPFARM stack; and a
paper-equivalent SMAST lifecycle. The public helper defaults to 1000 SciPy
iterations, while both published NetCDF tensors record max_iter=5000; both use
tolerance 1e-4 and expected-cost scaling 10. Because NLopt does not expose the
same SciPy iteration counter, this reconstruction enforces a separately named
maximum objective-callback budget (5000 in production), always emits actual
objective, gradient and complete-layout evaluation counts, and does not equate
that cap with an author iteration trajectory.
HPC design: one persistent all-core executor; exact-gradient evaluation
parallelized over independent flow cases; one geometry traversal produces
the objective and all coordinate derivatives through forward wake propagation
and reverse accumulation; speed-independent geometry is
reused; SMAST incrementally updates candidate-by-direction wake sums in
O(|L|*Ntheta*N) instead of recomputing every partial layout; independent
multi-starts are scheduled at the outer level without nested oversubscription.
Problem semantic IDs: t25_hornsrev_scaled_bg_v80_v1;
t25_iea37_scaled_sbg_v1
Method semantic ID: t25_smast_slsqp_exact_reverse_v1
Production backend: pure C++20 CPU-HPC. The selected production run uses all
Waffle cores. Single-core execution is only a sparse H6 timing baseline.
Controlling contract: shared/contracts/core99_t25_rodrigues_2024.json
Claim boundary: source-backed flexible academic reproduction of both paper
problem families and all target mechanisms; not author Python trajectory,
random bitstream, exact published optimum/timing replay, or first claim for
AD, flow parallelization, SMAST or multi-start parallelization
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::t25 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

enum class ProblemFamily {
    iea37,
    horns_rev,
};

enum class GradientMode {
    none,
    exact_reverse,
    central_finite_difference,
};

struct ProblemConfig {
    ProblemFamily family = ProblemFamily::iea37;
    int turbine_count = 16;
    int direction_count = 360;
    int speed_count = 1;
};

struct Evaluation {
    double aep_gwh = 0.0;
    std::vector<double> gradient_gwh_per_m;
    std::uint64_t physical_layout_evaluations = 0;
    std::uint64_t flow_cases = 0;
    std::uint64_t pair_interactions = 0;
    int requested_workers = 1;
    int observed_workers = 1;
    double seconds = 0.0;
};

struct SmartStartReceipt {
    std::vector<Point> layout;
    double aep_gwh = 0.0;
    double minimum_spacing_m = 0.0;
    int grid_points_initial = 0;
    int grid_points_remaining = 0;
    int random_percent = 0;
    double grid_resolution_rotor_radii = 3.0;
    std::uint64_t candidate_flow_updates = 0;
    int requested_workers = 1;
    int observed_workers = 1;
    double seconds = 0.0;
};

struct OptimizationConfig {
    int workers = 20;
    std::uint64_t seed = 20260801;
    int start_index = 0;
    int random_percent = 0;
    double grid_resolution_rotor_radii = 3.0;
    int maximum_evaluations = 5000;
    double relative_x_tolerance = 1.0e-4;
    bool use_smart_start = true;
};

struct OptimizationReceipt {
    std::string problem_semantic_id;
    std::string method_semantic_id;
    int turbine_count = 0;
    int direction_count = 0;
    int speed_count = 0;
    int requested_workers = 1;
    int observed_workers = 1;
    std::uint64_t seed = 0;
    int start_index = 0;
    int random_percent = 0;
    int optimizer_status = 0;
    std::string optimizer_status_name;
    int objective_calls = 0;
    int gradient_calls = 0;
    int constraint_calls = 0;
    std::uint64_t physical_layout_evaluations = 0;
    double initial_aep_gwh = 0.0;
    double final_aep_gwh = 0.0;
    double minimum_spacing_m = 0.0;
    double maximum_boundary_violation_m = 0.0;
    double initialization_seconds = 0.0;
    double evaluator_seconds = 0.0;
    double optimizer_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<Point> final_layout;
};

class Problem {
public:
    explicit Problem(ProblemConfig config);

    [[nodiscard]] const ProblemConfig& config() const noexcept;
    [[nodiscard]] std::string semantic_id() const;
    [[nodiscard]] double rotor_diameter_m() const noexcept;
    [[nodiscard]] double boundary_radius_m() const noexcept;
    [[nodiscard]] const std::vector<Point>& reference_layout() const noexcept;
    [[nodiscard]] std::vector<Point> random_feasible_layout(
        std::uint64_t seed,
        int start_index
    ) const;
    [[nodiscard]] double minimum_spacing(const std::vector<Point>& layout) const;
    [[nodiscard]] double maximum_boundary_violation(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout,
        GradientMode mode,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] SmartStartReceipt smart_start(
        int random_percent,
        double grid_resolution_rotor_radii,
        std::uint64_t seed,
        int start_index,
        fode::PersistentExecutor& executor
    ) const;

private:
    struct Data;
    std::shared_ptr<const Data> data_;
};

[[nodiscard]] OptimizationReceipt optimize(
    const Problem& problem,
    const OptimizationConfig& config
);

[[nodiscard]] const char* family_name(ProblemFamily family) noexcept;
[[nodiscard]] const char* gradient_name(GradientMode mode) noexcept;

}  // namespace core99::t25
