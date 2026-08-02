/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0368 real-seabed offshore layout problem and GA
Paper title: Genetic-algorithm-based layout optimization of an offshore wind
farm under real seabed terrain encountering an engineering cost model
Paper DOI: 10.1016/j.enconman.2021.114610. Target PDF SHA-256:
a81ca2a398ec6654af0755ca0f40dd64f206b0dd8a57cf244efe09cbe1701a24.
Public source search: exact-title, DOI, author, institution and GitHub searches
on 2026-08-01 found no target layout code, Nanao seabed/wind arrays, seeds,
traces or result archive. The target paper states that MATLAB/CPU GA took
about 4.7 h per case but does not publish its GA settings.
Cited same-author public asset: PyGAOWT/RBFNN-GA dataset DOI
10.17632/bvrdgykzwy.1, CC0, file URL
https://data.mendeley.com/public-files/datasets/bvrdgykzwy/files/
62e1546b-ac9e-4282-b9b2-8ce014e2b0b5/file_downloaded, archive SHA-256
0cc04a40d8d386f134af50d1a716cf708d4eb095c560181f1b626f32992866fe.
Its MATLAB GA scripts declare 500 generations, crossover fraction 0.3 and
population 50/100. It is completion evidence, not target-paper source.
Paper-provided facts: Eqs.1--11; Qian-Ishihara Gaussian wake reference;
engineering capital model; 2000 m square; 100 m rotor, 80 m hub, Ct 0.8888,
Cp 0.4, cut-in/out 4/20 m/s; 500 m separation; five S1--S5 seabeds; four
W1--W4 wind distributions; variable 0--25 turbine count; 20 native cases;
Tables 5--10, Figs.5--12 and one reported optimized layout per case.
Missing information: target source/random state; encoding; population and
operator details; rated power and ambient TI; exact S5 seabed and W1--W4
numeric arrays; Gaussian implementation details; stopping other than a
maximum generation; original layouts and reproducible numeric archive.
Conflicts/corrections: Eq.1 encodes an L-infinity square exclusion although
the prose claims physical 5D distance; the production problem restores the
Euclidean 5D norm and exposes the literal equation as an audit sensitivity.
Eq.7 defines transformer costs per MW but omits turbine count; full farm
rated capacity is restored. Eqs.9--10 make ICC implicit and are solved as
ICC=direct/(1-0.043-0.174). The reported COE is ICC divided by instantaneous
expected MW, not a lifecycle LCOE, so the implementation names it a
capital-to-power proxy. A zero-turbine individual makes Eq.11 undefined and
is excluded from evaluation although the prose gives the range 0--25.
Reconstruction: population100, 500 generations, crossover0.3 from the cited
same-author public scripts; rank selection, five elites, scattered crossover,
bounded adaptive coordinate/count mutation and deterministic spacing repair.
Use rated power2.3 MW as the Table-7 capital-scale completion and ambient TI
0.08. S1--S4 follow printed definitions; S5 is a frozen analytic terrain
proxy normalized to the printed 28.44 m mean and S4 standard deviation.
W1 is the printed dominant 12 m/s direction; W2--W4 are normalized analytic
digitizations preserving the visible direction/speed structure.
Method semantic ID: l0368_matlab_lineage_real_ga_declared_v1.
Problem semantic ID: l0368_seabed_engineering_capital_power_proxy_v1.
Protocol semantic ID: l0368_native_s1_s5_w1_w4_single_run_v1.
Production backend: pure C++20 CPU-HPC with immutable wind/cost constants,
one persistent full-core executor, parallel frozen-generation offspring and
fitness evaluation, fixed-index writes, ordered commits and counter-keyed
random events. Every individual owns a complete serial physics evaluation;
there is no nested team or oversubscription.
Controlling contract: shared/contracts/core99_l0368_liu_2021.json.
Claim boundary: source-backed flexible academic reconstruction of the target
problem, GA and all 20 paper-native cases; not author code, private Nanao
arrays, exact MATLAB defaults/random trajectory or numerical replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::l0368 {

enum class TerrainKind { s1_zero, s2_slope, s3_flat, s4_wave, s5_nanao_proxy };
enum class WindKind { w1_single, w2_directions, w3_speed_direction, w4_nanao_proxy };
enum class SpacingKind { euclidean_corrected, paper_linf_sensitivity };

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct WindState {
    double direction_degrees = 0.0;
    double speed_mps = 0.0;
    double probability = 0.0;
};

struct Scenario {
    std::string case_id;
    TerrainKind terrain = TerrainKind::s1_zero;
    WindKind wind = WindKind::w1_single;
    SpacingKind spacing = SpacingKind::euclidean_corrected;
    int paper_turbine_anchor = 0;
    double paper_capital_power_anchor_million_gbp_per_mw = 0.0;
    double paper_total_power_anchor_mw = 0.0;
    double paper_efficiency_anchor_percent = 0.0;
};

struct Evaluation {
    bool feasible = false;
    int turbine_count = 0;
    double capital_power_proxy_gbp_per_mw = 0.0;
    double initial_capital_cost_gbp = 0.0;
    double wind_turbine_cost_gbp = 0.0;
    double support_structure_cost_gbp = 0.0;
    double cable_substation_port_cost_gbp = 0.0;
    double expected_power_mw = 0.0;
    double no_wake_power_mw = 0.0;
    double efficiency_percent = 0.0;
    double minimum_distance_m = 0.0;
    double mean_water_depth_m = 0.0;
};

struct RunConfig {
    std::uint64_t seed = 36801;
    int workers = 20;
    int population = 100;
    int generations = 500;
    double crossover_fraction = 0.3;
    int elite_count = 5;
};

struct RunResult {
    std::string case_id;
    std::string method_semantic_id =
        "l0368_matlab_lineage_real_ga_declared_v1";
    std::string problem_semantic_id =
        "l0368_seabed_engineering_capital_power_proxy_v1";
    std::string protocol_semantic_id =
        "l0368_native_s1_s5_w1_w4_single_run_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t parallel_regions = 0;
    int population = 0;
    int generations = 0;
    std::uint64_t physical_fes = 0;
    Evaluation best_evaluation;
    std::vector<Point> best_layout;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    struct Impl;

    explicit Problem(Scenario scenario);
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] const Scenario& scenario() const noexcept;
    [[nodiscard]] const std::vector<WindState>& wind_states() const noexcept;
    [[nodiscard]] double water_depth_m(const Point& point) const noexcept;
    [[nodiscard]] Evaluation evaluate(const std::vector<Point>& layout) const;

private:
    std::unique_ptr<Impl> impl_;
    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config);
[[nodiscard]] std::vector<Scenario> paper_scenarios();
[[nodiscard]] std::string to_string(TerrainKind value);
[[nodiscard]] std::string to_string(WindKind value);
[[nodiscard]] std::string to_string(SpacingKind value);

}  // namespace core99::l0368
