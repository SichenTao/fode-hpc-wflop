/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T60 continuous random-search WFLOP public API
Paper/DOI: Solving the Wind Farm Layout Optimization Problem Using Random
Search Algorithm; 10.1016/j.renene.2015.01.005
Public source: no paper-linked author source was located. Same-lineage public
source https://gitlab.windenergy.dtu.dk/TOPFARM/PyWake.git revision
5b07481ec9b3633a74844651648f266ba82a8b32 (MIT) supplies independently
maintained Horns Rev 1 coordinates and Vestas V80 tabular power/CT data.
Paper-provided assets: Eqs. (1)-(19), Algorithm 1, Figure-1 GA layouts,
Table-3 Weibull resources, three ideal cases, three Horns Rev cases, 360-sector
selection, 100000 complete layout evaluations, 100 ideal and 40 Horns Rev
independent runs for each initialization, and the robustness perturbations.
Missing assets: author Fortran 95 source and random states; exact random-step
distribution; random-feasible initialization procedure; numeric Case-3 ideal
wind array; machine-readable Figure-1 layouts; Horns Rev coordinate, boundary,
power and CT arrays; speed quadrature; offshore roughness; and robustness
sampling increment.
Conflicts and completion: Algorithm 1 does not define a remembered ray that
has no feasible positive step; such a failed ray returns to the random branch,
matching the cited predecessor reconstruction. The ideal Figure-1 coordinates use a 4000 m square,
80 m rotor and 400 m grid while the cited Mosetti/Grady form is often scaled
as 2000 m, 40 m rotor and 200 m grid; these are dimensionlessly identical and
the target figure scale takes precedence. Figure-1 red GA locations are
digitized exactly on its 10x10 grid. Ideal Case-3 weights use the already
audited Mosetti/Grady figure reconstruction. Horns coordinates and V80 curves
use the pinned same-lineage public source. The original boundary is the public
10x8 affine grid hull; Case 3 adds one 7D affine interval to each main
dimension. Random starts use feasible stratified affine jitter because the
paper omits its sampler. Random direction and step are uniform, with the step
bounded by the problem long edge as stated. Weibull speed is integrated in
0.5 m/s bins from 0 to 30 m/s; z0=0.0002 m completes the offshore log-law and
wake-decay fields. Robustness uses {-20,-10,0,10,20}% or degrees.
Target method: Algorithm-1 improved random search with remembered successful
turbine/direction and one-turbine incremental wake-tensor updates.
Target problems: three ideal fixed-count cases; three Horns Rev fixed-80 cases;
12/72/360 direction-preprocessing probe; and Case-1 wind robustness sweeps.
Method/problem semantic IDs: t60_improved_rs_incremental_v1;
t60_ideal_continuous_jensen_v1; t60_hornsrev_jensen_v80_v1
Controlling contract: shared/contracts/core99_t60_feng_shen_2015.json
Production backend: pure C++20 CPU-HPC. Each trajectory performs O(S*N)
incremental updates with lookup-integrated power; independent paper runs fill
all allocated CPU cores without nested oversubscription.
Claim boundary: source-backed flexible academic reproduction of the paper
equations, target algorithm, cases, lifecycle and repeat matrix; not author
source, random state, native numeric arrays or exact-number replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::t60 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct Evaluation {
    double expected_power_kw = 0.0;
    double no_wake_power_kw = 0.0;
    double efficiency = 0.0;
    double constraint_violation_m = 0.0;
    bool feasible = false;
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    std::uint64_t physical_fes = 100000;
    bool random_initial_layout = false;
};

struct RunResult {
    std::string problem_id;
    std::string problem_semantic_id;
    std::string method_semantic_id =
        "t60_improved_rs_incremental_v1";
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    bool random_initial_layout = false;
    std::uint64_t feasible_proposals = 0;
    std::uint64_t rejected_infeasible_proposals = 0;
    std::uint64_t accepted_moves = 0;
    double initial_power_kw = 0.0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    Evaluation final_evaluation;
    std::vector<Point> final_layout;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(
        const std::string& problem_id,
        int direction_sectors = 360,
        double direction_rotation_degrees = 0.0,
        double weibull_scale_multiplier = 1.0,
        double weibull_shape_multiplier = 1.0
    );
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int direction_count() const noexcept;
    [[nodiscard]] double long_edge_m() const noexcept;
    [[nodiscard]] double minimum_spacing_m() const noexcept;
    [[nodiscard]] Evaluation evaluate_full(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] Evaluation evaluate_parallel(
        const std::vector<Point>& layout,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] Evaluation evaluate_incremental_candidate(
        const std::vector<Point>& current,
        const std::vector<Point>& candidate,
        int moved_turbine
    ) const;
    [[nodiscard]] std::vector<Point> paper_initial_layout() const;
    [[nodiscard]] std::vector<Point> random_feasible_layout(
        std::uint64_t seed
    ) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] RunResult run(
    const Problem& problem,
    const RunConfig& config
);

[[nodiscard]] std::vector<std::string> paper_problem_ids();

}  // namespace core99::t60
