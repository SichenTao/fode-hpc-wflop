/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T76 directional-spacing, heterogeneous-turbine WFLOP
and MPGA academic reconstruction API
Paper title: Investigation into Spacing Restriction and Layout Optimization
of Wind Farm with Multiple Types of Wind Turbines
Paper DOI: 10.1016/j.energy.2018.11.073
Public source: exact-title, DOI, author, GitHub, laboratory, and paper-link
searches on 2026-07-31 located no target MATLAB source or native arrays.
Cited predecessor/lineage: Wind turbine layout optimization using
multi-population genetic algorithm and a case study in Hong Kong offshore,
10.1016/j.jweia.2015.01.018; and the audited project T62 MPGA contract.
Paper-provided facts: 4 km square; four cases; five ENERCON types with
published rated power, diameter, cut-in/rated/cut-out speed and hub height;
omnidirectional 5D and downstream 5D/crosswind 3D or 2.5D directional
restrictions; partial-overlap 2-D Jensen/RSS wake; 10 MPGA populations,
20-bit variables, mutation 0.001--0.05, crossover 0.7--0.9, and 500
unchanged-generation termination.
Missing information: target code; numerical manufacturer power and CP
curves; axial induction and wake-decay values; exact MPGA population size,
selection, crossover form, migration schedule and safety maximum; random
states/repeats; native Sha Chau 2001--2011 hourly records, joint frequency
array and optimum coordinates.
Paper conflicts: “number of individuals=48” denotes turbines rather than
chromosomes; Table 1 gives E-44 hub height 45 m while Case 3 prose says 88 m;
the printed Case-3 hub-height speeds correspond to alpha=0.40, not offshore
alpha=0.10; directional crosswind distance is described as 3D while Fig. 4
shows a total 3D width (plus/minus 1.5D).
Reconstruction: use paper Table-1 values; anchor monotone cubic energy-law power
completions to all Case-3 theoretical outputs; derive one type-specific axial
induction from the anchor CP; use exact circle overlap and classic
Jensen expansion with the lineage-standard k=0.075; use Table-1 E-44 height and the Case-3
alpha=0.40 result; digitize the Figure-21 speed marginal and reconstruct
Figure-20's three direction modes deterministically; interpret crosswind
ratios as total restricted widths. Reuse T62 lineage defaults of 20
chromosomes per deme, tournament-2, one-point crossover, one elite per deme,
and 20-generation ring migration; use 5000 generations only as a declared
fail-safe.
Method semantic ID: t76_mpga_directional_heterogeneous_declared_v1
Problem semantic ID: t76_fourcase_directional_multitype_declared_v1
Protocol semantic ID: t76_fourcase_25seed_500stall_v1
Production backend: pure C++20 CPU-HPC. One persistent all-core team
parallelizes initialization, offspring construction, and complete layout
physics; direction geometry is shared across speed bins; counter-keyed events
and ordered elite/migration commits preserve one/all-core scientific identity.
Controlling contract: shared/contracts/core99_t76_sun_2019.json
Claim boundary: academic flexible reconstruction, not author code, native
power/wind arrays, original MATLAB stream, or numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t76 {

enum class Restriction { omnidirectional, directional };
enum class CaseRole {
    case1_omnidirectional_aligned,
    case1_directional_aligned,
    case2_omnidirectional_mpga,
    case2_directional_mpga,
    case3_directional_multitype_mpga,
    case4_sha_chau_multitype_mpga,
};

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct TurbineSpec {
    std::string name;
    double rated_power_kw = 0.0;
    double diameter_m = 0.0;
    double cut_in_mps = 0.0;
    double rated_speed_mps = 0.0;
    double cut_out_mps = 0.0;
    double hub_height_m = 0.0;
    double anchor_speed_mps = 0.0;
    double anchor_power_kw = 0.0;
};

struct WindState {
    double direction_deg = 0.0;
    double reference_speed_mps = 0.0;
    double probability = 0.0;
};

struct Evaluation {
    double expected_power_mw = 0.0;
    double theoretical_no_wake_power_mw = 0.0;
    double utilization_rate = 0.0;
    double minimum_turbine_power_kw = 0.0;
    double maximum_turbine_power_kw = 0.0;
    std::uint64_t inactive_turbine_states = 0;
    double boundary_violation_m = 0.0;
    bool feasible = false;
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    int workers = 20;
    int demes = 10;
    int individuals_per_deme = 20;
    int unchanged_generations = 500;
    int maximum_generations = 5000;
    int migration_period = 20;
};

struct RunResult {
    std::string case_id;
    std::string method_semantic_id =
        "t76_mpga_directional_heterogeneous_declared_v1";
    std::string problem_semantic_id =
        "t76_fourcase_directional_multitype_declared_v1";
    std::string protocol_semantic_id =
        "t76_fourcase_25seed_500stall_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int demes = 0;
    int individuals_per_deme = 0;
    int generations = 0;
    int unchanged_generations = 0;
    std::uint64_t physical_fes = 0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<Point> best_layout;
    Evaluation best_evaluation;
};

class Problem {
public:
    explicit Problem(CaseRole role);

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] CaseRole role() const noexcept;
    [[nodiscard]] Restriction restriction() const noexcept;
    [[nodiscard]] bool optimized() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] double farm_side_m() const noexcept;
    [[nodiscard]] double directional_crosswind_ratio() const noexcept;
    [[nodiscard]] double reference_height_m() const noexcept;
    [[nodiscard]] double shear_exponent() const noexcept;
    [[nodiscard]] const std::vector<int>& turbine_types() const noexcept;
    [[nodiscard]] const std::vector<WindState>& wind_states() const noexcept;
    [[nodiscard]] std::vector<Point> aligned_layout() const;
    [[nodiscard]] std::vector<Point> decode(
        const std::vector<std::uint32_t>& genes
    ) const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout
    ) const;

private:
    std::string id_;
    CaseRole role_;
    Restriction restriction_;
    bool optimized_ = false;
    int turbine_count_ = 0;
    double crosswind_ratio_ = 3.0;
    double reference_height_m_ = 78.0;
    double shear_exponent_ = 0.0;
    std::vector<int> turbine_types_;
    std::vector<WindState> wind_states_;
    std::vector<double> direction_degrees_;
    std::vector<std::vector<int>> state_indices_by_direction_;
    std::vector<double> ambient_speed_by_state_type_;
    std::vector<double> no_wake_power_by_state_type_;
};

[[nodiscard]] const std::vector<TurbineSpec>& turbine_catalog();
[[nodiscard]] double completed_power_kw(
    const TurbineSpec& turbine,
    double speed_mps
);
[[nodiscard]] std::vector<std::string> paper_case_ids();
[[nodiscard]] std::string case_role_name(CaseRole role);
[[nodiscard]] RunResult run(
    const Problem& problem,
    const RunConfig& config = {}
);

}  // namespace core99::t76
