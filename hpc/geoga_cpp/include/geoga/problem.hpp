/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: GeoGA Anholt-structured declared problem interface
Paper title: A Geometric Mutation-Based Genetic Algorithm for Irregular Large-Scale Offshore Wind Farm Layout Optimization
DOI: 10.1109/CBD69312.2025.00059
Public asset/source: no author implementation or numerical Anholt data found; evidence dossier docs/source-dossiers/L0726.json
Missing information: original Anholt boundary, candidate set, wind arrays, turbine curves, and author implementation
Reconstruction: geoga_anholt_structured_declared_proxy_v1 freezes every declared P3 completion below
Paper-preserved fields: irregular planar boundary, 111 turbines, approximately 180 candidates, 5D spacing, twelve joint wind bins, 4.2 MW turbine surface scalars, Jensen wakes with sum-of-squares overlap, and AEP objective
Declared P3 completions: synthetic polygon, deterministic capped Bridson sampling, frozen wind tuples, cubic-to-rated power curve, constant thrust coefficient, wake expansion coefficient, coordinate convention, and stable summation order
Problem evidence tier: P3_DECLARED_PROXY
Problem semantic ID: geoga_anholt_structured_declared_proxy_v1
Controlling contract: shared/contracts/geoga_anholt_structured_declared_proxy_contract.json
Claim boundary: no original Anholt boundary, candidate set, actual layout, wind arrays, curves, reported AEP, or ranking claim
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace geoga {

inline constexpr const char* kProblemSemanticId =
    "geoga_anholt_structured_declared_proxy_v1";

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct WindBin {
    double flow_to_degrees = 0.0;
    double free_speed_mps = 0.0;
    double probability = 0.0;
};

struct Problem {
    std::string case_id;
    int turbine_count = 111;
    int target_candidate_count = 180;
    double rotor_diameter_m = 117.0;
    double hub_height_m = 91.5;
    double rated_power_kw = 4200.0;
    double cut_in_speed_mps = 3.0;
    double rated_speed_mps = 12.0;
    double cut_out_speed_mps = 25.0;
    double thrust_coefficient = 0.8;
    double wake_expansion = 0.05;
    double air_density_kg_m3 = 1.225;
    double roughness_length_m = 0.00025;
    double turbulence_intensity = 0.10;
    double minimum_spacing_m = 585.0;
    int poisson_max_trials = 30;
    std::uint64_t poisson_seed = 20250726;
    std::vector<Point> boundary;
    std::vector<WindBin> wind_bins;
    std::vector<Point> candidates;
};

struct LayoutEvaluation {
    double aep_kwh = 0.0;
    double no_wake_aep_kwh = 0.0;
    double capacity_factor = 0.0;
};

Problem load_problem(const std::string& path);
bool point_in_boundary(const Problem& problem, const Point& point);
double minimum_candidate_spacing_m(const Problem& problem);
double turbine_power_kw(const Problem& problem, double speed_mps);
double single_wake_deficit_fraction(
    const Problem& problem,
    double downstream_distance_m,
    double crosswind_distance_m
);
LayoutEvaluation evaluate_layout(
    const Problem& problem,
    const std::vector<int>& layout_0based
);
std::string problem_semantic_hash(const Problem& problem);
std::string layout_hash(const std::vector<int>& layout_0based);

}  // namespace geoga
