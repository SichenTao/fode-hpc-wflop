/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0079 Middelgrunden LCOE WFLOP with adaptive GA and PSO
Paper/DOI: Pillai et al., Application of an offshore wind farm layout
optimization methodology at Middelgrunden wind farm;
10.1016/j.oceaneng.2017.04.049. Target PDF SHA-256:
38f5f9b607d9d1d96fe72a6b1b7e57144691736a1dda368cd630762ebaacd863.
Public assets: final paper at https://strathprints.strath.ac.uk/60992/ and
Ajit C. Pillai's 2017 thesis at
https://era.ed.ac.uk/items/2547b970-79d8-4ec2-90a4-20e483ae8d7f.
Exact-title/DOI/author/code searches found no target-linked implementation,
native input bundle, random state or result archive. Pinned independent
OpenWAKE revision cdd23b9155e9d8181d6176a5702d167411d9bcc6 supplies the
20 as-built coordinates, public Middelgrunden wind rose, and Bonus B76 power
and thrust tables.
Paper-provided facts: 20 B76-2000 turbines; 175 m separation; first-order
G.C. Larsen wake with RSS superposition; 1 m/s and 30 degree bins; cable
loss/path/CMST, lifetime cost and LCOE; array, binary and continuous
constraint modes; adaptive GA and gBest PSO; population/swarm 100; maximum
1000 generations; 20 percent GA elitism; and source stopping criteria.
Missing assets: target SCADA, polygon, bathymetry, obstacles, port and cable
arrays; proprietary industry cost relationships and Gurobi model; author
random state; real-coded GA operator details; and tuned PSO coefficients.
Reconstruction: an elongated 5.7 km2 boundary centered on the pinned as-built
layout; target-count 100 m triangular candidates; pinned public wind/turbine
tables with one fixed as-built AEP calibration; a disclosed capacity-group
MST cable/cost surrogate anchored at the reported as-built lifetime cost;
direct-predecessor roulette/uniform GA semantics; and canonical gBest PSO
with inertia 0.9 to 0.4 and c1=c2=2.
Conflicts: the journal says 628 binary candidates while the later thesis says
658; both profiles are named and formal journal reproduction uses 628. Eq.2
prints 8766 hours; literal 8766 and calendar 8760 AEP are both emitted. The
printed PSO equation omits tuned values and canonical stochastic factors;
the declared completion above is never represented as an author value.
Every paper-native case is a single run: the paper's "three times" denotes
three constraint modes, and thesis Section 8.6 explicitly confirms one run
per case/optimizer/mode.
Method semantic IDs: l0079_adaptive_ga_three_encoding_declared_v1;
l0079_gbest_pso_three_encoding_declared_v1.
Problem semantic ID: l0079_middelgrunden_lcoe_three_constraint_declared_v1.
Controlling contract:
shared/contracts/core99_l0079_pillai_middelgrunden_2017.json
Production backend: pure C++20 CPU-HPC with immutable precomputation,
population-parallel evaluation on one persistent all-core team, frozen
generation updates, fixed-index writes, ordered commits, and counter-keyed
random events.
Claim boundary: source-backed flexible academic reproduction of the target
problem and all six native algorithm/mode roles; not author source,
proprietary model, unavailable site arrays, random state or exact replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::l0079 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

enum class Optimizer { adaptive_ga, gbest_pso };
enum class ConstraintMode { array, binary, continuous };
enum class CandidateProfile { journal_628, thesis_658 };

struct Evaluation {
    double gross_aep_mwh_8766 = 0.0;
    double net_aep_mwh_8766 = 0.0;
    double net_aep_mwh_8760 = 0.0;
    double cable_length_m = 0.0;
    double electrical_loss_fraction = 0.0;
    double lifetime_cost_gbp = 0.0;
    double lcoe_gbp_per_mwh = 0.0;
    double minimum_spacing_m = 0.0;
    bool feasible = false;
};

struct RunConfig {
    Optimizer optimizer = Optimizer::adaptive_ga;
    ConstraintMode mode = ConstraintMode::array;
    CandidateProfile candidate_profile = CandidateProfile::journal_628;
    std::uint64_t seed = 79001;
    int workers = 20;
    int population = 100;
    int maximum_generations = 1000;
    int no_improvement_generations = 50;
    bool enable_convergence = true;
};

struct RunResult {
    std::string case_id;
    std::string method_semantic_id;
    std::string problem_semantic_id =
        "l0079_middelgrunden_lcoe_three_constraint_declared_v1";
    std::string protocol_semantic_id =
        "l0079_native_six_role_single_run_v1";
    std::string optimizer;
    std::string constraint_mode;
    std::string candidate_profile;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t parallel_regions = 0;
    int population = 0;
    int generations = 0;
    std::uint64_t physical_fes = 0;
    std::string convergence_reason;
    Evaluation reference_evaluation;
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

    explicit Problem(
        CandidateProfile profile = CandidateProfile::journal_628
    );
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int wind_state_count() const noexcept;
    [[nodiscard]] int candidate_count() const noexcept;
    [[nodiscard]] double minimum_spacing_m() const noexcept;
    [[nodiscard]] double domain_area_km2() const noexcept;
    [[nodiscard]] CandidateProfile candidate_profile() const noexcept;
    [[nodiscard]] const std::vector<Point>& as_built_layout() const noexcept;
    [[nodiscard]] const std::vector<Point>& candidate_positions() const noexcept;
    [[nodiscard]] Evaluation evaluate_layout(
        const std::vector<Point>& layout
    ) const;

private:
    std::unique_ptr<Impl> impl_;
    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config);
[[nodiscard]] std::string to_string(Optimizer value);
[[nodiscard]] std::string to_string(ConstraintMode value);
[[nodiscard]] std::string to_string(CandidateProfile value);

}  // namespace core99::l0079
