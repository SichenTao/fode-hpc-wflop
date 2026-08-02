/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T81 inhomogeneous-wave-load offshore WFLOP and
Multistart-SLSQP academic reconstruction API
Paper title: Layout Optimization of Offshore Wind Farm Considering
Spatially Inhomogeneous Wave Loads
Paper DOI: 10.1016/j.apenergy.2021.117947
Paper-provided assets: two-stage AEP/wave-load formulation; 24 wind sectors;
Gauss wake and RSS combination; NREL 5 MW turbine, 90 m hub, 126 m rotor and
6 m monopile; 2D spacing; 10--15 pseudo-random SLSQP starts; 16-turbine
3 km by 3 km mild-slope Case 1; 20-turbine polygonal complex-seabed Case 2;
four alpha0 values 0.99, 0.98, 0.95 and 0; analytical shoaling/refraction,
Stokes/Morison/breaking-load equations; three Case-2 extreme wave states.
Public source: exact-title, DOI, author, GitHub and laboratory searches on
2026-07-31 located no target source or native arrays. The paper cites FLORIS
2.4, https://github.com/NREL/floris, whose v2.4 tag supplies the NREL 5 MW
CP/CT and legacy Gauss lineage, but not this target implementation or data.
Open replacement dependency: pinned NLopt v2.11.0 commit
88c424d4f458412787df96fcc95218acbca224fd supplies C++ Kraft SLSQP in
place of SciPy's wrapper around the same algorithm family.
Missing information: target Python source; SciPy version, SLSQP tolerances,
finite-difference step and maximum evaluations; random states; native NCEP
wind array; Case-2 polygon coordinates, bathymetry, 75,737-element mesh,
MIKE21/SWAN model setup and wave-load chart; exact Case-1 chart samples;
optimized coordinates and per-start histories.
Reconstruction: retain FLORIS-v2.4 NREL 5 MW CP/CT and legacy-Gauss
parameters; use paper formulas for Case-1 depth, dispersion, shoaling,
Stokes/Morison and breaking load with a declared smooth figure-informed
breaker completion; digitize the two wind roses and Case-2 polygon/load
topology deterministically; use bound-aware central finite differences with
one-sided completion at an active bound, pinned open
SLSQP, 15 starts, 1e-7 relative x tolerance and 300 evaluations per start.
The paper's precomputed wave-load-chart design is retained as a 0.25 m
Case-1 linear table and 25 m Case-2 bilinear table; optimization evaluations
therefore interpolate the frozen physical chart rather than recomputing the
dispersion/Morison construction for every turbine and finite difference.
Method semantic ID: t81_multistart_slsqp_wave_aep_declared_v1
Problem semantic ID: t81_twofarm_inhomogeneous_wave_declared_v1
Protocol semantic ID: t81_twofarm_4alpha_25seed_15start_v1
Production backend: pure C++20 CPU-HPC. Independent SLSQP starts are the
primary parallel axis; each start uses schedule-independent physics and the
same finite-difference event order. Formal orchestration fills all Waffle
cores across paper cases/seeds without nested oversubscription.
Controlling contract: shared/contracts/core99_t81_ti_2022.json
Claim boundary: academic flexible reconstruction, not author code, SciPy
trajectory, native bathymetry/wind/wave arrays, MIKE21 replay, or numerical
optimum replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t81 {

enum class CaseRole { mild_slope, complex_terrain };

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct WindState {
    double direction_deg = 0.0;
    double speed_mps = 0.0;
    double probability = 0.0;
};

struct Evaluation {
    double aep_gwh = 0.0;
    double total_wave_load_index = 0.0;
    double minimum_spacing_m = 0.0;
    double spacing_violation_m = 0.0;
    double boundary_violation_m = 0.0;
    bool feasible = false;
};

struct StageResult {
    std::string stage_id;
    double alpha0 = 1.0;
    int multistarts = 0;
    int successful_starts = 0;
    std::uint64_t physical_fes = 0;
    double seconds = 0.0;
    std::vector<Point> best_layout;
    Evaluation best_evaluation;
    double beta = 1.0;
    double alpha1 = 1.0;
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    int workers = 20;
    int multistarts = 15;
    int maximum_evaluations_per_start = 300;
    double relative_x_tolerance = 1.0e-7;
    std::vector<double> alpha0_values{0.99, 0.98, 0.95, 0.0};
};

struct RunResult {
    std::string case_id;
    std::string method_semantic_id =
        "t81_multistart_slsqp_wave_aep_declared_v1";
    std::string problem_semantic_id =
        "t81_twofarm_inhomogeneous_wave_declared_v1";
    std::string protocol_semantic_id =
        "t81_twofarm_4alpha_25seed_15start_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t physical_fes = 0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<StageResult> stages;
};

class Problem {
public:
    explicit Problem(CaseRole role);

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] CaseRole role() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] const std::vector<WindState>& wind_states() const noexcept;
    [[nodiscard]] const std::vector<Point>& boundary_polygon() const noexcept;
    [[nodiscard]] std::vector<Point> reference_layout() const;
    [[nodiscard]] std::vector<Point> reconstructed_start(
        int start_index,
        std::uint64_t seed
    ) const;
    [[nodiscard]] Evaluation evaluate(const std::vector<Point>& layout) const;
    [[nodiscard]] double wave_load_at(const Point& point) const;

private:
    CaseRole role_;
    std::string id_;
    int turbine_count_ = 0;
    std::vector<WindState> wind_states_;
    std::vector<Point> boundary_polygon_;
    double wave_chart_step_x_m_ = 0.0;
    double wave_chart_step_y_m_ = 0.0;
    int wave_chart_columns_ = 0;
    int wave_chart_rows_ = 0;
    std::vector<double> wave_chart_;
};

[[nodiscard]] std::vector<std::string> paper_case_ids();
[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config = {});

}  // namespace core99::t81
