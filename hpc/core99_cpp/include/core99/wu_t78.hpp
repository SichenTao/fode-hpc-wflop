/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T78 onshore layout/noise problem and traditional PSO API
Paper title: Optimizing the Layout of Onshore Wind Farms to Minimize Noise
Paper DOI: 10.1016/j.apenergy.2020.114896
Paper-provided assets: Hubbard inflow/turbulent-boundary-layer noise equations,
octave-band A weighting and logarithmic source superposition; partial-overlap
Jensen wakes with linear multiple-deficit superposition and k=0.07; MPPT power;
80 DTU 10 MW turbines; the 12,864 m by 10,401 m construction region; a 500 m
square observation zone centred at (5750,5750); 4D spacing; L10 and 45 dBA;
10,000 kWh/dBA compensation; traditional PSO equations; 200 iterations; two
paper cases and 10 trials per case; reference and optimized result anchors.
Public source: exact-title, DOI, author and GitHub repository searches on
2026-07-31 located no target source or machine-readable target arrays. NASA
NTRS record 19910070917 confirms the cited Hubbard/Shepherd aeroacoustic
source but provides no downloadable implementation.
Missing information: target code and random states; numerical Norwegian wind
time series; exact FINO3 coordinate array; observation-zone quantization m;
DTU blade/noise constants B, phi, C0.7, sigma, S, h, chord, delta-l, Ka and Kb;
Cp, Ct and rated rotor speed; PSO population, c1/c2 values, velocity bounds and
initialization; numerical infinite penalties.
Paper/source conflicts: the text calls FINO3 the reference wind farm although
FINO3 is an offshore research platform while the study is framed as onshore;
the printed A-weighting equation uses natural-log typography although acoustic
level accumulation elsewhere uses base-10 logarithms.
Reconstruction: digitize the 80 plotted reference coordinates and plotted
12-direction/five-speed wind rose; use a 5 by 5 observation grid; use DTU-10MW
power bounds, Ct=0.8, lambda-opt=7.5 and 9.6 rpm; retain the two Hubbard source
scalings through an octave A-weighted, wake-speed-dependent completion and
calibrate only its unavailable additive constants to the published 48.60 dBA
reference; calibrate the unavailable wind-series mass to the published
4015.17 GWh reference; use population 100, c1=c2=2, a 10-percent velocity
bound, reference-centred initialization and deterministic spacing repair. The
paper's unquantified "limited" slight-noise range is completed with the 50 dBA
upper value of its stated 45--50 dBA recommended range: compensation starts at
45 dBA and an economic-case hard constraint applies above 50 dBA.
IA-PSO is not reconstructed: the target explicitly says it is unchanged and
is used only to compare calculation time, so it is a non-contribution baseline.
Method semantic ID: t78_traditional_pso_declared_v1
Problem semantic ID: t78_fino3_noise_layout_two_case_declared_v1
Protocol semantic ID: t78_native_2x10_repeat_declared_v1
Production backend: pure C++20 CPU-HPC. One persistent all-core team performs
population evaluation and particle update; the evaluator reuses each
direction's wake geometry across its five speed bins, each layout's receiver
distances across 60 states, and each turbine-state source spectrum across 25
receivers; SIMD reductions cover the remaining turbine sums. Counter-keyed
events and ordered best updates preserve one/all-worker scientific identity.
Each formal run uses all Waffle cores; paper-native repeats are executed
sequentially to avoid oversubscription.
Controlling contract: shared/contracts/core99_t78_wu_2020.json
Claim boundary: academic flexible reconstruction of the target traditional
PSO and both paper-native noise-layout problems, not author code, native wind,
layout/noise arrays, IA-PSO, random stream, or exact numerical optimum replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t78 {

enum class Role {
    strict_noise_control,
    economic_compensation,
};

struct WindState {
    double direction_deg = 0.0;
    double speed_mps = 0.0;
    double probability = 0.0;
};

struct Evaluation {
    double annual_energy_gwh = 0.0;
    double maximum_l10_dba = 0.0;
    double excess_noise_dba = 0.0;
    double minimum_spacing_m = 0.0;
    double spacing_violation_m = 0.0;
    double hard_noise_violation_dba = 0.0;
    double noise_penalty_gwh = 0.0;
    double constraint_penalty_gwh = 0.0;
    double objective_gwh = 0.0;
    bool feasible = false;
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    int workers = 20;
    int population_override = 0;
    int iteration_override = 0;
};

struct RunResult {
    std::string case_id;
    std::string method_semantic_id = "t78_traditional_pso_declared_v1";
    std::string problem_semantic_id =
        "t78_fino3_noise_layout_two_case_declared_v1";
    std::string protocol_semantic_id = "t78_native_2x10_repeat_declared_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int population_size = 0;
    int generations = 0;
    int dimensions = 160;
    std::uint64_t physical_fes = 0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<double> best_decision;
    Evaluation best_evaluation;
};

class Problem {
public:
    explicit Problem(Role role);

    [[nodiscard]] Role role() const noexcept;
    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] int dimensions() const noexcept;
    [[nodiscard]] int population_size() const noexcept;
    [[nodiscard]] int maximum_iterations() const noexcept;
    [[nodiscard]] int paper_repeats() const noexcept;
    [[nodiscard]] const std::vector<double>& lower_bounds() const noexcept;
    [[nodiscard]] const std::vector<double>& upper_bounds() const noexcept;
    [[nodiscard]] const std::vector<WindState>& wind_states() const noexcept;
    [[nodiscard]] std::vector<double> reference_decision() const;
    [[nodiscard]] Evaluation evaluate(const std::vector<double>& decision) const;

private:
    Role role_;
    std::string id_;
    std::vector<double> lower_bounds_;
    std::vector<double> upper_bounds_;
    std::vector<WindState> wind_states_;
    double power_scale_ = 1.0;
    double noise_offset_dba_ = 0.0;
};

[[nodiscard]] std::vector<std::string> paper_case_ids();
[[nodiscard]] std::string role_name(Role role);
[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config = {});

}  // namespace core99::t78
