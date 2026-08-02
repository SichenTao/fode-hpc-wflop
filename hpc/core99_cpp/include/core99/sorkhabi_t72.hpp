/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T72 constrained energy-noise WFLOP and CHCP-NSGA-II API
Paper/DOI: Constrained Multi-Objective Wind Farm Layout Optimization:
Novel Constraint Handling Approach Based on Constraint Programming;
10.1016/j.renene.2018.03.053
Public source: no author implementation or native nine-map data were located;
the paper states that its NSGA-II and IBM ILOG CP Optimizer 12.6 wrapper were
in-house C++ code. Related public source:
https://gitlab.windenergy.dtu.dk/TOPFARM/PyWake.git revision
5b07481ec9b3633a74844651648f266ba82a8b32 supplies an MIT-licensed,
independently maintained ISO 9613-1/2 implementation used as a formula check
Missing assets: author source, nine random polygon maps and receptors, raw
24-by-43 wind probabilities, penalty values in the target paper, NSGA-II
distribution indices, CP random states, and raw optimization fronts
Paper/source conflicts: printed Jensen Eqs. (1), (3), and (4) use mutually
inconsistent induction-factor conventions; printed CP Eq. (20) fixes other
turbines at starred coordinates although multiple infeasible turbines may be
repaired together; AEP is maximized although Eq. (17) adds a positive penalty
Resolution and Reconstruction: use the standard full Jensen deficit
1-sqrt(1-CT), the target paper's 24 directions and 43 speeds with the cited
Kusiak direction-conditioned Weibull data, deterministic similar-area
jittered-Voronoi maps, the predecessor paper's 1e4/4e4 penalties, and a
joint discrete branch-and-bound repair over the paper's 150 coordinate bins;
maximum distance is measured in squared discrete-bin coordinates because the
CP objective operates on those integer variables and this interpretation,
unlike m2, is consistent with the paper's reported CP percentages; the open
replacement keeps 4096 nearest legal candidates per variable, uses greedy
incumbents plus a deterministic 2000-node joint fallback, and exposes every
node-limit event instead of claiming proprietary-solver objective identity
Target method: CHCP repair followed by dynamic penalty fallback inside
continuous-variable NSGA-II
Target problems: 3 km by 3 km, land availability 70/80/90 percent, and
5/10/15 turbines with AEP and maximum receptor noise objectives
Method/problem semantic IDs: t72_chcp_nsga2_declared_reconstruction_v1;
t72_energy_noise_voronoi9_declared_reconstruction_v1
Controlling contract: shared/contracts/core99_t72_sorkhabi_2018.json
Production backend: pure C++20 CPU-HPC; population evaluations and independent
paper runs use persistent, non-oversubscribed worker teams
Claim boundary: academic declared flexible reproduction of the paper method
and problem family, not author-code, IBM-wrapper, native-map, or numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t72 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct Evaluation {
    double aep_gwh = 0.0;
    double maximum_spl_dba = 0.0;
    double proximity_violation_m = 0.0;
    double regulatory_violation_m = 0.0;
    bool feasible = false;
};

struct RepairReceipt {
    bool attempted = false;
    bool repaired = false;
    bool timed_out = false;
    bool node_limit_hit = false;
    std::uint64_t search_nodes = 0;
    int infeasible_turbines = 0;
    double squared_displacement_bin2 = 0.0;
};

struct FrontPoint {
    double aep_gwh = 0.0;
    double maximum_spl_dba = 0.0;
    std::vector<Point> layout;
};

enum class PhysicsProfile {
    sorkhabi_2018_lineage,
    sorkhabi_2016_cubic_100db,
};

enum class ConstraintHandlingMode {
    chcp_dynamic_penalty,
    static_penalty,
    dynamic_penalty,
    death_penalty,
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    int workers = 20;
    std::uint64_t physical_fes = 80000;
    double maximum_repair_distance_bin2 = 1000.0;
    double penalty_coefficient = 10000.0;
    ConstraintHandlingMode constraint_mode =
        ConstraintHandlingMode::chcp_dynamic_penalty;
    double dynamic_penalty_multiplier = 1.0;
    bool feasible_initialization = false;
    bool enable_convergence = true;
};

struct RunResult {
    std::string problem_id;
    std::string problem_semantic_id =
        "t72_energy_noise_voronoi9_declared_reconstruction_v1";
    std::string method_semantic_id =
        "t72_chcp_nsga2_declared_reconstruction_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t physical_fes = 0;
    int generations = 0;
    int population_size = 0;
    double maximum_repair_distance_bin2 = 0.0;
    double penalty_coefficient = 0.0;
    std::uint64_t repair_attempts = 0;
    std::uint64_t repair_successes = 0;
    std::uint64_t repair_timeouts = 0;
    std::uint64_t repair_node_limit_hits = 0;
    std::uint64_t repair_search_nodes = 0;
    double repair_seconds = 0.0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    bool converged = false;
    double measured_land_availability = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<FrontPoint> front;
};

class Problem {
public:
    Problem(
        int land_availability_percent,
        int turbine_count,
        int map_variant = 0,
        PhysicsProfile physics_profile =
            PhysicsProfile::sorkhabi_2018_lineage
    );

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] int land_availability_percent() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int map_variant() const noexcept;
    [[nodiscard]] PhysicsProfile physics_profile() const noexcept;
    [[nodiscard]] int population_size() const noexcept;
    [[nodiscard]] double measured_land_availability() const noexcept;
    [[nodiscard]] const std::vector<Point>& receptors() const noexcept;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_population(
        const std::vector<std::vector<Point>>& layouts,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] RepairReceipt repair(
        std::vector<Point>& layout,
        double maximum_squared_displacement_bin2,
        double time_limit_seconds = 10.0
    ) const;
    [[nodiscard]] bool regulatory_forbidden(const Point& point) const;

private:
    struct MapSeed {
        Point point;
        bool forbidden = false;
    };

    int nearest_map_seed(const Point& point) const;
    double regulatory_distance(const Point& point) const;
    void build_map();
    void build_wind();
    void build_noise();

    std::string id_;
    int land_availability_percent_ = 0;
    int turbine_count_ = 0;
    int map_variant_ = 0;
    PhysicsProfile physics_profile_ =
        PhysicsProfile::sorkhabi_2018_lineage;
    int population_size_ = 0;
    double measured_land_availability_ = 0.0;
    std::vector<MapSeed> map_seeds_;
    std::vector<Point> receptors_;
    std::vector<double> direction_degrees_;
    std::vector<double> wind_speeds_mps_;
    std::vector<double> joint_probabilities_;
    std::vector<double> expected_acoustic_source_energy_;
    std::vector<double> regulatory_distance_grid_;
};

[[nodiscard]] RunResult run(
    const Problem& problem,
    const RunConfig& config
);

}  // namespace core99::t72
