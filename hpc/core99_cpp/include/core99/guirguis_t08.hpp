/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T08 exact-gradient multi-start interior-point WFLOP
academic reproduction
Paper/DOI: Toward Efficient Optimization of Wind Farm Layouts: Utilizing
Exact Gradient Information; 10.1016/j.apenergy.2016.06.101
Primary paper asset: publisher PDF SHA-256
f3df418e9cdb09f18873b3ee35848068e94634d0cfb771ba569a01df7cd6adf0;
Eqs. (1)-(39), Tables 1-6 and Figs. 3, 5 and 9-10 were consumed.
Public source: no paper-linked author implementation or numeric data archive
was found in the paper, PDF annotations, author/DOI search, or project corpus.
Provided facts: continuous x/y variables; Jensen wake with the paper's
Gaussian angular modulation and root-sum-square deficits; cubic power;
analytical objective, spacing and land gradients; nonlinear interior-point
local optimization; uniform-staggered, one-random, five-start and twenty-start
initialization; three 2 km benchmark wind resources and 10/20/30 turbines;
Horns-Rev wind, V80, 37/50/100 turbines and densities 4/5/6 per km2; a
37-turbine 66.2-percent Copenhagen-shaped farm; and a 20-turbine 50 m ring.
Missing/conflicts: MATLAB/fmincon source, release, tolerances, BFGS state,
random states and exact Latin-hypercube layouts are absent. Figure 3(c),
Figure 5 and both land boundaries have no machine-readable arrays. The paper
describes polygon constraints through dynamically selected forbidden-region
partitions but does not publish the partition coordinates. It calls the
terrain roughness parameter Zo in Table 1 although Eq. (3) uses z0. Section
4.3 says 5S-IPM while Figs. 9-10 label a single displayed random start.
Declared reconstruction: a pure-C++ feasible log-barrier interior-point
method with limited-memory BFGS and exact first derivatives replaces the
unavailable MATLAB trajectory. The classical Case-C wind distribution is the
versioned same-benchmark T05 Figure-5 digitization. The Horns-Rev directional
frequencies are the public numeric lineage already pinned by T25, rotated to
the paper's plotted wind-to convention and normalized at the stated 10 m/s.
The V80 cases retain 0.3 m, the paper's sole numerical roughness and therefore
the nearest paper-native continuation; the paper does not state a replacement
roughness in Sections 4.2-4.3, so this completion is emitted rather than
silently substituting an offshore convention.
The Copenhagen polygon is digitized from Fig. 9 then affine-calibrated to the
published 66.2-percent area; its missing partitions are represented by a
piecewise differentiable signed-distance boundary. The ring uses the visible
800/750 m radii. Latin-hypercube ranks are mapped to a strictly feasible
staggered candidate lattice because the paper omits infeasible-start handling.
Every raw initialization policy, optimizer evaluation count, feasibility
margin and completion is emitted; no author numerical replay is claimed.
HPC design: a persistent fixed-slot executor evaluates wind-state/turbine
tasks and pair constraints with deterministic reductions. A one-start role
uses all cores inside objective and barrier evaluation. Five- and twenty-start
roles use all cores across independent interior-point solves, each with a
single-thread evaluator, preventing nested oversubscription. Objective and
analytical gradient share one wake traversal. Production selects every Waffle
core; one-core work is limited to sparse H6 baselines.
Method semantic ID: t08_exact_gradient_log_barrier_lbfgs_declared_ipm_v1
Problem semantic IDs: t08_classical_case1_2_3_continuous_v1;
t08_horns_density_scaling_v1; t08_copenhagen_figure66p2_v1;
t08_ring50m_v1
Protocol semantic ID: t08_49role_native_multistart_v1
Controlling contract: shared/contracts/core99_t08_guirguis_2016.json
Claim boundary: flexible equation-level academic reproduction of every target
problem and proposed IPM role, not author MATLAB/fmincon code, exact polygon
partition, source random stream, trajectory, table replay or first claim for
gradient optimization or parallel multi-start.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::t08 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct WindState {
    double flow_to_degrees = 0.0;
    double speed_mps = 0.0;
    double probability = 0.0;
};

enum class StartPolicy {
    uniform_staggered,
    latin_hypercube_feasible,
};

struct Evaluation {
    double efficiency_percent = 0.0;
    std::vector<double> gradient_percent_per_m;
    std::uint64_t wind_turbine_tasks = 0;
    int requested_workers = 1;
    int observed_workers = 1;
    double seconds = 0.0;
};

struct ConstraintReceipt {
    double barrier_value = 0.0;
    std::vector<double> barrier_gradient_per_m;
    double maximum_violation = 0.0;
    double minimum_normalized_margin = 0.0;
    std::uint64_t pair_constraints = 0;
    std::uint64_t land_constraints = 0;
    int requested_workers = 1;
    int observed_workers = 1;
    double seconds = 0.0;
};

struct PaperCase {
    std::string case_id;
    std::string problem_semantic_id;
    int turbine_count = 0;
    double width_m = 0.0;
    double height_m = 0.0;
    double rotor_diameter_m = 0.0;
    double hub_height_m = 0.0;
    double roughness_m = 0.0;
    double minimum_spacing_m = 0.0;
    std::vector<WindState> wind_states;
    std::string land_model;
    double published_reference_efficiency_percent = 0.0;
};

struct OptimizationConfig {
    StartPolicy start_policy = StartPolicy::uniform_staggered;
    int starts = 1;
    std::uint64_t seed = 201606101ULL;
    int workers = 20;
    int maximum_evaluations_per_start = 1500;
    int barrier_phases = 6;
    double initial_barrier = 0.08;
    double gradient_tolerance = 1.0e-7;
};

struct StartReceipt {
    int start_index = 0;
    double initial_efficiency_percent = 0.0;
    double final_efficiency_percent = 0.0;
    double maximum_constraint_violation = 0.0;
    double minimum_spacing_m = 0.0;
    int objective_gradient_evaluations = 0;
    int accepted_steps = 0;
    int barrier_phases_completed = 0;
    std::string termination;
    double evaluator_seconds = 0.0;
    double constraint_seconds = 0.0;
    double optimizer_seconds = 0.0;
    std::vector<Point> final_layout;
};

struct OptimizationReceipt {
    std::string case_id;
    std::string problem_semantic_id;
    std::string method_semantic_id;
    std::string start_policy;
    int starts = 0;
    std::uint64_t seed = 0;
    int requested_workers = 1;
    int observed_workers = 1;
    std::uint64_t physical_layout_evaluations = 0;
    double best_efficiency_percent = 0.0;
    double maximum_constraint_violation = 0.0;
    double minimum_spacing_m = 0.0;
    double initialization_seconds = 0.0;
    double evaluator_seconds = 0.0;
    double constraint_seconds = 0.0;
    double optimizer_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<Point> best_layout;
    std::vector<StartReceipt> start_receipts;
};

class Problem {
public:
    explicit Problem(std::string case_id);

    [[nodiscard]] const PaperCase& paper_case() const noexcept;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout,
        bool gradient,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] ConstraintReceipt barrier(
        const std::vector<Point>& layout,
        double barrier_weight,
        bool gradient,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] std::vector<Point> initial_layout(
        StartPolicy policy,
        std::uint64_t seed,
        int start_index
    ) const;
    [[nodiscard]] bool feasible(const std::vector<Point>& layout) const;
    [[nodiscard]] double minimum_spacing(
        const std::vector<Point>& layout
    ) const;

private:
    struct Data;
    std::shared_ptr<const Data> data_;
};

[[nodiscard]] OptimizationReceipt optimize(
    const Problem& problem,
    const OptimizationConfig& config
);

[[nodiscard]] std::vector<std::string> paper_case_ids();
[[nodiscard]] const char* start_policy_name(StartPolicy policy) noexcept;

}  // namespace core99::t08
