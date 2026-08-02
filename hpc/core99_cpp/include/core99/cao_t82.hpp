/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T82 power--wake-turbulence multi-objective WFLOP and
NSGA-II API
Paper/DOI: Wind Farm Layout Optimization to Minimize the Wake-Induced
Turbulence Effect on Wind Turbines; 10.1016/j.apenergy.2022.119599
Public source: exact-title, DOI, author, and GitHub searches on 2026-07-31
located no paper-linked source repository; the paper states that supporting
data are available from the corresponding author upon reasonable request
Provided paper assets: Eqs. (1)--(27), ideal-farm constants, ideal Case I and
Case II definitions, Zhuanghe boundary and turbine counts, plotted wind
resources, and aggregate result tables
Missing assets: author source and random states; NSGA-II population,
generation, crossover, mutation, and repeat settings; exact Case-II
probability matrix; Zhuanghe original coordinates, numerical wind-resource
arrays, turbine thrust/power curves, and optimizer traces
Paper conflicts: the Zhuanghe prose says two 2.3 MW turbines, but Fig. 19
labels them GW121/3000 and the stated 303.15 MW total is obtained only with
two 3.0 MW turbines; the self-consistent 3.0 MW interpretation controls
Resolution and reconstruction: the printed MTG and added-turbulence equations
control; rotor-area velocity deficit uses deterministic equal-area quadrature;
Case-II probabilities and Zhuanghe direction/turbulence curves are
deterministically digitized from Figs. 9, 20, and 21; a shape-2 Weibull law
with paper mean 6.9 m/s and six midpoint bins reconstructs Zhuanghe speeds;
unpublished turbine curves use declared cubic-to-rating curves and CT=0.8;
standard Deb SBX/polynomial mutation, population 100, twenty generations, and
25 independent seeds complete the missing NSGA-II protocol
Target method: real-coded elitist NSGA-II with constraint-preserving repair
Target problems: ideal 30-turbine single-state Case I; ideal 39-turbine
36-direction/three-speed Case II; and the 72-turbine, three-type Zhuanghe case
Method/problem semantic IDs:
t82_nsga2_mo_turbulence_declared_reconstruction_v1;
t82_cao_power_turbulence_three_case_v1
Controlling contract: shared/contracts/core99_t82_cao_2022.json
Production backend: pure C++20 CPU-HPC; immutable wind constants and rotor
quadrature are precomputed, offspring construction, population evaluation,
and dominance work use one persistent all-core worker team, and random events
are counter-keyed so one/all-core scientific trajectories are identical
Claim boundary: academic flexible declared reconstruction of every paper
problem and target method, not author-code, private-data, or numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t82 {

enum class CaseId {
    ideal_single,
    ideal_multi,
    zhuanghe,
};

struct Turbine {
    double x_m = 0.0;
    double y_m = 0.0;
    int type = 0;
};

struct Evaluation {
    double expected_power_kw = 0.0;
    double maximum_comprehensive_turbulence = 0.0;
    double spacing_violation_m = 0.0;
    double boundary_violation_m = 0.0;
    bool feasible = false;
};

struct FrontPoint {
    double expected_power_kw = 0.0;
    double maximum_comprehensive_turbulence = 0.0;
    std::vector<Turbine> layout;
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    int workers = 20;
    int population = 100;
    int generations = 20;
};

struct RunResult {
    std::string problem_id;
    std::string problem_semantic_id =
        "t82_cao_power_turbulence_three_case_v1";
    std::string method_semantic_id =
        "t82_nsga2_mo_turbulence_declared_reconstruction_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int population = 0;
    int generations = 0;
    std::uint64_t physical_fes = 0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<FrontPoint> front;
};

class Problem {
public:
    explicit Problem(CaseId id);

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] CaseId case_id() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int wind_state_count() const noexcept;
    [[nodiscard]] int paper_population() const noexcept;
    [[nodiscard]] int paper_generations() const noexcept;
    [[nodiscard]] int paper_repeats() const noexcept;
    [[nodiscard]] bool inside(double x_m, double y_m) const noexcept;
    [[nodiscard]] double diameter(int type) const noexcept;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Turbine>& layout
    ) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_population(
        const std::vector<std::vector<Turbine>>& layouts,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] std::vector<Turbine> reference_layout() const;

private:
    struct WindState {
        double direction_deg = 0.0;
        double speed_mps = 0.0;
        double ambient_turbulence = 0.1;
        double probability = 0.0;
    };
    struct TurbineType {
        double diameter_m = 40.0;
        double hub_height_m = 60.0;
        double thrust = 0.88;
        double rated_power_kw = 0.0;
        double rated_speed_mps = 12.0;
    };

    [[nodiscard]] double boundary_violation(
        double x_m, double y_m
    ) const noexcept;
    [[nodiscard]] double power_kw(int type, double speed_mps) const;
    void build_wind_states();

    CaseId case_id_;
    std::string id_;
    int turbine_count_ = 0;
    std::vector<TurbineType> turbine_types_;
    std::vector<WindState> wind_states_;
};

[[nodiscard]] RunResult run(
    const Problem& problem,
    const RunConfig& config
);

}  // namespace core99::t82
