/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: PPGA Nantong-structured declared 3D problem interface
Paper title: Advanced 3D Wind Farm Layout Optimization Framework via Power-Law Perturbation-Based Genetic Algorithm
DOI: 10.1109/JAS.2025.125351
Public author problem source/data: unavailable as recorded in docs/source-dossiers/T43.json
Paper-preserved fields: 16 by 27 grid, 300 m spacing, H171-6.2MW surface parameters, four 16-direction by 7-speed scenarios, 20/30/40/50 turbines, 3D terrain-aware wake structure, and conversion-efficiency objective
Declared P3 completions: frozen analytic gentle seabed, factorized joint wind distributions, piecewise turbine power curve, axial induction one third, closed-form Gaussian deficit, Tao-2020 sum-of-squares multiple wakes, and deterministic ties
Problem evidence tier: P3_DECLARED_PROXY
Problem semantic ID: ppga_nantong_structured_3d_declared_proxy_v1
Controlling contract: shared/contracts/ppga_nantong_structured_3d_declared_proxy_contract.json
Claim boundary: distinct Nantong-structured engineering proxy only; original Nantong terrain, wind arrays, turbine curves, reported efficiencies, and author implementation remain blocked
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <string>
#include <vector>

namespace ppga {

inline constexpr const char* kProblemSemanticId =
    "ppga_nantong_structured_3d_declared_proxy_v1";

struct WindScenario {
    std::string id;
    std::vector<double> direction_probabilities;
    std::vector<double> speed_probabilities;
};

struct Problem {
    std::string case_id;
    std::string wind_scenario_id;
    int rows = 16;
    int cols = 27;
    int turbine_count = 20;
    double cell_width_m = 300.0;
    std::vector<double> wind_directions_rad;
    std::vector<double> wind_speeds_mps;
    WindScenario wind;
};

struct LayoutEvaluation {
    double expected_power_kw = 0.0;
    double ideal_expected_power_kw = 0.0;
    double conversion_efficiency = 0.0;
    double cost_per_expected_power = 0.0;
};

Problem load_problem(
    const std::string& path,
    const std::string& case_id
);
std::string problem_semantic_hash(const Problem& problem);
double foundation_elevation_m(const Problem& problem, int cell_1based);
double turbine_power_kw(double effective_speed_mps);
double single_wake_deficit_fraction(
    double downstream_distance_m,
    double crosswind_distance_m,
    double vertical_distance_m
);
LayoutEvaluation evaluate_layout(
    const Problem& problem,
    const std::vector<int>& layout_1based
);

}  // namespace ppga
