/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T33 combined offshore layout-and-cable VNS and its
paper-native ten-site, low/high-density benchmark
Paper title: Combined Layout and Cable Optimization of Offshore Wind Farms
Paper DOI: 10.1016/j.ejor.2023.04.046
Direct predecessor used for delegated cable details: Balanced Cable Routing
for Offshore Wind Farms with Obstacles, DOI 10.1002/net.22100.
Public source and data: no paper-linked implementation was found by exact
title, DOI, author and GitHub searches on 2026-07-31. The paper explicitly
reuses the ten-instance dataset of Cazzaro and Pisinger; official dataset DOI
10.11583/DTU.13134731, file 25210169, SHA-256
17b1147b805f679ed4b25384b74f841681a1e7c6e73798533555cab4c16b80af,
CC BY-NC-SA 4.0, is consumed byte-for-byte.
Paper-provided assets: discrete binary layout; Jensen packed pairwise wake
approximation; fixed turbines; foundation costs; 25-year lifetime NPV with
EPF=0.45; balanced radial collection strings; two cable types; capacity,
flow, no-crossing and obstacle constraints; PDSP initialization; obstacle-
aware multistart Sweep; exact small-string enumeration; Swap; simultaneous
layout/cable 1-opt and nearby 2-opt delta moves; weighted feasible shake;
ten official sites at 4 and 8 MW/km2; 10 h low-density and 24 h high-density
protocols.
Missing assets: author source and random states; manually placed substation
coordinates; exact cable-loss augmentation; the number and placement rules
for root strings; Sweep multistart count; string randomization; candidate
radii/caps; shake size; VNS neighborhood schedule; exact MILP/Gurobi local
model and stopping tolerances; lateral lane widths/offset costs where
multiple cables use one mandatory visibility corridor; full original result
layouts and trajectories.
Paper/data conflict: Table 1 calls N the number of available positions but
reports info.json total Points, which includes fixed turbines. The actual
availablePositions.txt files exclude fixed turbines and are the valid binary
decision set. For site A, the official README states high-density zone counts
26/8 while info.json areas and the paper's total 40 imply 26/14; the latter
formula- and paper-consistent quotas are used.
Reconstruction and resolution: paper equations and Algorithm 1/2 control.
The official per-zone 4 and 8 MW/km2 15-MW quotas are used. One fixed
substation per official available zone is placed at that zone's candidate
centroid because unpublished manual coordinates cannot be recovered. The
predecessor cable set is retained: 240 EUR/m up to four turbines and
336 EUR/m up to six. Root-string count is the minimum capacity-feasible
count, making strings differ by at most one. Ten obstacle-aware Sweep
rotations and exact enumeration of every at-most-six-turbine string complete
the initial routing. Geometry polygons define boundaries and obstacles.
Visibility paths retain obstacle-feasible cable lengths. Each zone network
is a planar rooted-path forest, and projected cable routes use separable
lateral lanes inside shared corridors because the public instances specify
neither lane width nor offset cost; the final routed crossing count is zero
by this declared completion.
Layout neighborhoods reuse the directly cited same-lineage 3D/5D/8D radii,
128 one-opt candidates and 16-by-16 nearby two-opt candidates. Stable ordered
commit and counter-keyed events complete unpublished random lifecycle.
The original 10/24 h profiles remain exposed. The reproducible 25-seed
formal profile uses 860 complete VNS cycles for low density, the only
published combined iteration count, and 2064 cycles for high density by the
paper's 24/10 time ratio; this is declared fixed scientific work, not a claim
of wall-time equivalence or author numerical replay.
Method semantic ID: t33_combined_layout_cable_vns_declared_v1
Problem semantic ID: t33_official_synthetic10_low_high_joint_npv_v1
Protocol semantic IDs: t33_literal_10h_24h_v1;
t33_fixed_860_2064_cycles_25seed_v1
Production backend: pure C++20 CPU-HPC. One persistent all-core team builds
the packed wake matrix, updates PDSP contributions, constructs independent
Sweep rotations, and evaluates independent 1-opt/2-opt candidates. Immutable
geometry, wind and pair data use contiguous storage; fixed-index writes,
stable reductions and ordered commits preserve one/all-core scientific
trajectories. Short domains remain serial to avoid synchronization slowdown.
Claim boundary: academic flexible paper/data reconstruction of the target
joint heuristic and every paper problem; not author code, unpublished manual
substations, Gurobi bitstream, original random stream or numerical replay.
Contract: shared/contracts/core99_t33_cazzaro_combined_2023.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "core99/cazzaro_t31.hpp"
#include "fode/executor.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace core99::t33 {

enum class Density {
    low,
    high,
};

struct CableEdge {
    int zone = 0;
    int string = 0;
    int upstream_position = -1;
    int downstream_position = -1;
    int supported_turbines = 0;
    double length_m = 0.0;
    double cost_eur = 0.0;
};

struct Evaluation {
    double aep_mwh = 0.0;
    double lifetime_revenue_eur = 0.0;
    double foundation_cost_eur = 0.0;
    double cable_cost_eur = 0.0;
    double npv_eur = 0.0;
    double spacing_violation_m = 0.0;
    int cable_crossings = 0;
    bool feasible = false;
};

struct ProblemInfo {
    std::string case_id;
    char site = 'A';
    Density density = Density::low;
    int available_positions = 0;
    int fixed_turbines = 0;
    int turbines = 0;
    int zones = 0;
    int wind_states = 0;
    std::vector<int> zone_quotas;
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    int workers = 20;
    std::uint64_t fixed_vns_cycles = 0;
    double time_limit_seconds = 0.0;
    std::filesystem::path matrix_cache;
};

struct RunResult {
    std::string case_id;
    std::string problem_semantic_id =
        "t33_official_synthetic10_low_high_joint_npv_v1";
    std::string method_semantic_id =
        "t33_combined_layout_cable_vns_declared_v1";
    std::string protocol_semantic_id;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t completed_vns_cycles = 0;
    std::uint64_t matrix_pair_evaluations = 0;
    std::uint64_t wake_state_evaluations = 0;
    std::uint64_t layout_candidate_evaluations = 0;
    std::uint64_t cable_route_evaluations = 0;
    Evaluation initial;
    Evaluation best;
    std::vector<int> best_positions;
    std::vector<double> best_npv_history_eur;
    double problem_preprocessing_seconds = 0.0;
    double matrix_seconds = 0.0;
    double initialization_seconds = 0.0;
    double cable_seconds = 0.0;
    double candidate_seconds = 0.0;
    double optimization_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    Problem(
        std::filesystem::path extracted_dataset_root,
        char site,
        Density density,
        int workers = 20
    );
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] const ProblemInfo& info() const noexcept;
    [[nodiscard]] const std::vector<t31::Position>& positions() const noexcept;
    [[nodiscard]] double minimum_spacing_m() const noexcept;
    [[nodiscard]] double energy_price_factor_eur_per_mwh() const noexcept;
    [[nodiscard]] std::uint64_t paper_fixed_cycles() const noexcept;
    [[nodiscard]] double paper_time_limit_seconds() const noexcept;
    [[nodiscard]] double preprocessing_seconds() const noexcept;
    [[nodiscard]] double diagonal_aep_mwh(int position) const;
    [[nodiscard]] double pair_aep_mwh(int first, int second) const;
    [[nodiscard]] std::uint64_t wake_matrix_fingerprint() const noexcept;
    [[nodiscard]] double substation_x_m(int zero_based_zone) const;
    [[nodiscard]] double substation_y_m(int zero_based_zone) const;
    [[nodiscard]] double cable_path_distance_m(
        int zero_based_zone,
        int first_position,
        int second_position
    ) const;
    [[nodiscard]] std::vector<std::array<double, 2>>
    cable_path_polyline(
        int zero_based_zone,
        int first_position,
        int second_position
    ) const;
    [[nodiscard]] bool cable_paths_cross(
        int zero_based_zone,
        int first_upstream_position,
        int first_downstream_position,
        int second_upstream_position,
        int second_downstream_position
    ) const;
    [[nodiscard]] Evaluation evaluate_direct(
        const std::vector<int>& selected,
        int workers = 1
    ) const;
    [[nodiscard]] std::vector<int> deterministic_reference_layout() const;

private:
    class Geometry;
    std::filesystem::path root_;
    ProblemInfo info_;
    t31::Problem wake_problem_;
    std::vector<t31::Position> positions_;
    std::vector<double> substation_x_;
    std::vector<double> substation_y_;
    std::unique_ptr<Geometry> geometry_;
    double preprocessing_seconds_ = 0.0;

    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] RunResult run(
    const Problem& problem,
    const RunConfig& config = {}
);
[[nodiscard]] std::vector<std::string> paper_case_ids();
[[nodiscard]] std::string density_name(Density density);

}  // namespace core99::t33
