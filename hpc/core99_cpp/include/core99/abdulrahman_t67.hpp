/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T67 commercial-turbine, variable-height WFLOP and
single-objective MATLAB-ga semantic reconstruction API
Paper title: Investigating the Power-COE Trade-Off for Wind Farm Layout
Optimization Considering Commercial Turbine Selection and Hub Height Variation
Paper DOI: 10.1016/j.renene.2016.10.038
Public source: exact-title, DOI, author, GitHub, and laboratory searches on
2026-07-31 located no target MATLAB code, 61-turbine table, fitted curve
coefficients, random states, or result layouts.
Related public source: https://github.com/NatLabRockies/SAM provides a
BSD-3-Clause commercial wind-turbine library and is used only as an
independent range reference, not as author data.
Paper-provided facts: MATLAB ga; 61 commercial turbines sorted by rated
power; fifth-degree manufacturer-power fits; a fifth-degree generic CT(CPe)
fit; hub heights 80--140 m; six-turbine line and fixed 18-turbine array and
staggered layouts; spacing multipliers 3/4/5; 8/10/12 m/s at 60 m; onshore
z0=0.3 and offshore z0=0.0002; Jensen/Frandsen partial-overlap wakes; total
power, capacity factor, and total-cost-index/output-power optimized
separately; at most 3000 generations and TolFun=1e-15.
Missing information: target source; 55 of 61 turbine identities and all
complete manufacturer curves; all fifth-degree power and CT coefficients;
cut-in/out values by turbine; exact partial-overlap corner convention; MATLAB
release/options, population size, elite/crossover/mutation settings, stall
window, seeds, repeats, and machine-readable optima.
Paper/source conflicts: the text calls the problem high-dimensional but
reports 18 TIL and 36 SWF variables; SWF paragraph reverses row/column
terminology relative to its 672 m crosswind and LY dimensions.
Reconstruction and completion: retain the paper's 18/36 decision variables;
anchor the six published turbine rows exactly and complete the other 55 by a
deterministic rated-power-sorted catalog spanning the paper ranges; use a
monotone smoothstep fifth-degree curve and a bounded fifth-degree CT(CPe)
completion; orient three crosswind columns by six downwind rows; use
population 256 and MATLAB-ga-lineage defaults (elite 5 percent, scattered
crossover 0.8, bounded per-gene mixed-variable mutation, 50-generation
stall).
Method semantic ID: t67_matlab_ga_mixed_turbine_height_declared_v1
Problem semantic ID: t67_til_swf_power_cf_tciop_162role_declared_v1
Protocol semantic ID: t67_3000gen_25seed_162role_v1
Production backend: pure C++20 CPU-HPC. One persistent all-core team
parallelizes complete population physics, offspring construction, and
independent fitness work; counter-keyed events and ordered elitist commits
preserve one/all-worker scientific identity.
Controlling contract: shared/contracts/core99_t67_abdulrahman_2017.json
Claim boundary: academic flexible paper reconstruction, not author code,
native 61-turbine curves, original MATLAB stream, or numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t67 {

enum class Terrain { onshore, offshore };
enum class LayoutKind { turbine_in_line, array, staggered };
enum class Objective { maximum_power, maximum_capacity_factor, minimum_tciop };

struct TurbineSpec {
    int code = 0;
    double rated_power_mw = 0.0;
    double diameter_m = 0.0;
    double rated_speed_mps = 0.0;
    double cut_in_mps = 3.0;
    double cut_out_mps = 25.0;
};

struct Decision {
    std::vector<double> y_m;
    std::vector<int> turbine_code;
    std::vector<int> hub_height_m;
};

struct Evaluation {
    double total_power_mw = 0.0;
    double rated_power_mw = 0.0;
    double capacity_factor = 0.0;
    double total_cost_index = 0.0;
    double total_cost_index_per_output_power = 0.0;
    double minimum_spacing_margin_m = 0.0;
    bool feasible = false;
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    int workers = 20;
    int population_size = 256;
    int maximum_generations = 3000;
    int stall_generations = 50;
    double tolerance_function = 1.0e-15;
};

struct RunResult {
    std::string case_id;
    std::string method_semantic_id =
        "t67_matlab_ga_mixed_turbine_height_declared_v1";
    std::string problem_semantic_id =
        "t67_til_swf_power_cf_tciop_162role_declared_v1";
    std::string protocol_semantic_id =
        "t67_3000gen_25seed_162role_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int population_size = 0;
    int generations = 0;
    std::uint64_t physical_fes = 0;
    bool converged = false;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    Decision best_decision;
    Evaluation best_evaluation;
};

class Problem {
public:
    Problem(
        LayoutKind layout_kind,
        int spacing_multiplier,
        double reference_speed_mps,
        Terrain terrain,
        Objective objective
    );

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] LayoutKind layout_kind() const noexcept;
    [[nodiscard]] int spacing_multiplier() const noexcept;
    [[nodiscard]] double reference_speed_mps() const noexcept;
    [[nodiscard]] Terrain terrain() const noexcept;
    [[nodiscard]] Objective objective() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] double length_y_m() const noexcept;
    [[nodiscard]] double length_x_m() const noexcept;
    [[nodiscard]] Decision reference_decision() const;
    [[nodiscard]] Evaluation evaluate(const Decision& decision) const;
    [[nodiscard]] double fitness(const Evaluation& evaluation) const;

private:
    std::string id_;
    LayoutKind layout_kind_;
    int spacing_multiplier_;
    double reference_speed_mps_;
    Terrain terrain_;
    Objective objective_;
    int turbine_count_;
    double length_y_m_;
    double length_x_m_;
};

[[nodiscard]] const std::vector<TurbineSpec>& turbine_catalog();
[[nodiscard]] RunResult run(
    const Problem& problem,
    const RunConfig& config = {}
);
[[nodiscard]] std::vector<std::string> paper_case_ids();
[[nodiscard]] std::string layout_kind_name(LayoutKind value);
[[nodiscard]] std::string terrain_name(Terrain value);
[[nodiscard]] std::string objective_name(Objective value);

}  // namespace core99::t67
