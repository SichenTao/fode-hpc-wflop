/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y14 preference-oriented energy-noise WFLOP and p-SDRDE
Paper title: Preference-Oriented Evolutionary Multi-Objective Energy-Noise
Optimization for Fast Wind Farm Layout Design
Paper DOI: 10.1109/TSTE.2026.3661110. Target PDF SHA-256:
1479f6921738c9485f27e181d515e21fcc6e52df67a2d1df8a6bf40e55665a08.
Public asset search: exact-title, DOI, method-name, author, GitHub-code and
institutional searches on 2026-08-01 found no target source, measured wind
array, real layouts, receiver coordinates, seeds or numeric archive.
Paper-provided facts: continuous two-coordinate encoding; 4R spacing; Jensen
wake with RSS deficits; Gaussian SL113-3000/90 power curve; ISO-9613-2:2024
octave attenuation; maximum receiver SPL; p-SDRDE Algorithms 1--4; NP=50,
Nf=10, r=N/2, maxEvas=150000; three 16/24/48-turbine cases; original and
interactively adjusted reference points; ten independent runs.
Missing information: author code and random states; real layouts and measured
direction probabilities; direction-specific Weibull arrays; exact octave
source spectrum, atmospheric/ground settings and terrain; CR, learning-period
length, SFM overflow rule and several tie/order details.
Paper conflict: nomenclature calls K shape and C scale, but Fig.5's K=8.3,
C=2.0 curve is physically consistent only with scale=8.3 and shape=2.0.
Reconstruction: the plotted distribution controls; a declared normalized
16-sector digitization, hard-ground ISO engineering specialization, 105-dB
per-band interpretation, CR=0.9, LP=50, Laplace-smoothed rolling SFM and
deterministic tie rules complete missing data. Finite NSDE scaling samples
are clipped to [0.05,1.5] after the published Gaussian/Cauchy mixture.
Table-II turbine values,
Table-III sites/reference points and Table-VI adjusted points remain literal.
Method semantic ID: y14_psdrde_declared_reconstruction_v1.
Problem semantic ID: y14_energy_noise_threefarm_declared_proxy_v1.
Protocol semantic ID: y14_threefarm_two_preference_10seed_150k_v1.
Production backend: pure C++20 CPU-HPC with precomputed wind-power and acoustic
distance tables, one persistent all-core team, parallel frozen-generation
offspring construction/evaluation and parallel dominance rows. Counter-keyed
events and ordered commits require one/all-core scientific identity.
Controlling contract: shared/contracts/core99_y14_zhang_2026.json
Claim boundary: source-backed flexible academic reconstruction of every target
method component and six paper-native optimization roles; not author code,
private Gansu data, exact ISO site replay, random trajectory or numeric replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::y14 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct Scenario {
    std::string case_id;
    int turbine_count = 16;
    double length_m = 2500.0;
    double width_m = 4000.0;
    double reference_negative_aep_gwh = -138.229;
    double reference_spl_db = 45.0;
    bool adjusted_preference = false;
    double final_noise_weight = 0.5;
};

struct Evaluation {
    double negative_aep_gwh = 0.0;
    double spl_db = 0.0;
    double spacing_violation_m = 0.0;
    double boundary_violation_m = 0.0;
    bool feasible = false;
};

struct FrontPoint {
    Evaluation evaluation;
    std::vector<Point> layout;
};

struct RunConfig {
    std::uint64_t seed = 20260801;
    int workers = 20;
    int population = 50;
    int subpopulation = 10;
    std::uint64_t maximum_evaluation_slots = 150000;
    double crossover_rate = 0.9;
    int learning_period = 50;
};

struct RunResult {
    std::string case_id;
    std::string method_semantic_id =
        "y14_psdrde_declared_reconstruction_v1";
    std::string problem_semantic_id =
        "y14_energy_noise_threefarm_declared_proxy_v1";
    std::string protocol_semantic_id =
        "y14_threefarm_two_preference_10seed_150k_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int population = 0;
    int generations = 0;
    std::uint64_t nominal_evaluation_slots = 0;
    std::uint64_t physical_fes = 0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<FrontPoint> front;
};

class Problem {
public:
    explicit Problem(Scenario scenario);

    [[nodiscard]] const Scenario& scenario() const noexcept;
    [[nodiscard]] int receiver_count() const noexcept;
    [[nodiscard]] double rotor_radius_m() const noexcept;
    [[nodiscard]] double minimum_spacing_m() const noexcept;
    [[nodiscard]] const std::vector<double>& wind_probabilities() const noexcept;
    [[nodiscard]] std::vector<Point> reference_layout() const;
    [[nodiscard]] Evaluation evaluate(const std::vector<Point>& layout) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_population(
        const std::vector<std::vector<Point>>& layouts,
        fode::PersistentExecutor& executor
    ) const;

private:
    [[nodiscard]] double expected_power_kw(double deficit) const;
    [[nodiscard]] double source_noise_intensity(double distance_m) const;
    void build_receivers();
    void build_tables();

    Scenario scenario_;
    std::vector<Point> receivers_;
    std::vector<double> wind_probabilities_;
    std::vector<double> expected_power_table_;
    std::vector<double> source_noise_table_;
};

[[nodiscard]] std::vector<Scenario> paper_scenarios();
[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config);

}  // namespace core99::y14
