/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y16 regular offshore layout MILFP/BDA problem and method
Paper title: Regular Layout Optimization for Offshore Wind Farms Considering
Seabed Terrains and Turbulence Intensity
Paper DOI: 10.1109/TSTE.2026.3686029. Target PDF SHA-256:
4288399ae7711a6fc1acbbfba5d5c510c5f56a8cb820e6d8d91e7f419d480e3c.
First-party supporting asset: CN121683298A/CN121683298B, application
CN202610193544.7A, inventors Xinwei Shen and Zehai Huang, public text at
https://patents.google.com/patent/CN121683298A/zh.
Public source search: exact-title, DOI, BDA phrase, author/institution and
GitHub searches on 2026-08-01 found no target code, solver model, site arrays,
wind arrays, terrain arrays, layouts, tolerances, traces or result archive.
Paper-provided facts: Eqs.1--69, BMM and IMM, 2D cosine Jensen wake,
Ishihara double-Gaussian added turbulence, piecewise-linear TI, support and
installation terrain costs, regular row patterns, 18 angles from 0 to 170
degrees, 10 patterns, BDA, Tables I--X and both Case-1/Case-2 studies.
Missing information: exact Zhuhai/Hainan boundaries, depth/soil rasters,
sixteen-scenario wind arrays, the ten pattern start/gap pairs, BDA tolerance and
iteration cap, Gurobi settings/model, original layout coordinates and seeds.
Paper conflicts/corrections: Table I calls 155/90/263 m rotor radii although
they are model-consistent rotor diameters; Eqs.13--14 omit the upstream
selection x_i from added turbulence; Eqs.12/51 omit the target-selection
factor and would force positive power at unselected targets; Eq.43 omits wind
scenario probabilities although Table IV fatigue values require weighting;
Table IX prose reports 1530/7237 s while its table reports 5530/16237 s.
Reconstruction: use the diameter interpretation; restore physical source-
target products in wake/TI terms and probability-weight TI; digitize declared
analytic figure proxies for site, terrain and wind; generate the first ten
safety-valid (start,gap) row patterns; use tolerance 1e-6 and 20 BDA iterations. These
corrections are explicit and never represented as author-original behavior.
Method semantic ID: y16_imm_bmm_bounded_dinkelbach_highs_reconstruction_v1.
Problem semantic ID: y16_regular_seabed_ti_lcoe_figure_proxy_v1.
Protocol semantic ID: y16_native_31role_deterministic_v1.
Production backend: pure C++20 CPU-HPC with persistent full-core coefficient
generation, independent angle-pattern MILFP tasks, immutable coefficient
blocks, ordered reduction and pinned HiGHS v1.15.1 commit
04024d701f79feb8e2f18bc3df0dffc04ef05088.
Controlling contract: shared/contracts/core99_y16_huang_2026.json.
Claim boundary: source-backed flexible academic reconstruction of the target
problem, BMM/IMM and BDA across every unique paper-native target role; not
author code, private site/wind/terrain data, Gurobi replay, global certificate
for private cases, random trajectory or numerical replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core99::y16 {

enum class ModelKind { bmm, imm };
enum class ObjectiveKind { minimum_lcoe, minimum_annual_cost, maximum_aep,
                           minimum_capital_lcoe };
enum class SiteKind { zhuhai_type1, zhuhai_type2, zhuhai_type3, hainan };

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct Turbine {
    std::string id;
    double diameter_m = 155.0;
    double hub_height_m = 95.0;
    double cut_in_mps = 3.0;
    double rated_mps = 10.1;
    double cut_out_mps = 25.0;
    double rated_power_kw = 5500.0;
    double cubic_power_coefficient = 5.34;
    double wake_expansion = 0.05;
    double thrust_coefficient = 0.88;
    double turbine_cost_cny = 19.0e6;
    double opex_cny_per_kw_year = 64.0;
    double base_install_cny = 2.5e6;
    double mud_depth_cny_per_m = 3.25e5;
    double rock_depth_cny_per_m = 5.75e5;
    double preventive_cny = 5.0e3;
    double corrective_cny = 1.35e6;
};

struct Scenario {
    std::string case_id;
    SiteKind site = SiteKind::zhuhai_type3;
    ModelKind model = ModelKind::imm;
    ObjectiveKind objective = ObjectiveKind::minimum_lcoe;
    Turbine turbine;
    int turbine_count = 40;
    double grid_spacing_diameters = 2.5;
    int ti_intervals = 3;
    bool expected_paper_infeasible = false;
    std::string paper_table_role;
};

struct Evaluation {
    double lcoe_cny_per_kwh = 0.0;
    double annual_cost_cny = 0.0;
    double capital_cost_cny = 0.0;
    double annual_energy_mwh = 0.0;
    double wake_loss_percent = 0.0;
    double support_cost_cny = 0.0;
    double installation_cost_cny = 0.0;
    double operation_maintenance_cost_cny = 0.0;
    double work_fatigue = 0.0;
    double disturbance_fatigue = 0.0;
    double minimum_spacing_m = 0.0;
    bool feasible = false;
};

struct RunConfig {
    int workers = 20;
    int angle_start = 0;
    int angle_count = 18;
    int pattern_start = 0;
    int pattern_count = 10;
    int maximum_bda_iterations = 20;
    double bda_tolerance = 1.0e-6;
    double mip_time_limit_seconds = 10000.0;
};

struct RunResult {
    std::string case_id;
    std::string method_semantic_id =
        "y16_imm_bmm_bounded_dinkelbach_highs_reconstruction_v1";
    std::string problem_semantic_id =
        "y16_regular_seabed_ti_lcoe_figure_proxy_v1";
    std::string protocol_semantic_id =
        "y16_native_31role_deterministic_v1";
    std::string status;
    std::string first_subproblem_status;
    int requested_workers = 0;
    int observed_workers = 0;
    int generated_subproblems = 0;
    int bound_feasible_subproblems = 0;
    int feasible_subproblems = 0;
    int solved_subproblems = 0;
    int incumbent_subproblems = 0;
    int evaluator_rejected_subproblems = 0;
    int pruned_subproblems = 0;
    int bda_iterations = 0;
    int selected_angle_degrees = -1;
    int selected_pattern = -1;
    double coefficient_seconds = 0.0;
    double mip_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    Evaluation evaluation;
    std::vector<Point> layout;
};

[[nodiscard]] std::vector<Scenario> paper_scenarios();
[[nodiscard]] RunResult run(const Scenario& scenario, const RunConfig& config);

}  // namespace core99::y16
