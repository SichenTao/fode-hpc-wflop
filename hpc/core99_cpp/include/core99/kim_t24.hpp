/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T24 Markov-intermittency multi-objective WFLOP and
NSGA-III API
Paper/DOI: Optimization of a Wind Farm Layout to Mitigate the Wind Power
Intermittency; 10.1016/j.apenergy.2024.123383
Public source: exact-title, DOI, author, arXiv, and GitHub searches on
2026-07-31 found the publisher paper and an APS abstract, but no paper-linked
optimizer, wake evaluator, 2002--2022 Marado wind data, Markov arrays, layouts,
or result-replay source. The paper states that no data were used, although
its method section says hourly Korea Meteorological Administration wind data
were used; both facts are preserved as a paper-internal provenance conflict.
Provided paper assets: Gaussian wake and linear local-inlet wake-merging
Eqs. (1)--(6), Markov stationary distribution and intermittency Eqs. (21)--
(22), two-objective WFLOP Eqs. (23)--(26), NSGA-III operators Eqs. (27)--
(28), V112 curves, six real-scale optimization cases, a three-turbine model
problem, population 92, 91 reference intervals, crossover probability one,
per-coordinate mutation probability 1/(2N), and the hypervolume criterion
Missing assets: in-house code and random states; raw hourly Marado data,
machine-readable 144-state probabilities and Markov matrices, numerical
V112 curves and rotor quadrature, initialization and constraint repair,
random-matrix laws, mutation distribution index, hypervolume reference and
maximum stopping limit, repeat count, raw fronts, and optimized layouts
Paper/source conflicts: the data-availability statement says no data were
used while Section 3.1 explicitly uses hourly 2002--2022 KMA Marado data
Resolution and reconstruction: paper equations control. Figure-constrained
nine-speed probabilities and sixteen-direction real-wind probabilities are
frozen; a reversible local Markov kernel with 24.8-degree directional scale
reconstructs Fig. 9 and exactly preserves each declared stationary wind rose.
Uniform wind changes only direction stationarity. V112 Fig. 5 is digitized
at integer speeds, eight equal-area rotor samples complete the area integral,
and deterministic repair enforces the square and >3D spacing. Elementwise
uniform crossover implements Eq. (27); Deb polynomial mutation with index 20
completes Eq. (28). Two-objective NSGA-III uses 92 points over 91 intervals.
The hypervolume reference and history test are not uniquely recoverable, so
the formal profile freezes the paper-visible 1000-generation minimum rather
than claiming the missing dynamic stop; a declared 2000-generation ceiling
is exposed for sensitivity runs. Formal results use 25 platform seeds for
every one of the six paper cases.
Target method: real-coded constrained NSGA-III with reference-line niching
Target problems: uniform and reconstructed real winds, each with power-change
thresholds of 0%, 7%, and 15% of the 75 MW rated farm; the three-turbine
model problem is retained as a physics validation fixture
Method/problem semantic IDs:
t24_nsga3_markov_intermittency_declared_reconstruction_v1;
t24_kim_markov_intermittency_six_case_v1
Controlling contract: shared/contracts/core99_t24_kim_2024.json
Production backend: pure C++20 CPU-HPC. Wind geometry is reused across nine
speeds per direction; immutable rotor, turbine, stationary-wind, sparse
transition, and reference-line data are precomputed. Initialization,
offspring construction, complete-layout evaluation, dominance rows, and
association work use one persistent all-core team. Counter-keyed random
events and fixed reductions preserve one/all-core scientific trajectories.
Claim boundary: academic flexible paper-equation and figure-constrained
reconstruction of NSGA-III and every paper optimization problem, not author
code, original Marado data, private arrays, random states, or numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t24 {

enum class CaseId {
    uniform_p0,
    uniform_p007,
    uniform_p015,
    real_p0,
    real_p007,
    real_p015,
};

struct Turbine {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct Evaluation {
    double mean_power_mw = 0.0;
    double intermittency_mw = 0.0;
    double spacing_violation_m = 0.0;
    double boundary_violation_m = 0.0;
    bool feasible = false;
};

struct FrontPoint {
    double mean_power_mw = 0.0;
    double intermittency_mw = 0.0;
    std::vector<Turbine> layout;
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    int workers = 20;
    int population = 92;
    int generations = 1000;
};

struct RunResult {
    std::string problem_id;
    std::string problem_semantic_id =
        "t24_kim_markov_intermittency_six_case_v1";
    std::string method_semantic_id =
        "t24_nsga3_markov_intermittency_declared_reconstruction_v1";
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
    [[nodiscard]] int paper_reference_intervals() const noexcept;
    [[nodiscard]] int paper_minimum_generations() const noexcept;
    [[nodiscard]] int declared_maximum_generations() const noexcept;
    [[nodiscard]] int declared_repeats() const noexcept;
    [[nodiscard]] double threshold_fraction() const noexcept;
    [[nodiscard]] double side_length_m() const noexcept;
    [[nodiscard]] double rotor_diameter_m() const noexcept;
    [[nodiscard]] bool real_wind() const noexcept;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Turbine>& layout
    ) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_population(
        const std::vector<std::vector<Turbine>>& layouts,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] std::vector<Turbine> reference_layout() const;
    void repair(
        std::vector<Turbine>& layout,
        std::uint64_t seed,
        std::uint64_t generation,
        std::uint64_t individual
    ) const;
    [[nodiscard]] double model_problem_power_mw(
        double upstream_y_over_d,
        double speed_mps,
        double direction_deg
    ) const;

private:
    struct WindState {
        double direction_deg = 0.0;
        double speed_mps = 0.0;
        double probability = 0.0;
    };
    struct Transition {
        int from = 0;
        int to = 0;
        double joint_probability = 0.0;
    };

    [[nodiscard]] double power_mw(double speed_mps) const;
    [[nodiscard]] double thrust(double speed_mps) const;
    [[nodiscard]] std::vector<double> state_powers(
        const std::vector<Turbine>& layout
    ) const;
    void build_wind_contract();

    CaseId case_id_;
    std::string id_;
    bool real_wind_ = false;
    double threshold_fraction_ = 0.0;
    std::vector<WindState> winds_;
    std::vector<Transition> transitions_;
};

[[nodiscard]] RunResult run(
    const Problem& problem,
    const RunConfig& config
);

}  // namespace core99::t24
