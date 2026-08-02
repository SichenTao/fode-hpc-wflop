/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T85 joint wind-farm layout and active-yaw WFLOP plus
AGLDPSO API
Paper/DOI: Particle Swarm Optimization of a Wind Farm Layout with Active
Control of Turbine Yaws; 10.1016/j.renene.2023.02.058
Public source: the authors provide the complete arXiv manuscript source at
https://arxiv.org/abs/2210.02084, but no paper-linked optimizer, wake-model,
case-data, or result-replay source was located by exact-title, DOI, author,
arXiv-ID, and GitHub searches on 2026-07-31
Provided paper assets: yawed-Gaussian wake Eqs. (1)--(6), atmospheric and AEP
Eqs. (11)--(13), joint decision/constraint Eqs. (14)--(17), AGLDPSO update
Eq. (10), six native case definitions, visible V80/V112 curves, and aggregate
AEP result tables
Cited predecessor: Wang et al., Adaptive Granularity Learning Distributed
Particle Swarm Optimization for Large-Scale Optimization,
10.1109/TCYB.2020.2977956; its paper specifies complete LSH/LR granularity
control, c1=1.0, c2=0.1, N=500, M in [10,sqrt(N)], and Algorithm 1
Missing assets: author in-house code and random states; wind-paper population,
termination, repeats, yaw bounds, constraint handler, rotor integration grid,
V80/V112 numerical curves, V112 hub height, rotor-to-hub distance, and
heterogeneous-variable scaling used by LSH
Paper/source conflicts: none found; the wind paper deliberately summarizes
AGLDPSO and delegates its full transition to the cited predecessor
Resolution and reconstruction: paper equations and cases control; visible
manufacturer curves are deterministically digitized at integer wind speeds;
V80 uses D=80 m, hub=70 m, and 2 MW, while V112 uses D=112 m, hub=84 m,
and 3 MW; d_rt=0 because neither paper nor cited turbine assets provides it;
eight equal-area rotor points complete the unspecified quadrature; yaw is
bounded to [-30,30] degrees following the cited yaw-control convention;
mixed position/yaw coordinates are normalized before LSH; deterministic
constraint-preserving repair is used. The predecessor N=500 is retained,
while the absent wind-paper termination is completed by a declared 10,000
complete-layout-evaluation profile and 25 platform seeds.
Target method: AGLDPSO with master/multiple-slave subpopulation evolution,
LSH clustering, LR granularity adaptation, and worst-particle updates
Target problems: WF1; WF1 at Uref=6 m/s; WF1 with V112; WF2; WF3; and WF4,
all using joint layout/yaw optimization
Method/problem semantic IDs:
t85_agldpso_joint_yaw_declared_reconstruction_v1;
t85_song_joint_layout_yaw_six_case_v1
Controlling contract: shared/contracts/core99_t85_song_2023.json
Production backend: pure C++20 CPU-HPC. Wind constants, trigonometry, turbine
curves, and rotor quadrature are precomputed; initialization, LSH projection,
subpopulation updates, constraint repair, and complete-layout evaluations use
one persistent all-core worker team. Counter-keyed random events and fixed
reductions make one/all-core scientific trajectories identical.
Claim boundary: academic flexible declared reconstruction of every paper
problem and the target method, not author-code, private-data, or numerical
result replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace core99::t85 {

enum class CaseId {
    wf1,
    wf1_u6,
    wf1_v112,
    wf2,
    wf3,
    wf4,
};

struct TurbineDecision {
    double x_m = 0.0;
    double y_m = 0.0;
    std::vector<double> yaw_deg;
};

struct Evaluation {
    double aep_gwh = 0.0;
    std::array<double, 8> wind_aep_contribution_gwh{};
    double spacing_violation_m = 0.0;
    double boundary_violation_m = 0.0;
    bool feasible = false;
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    int workers = 20;
    int population = 500;
    std::uint64_t physical_fes_limit = 10000;
};

struct RunResult {
    std::string problem_id;
    std::string problem_semantic_id =
        "t85_song_joint_layout_yaw_six_case_v1";
    std::string method_semantic_id =
        "t85_agldpso_joint_yaw_declared_reconstruction_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int population = 0;
    std::uint64_t physical_fes = 0;
    std::uint64_t generations = 0;
    int final_subpopulation_size = 0;
    double initial_best_aep_gwh = 0.0;
    double best_aep_gwh = 0.0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<TurbineDecision> best_layout;
};

class Problem {
public:
    explicit Problem(CaseId id);

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] CaseId case_id() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int wind_state_count() const noexcept;
    [[nodiscard]] int decision_dimension() const noexcept;
    [[nodiscard]] int declared_population() const noexcept;
    [[nodiscard]] std::uint64_t declared_physical_fes() const noexcept;
    [[nodiscard]] int declared_repeats() const noexcept;
    [[nodiscard]] double side_length_m() const noexcept;
    [[nodiscard]] double rotor_diameter_m() const noexcept;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<TurbineDecision>& layout
    ) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_population(
        const std::vector<std::vector<TurbineDecision>>& layouts,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] std::vector<TurbineDecision> reference_layout() const;
    void repair(
        std::vector<TurbineDecision>& layout,
        std::uint64_t seed,
        std::uint64_t generation,
        std::uint64_t particle
    ) const;

private:
    struct WindState {
        double along_cos = 0.0;
        double along_sin = 0.0;
        double reference_speed_mps = 0.0;
        double probability = 0.0;
    };
    struct TurbineModel {
        double diameter_m = 80.0;
        double hub_height_m = 70.0;
        double rated_power_mw = 2.0;
        std::vector<double> power_mw;
        std::vector<double> thrust;
    };

    [[nodiscard]] double power_mw(double speed_mps) const;
    [[nodiscard]] double thrust(double speed_mps) const;

    CaseId case_id_;
    std::string id_;
    int turbine_count_ = 25;
    double side_length_m_ = 1600.0;
    TurbineModel turbine_;
    std::vector<WindState> winds_;
};

[[nodiscard]] RunResult run(
    const Problem& problem,
    const RunConfig& config
);

}  // namespace core99::t85
