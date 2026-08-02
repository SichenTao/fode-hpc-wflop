/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T31 VNS and large offshore paper-native benchmark
Paper title: Variable Neighborhood Search for Large Offshore Wind Farm Layout
Optimization
Paper DOI: 10.1016/j.cor.2021.105588
Paper dataset DOI: 10.11583/DTU.13134731, official version 1, archive file
25210169, MD5 994c1c65356e48749ec57a25bd523c45, CC BY-NC-SA 4.0.
Public source: the full ten-instance dataset is public; no paper-linked VNS
source implementation was found.
Paper-provided assets: binary fixed-cardinality formulation; pairwise power
matrix; PDSP-inspired removal initialization; Basic VNS; Random and Conic
shake; best-improvement 1-opt and nearby 2-opt; delta evaluation; three
Mosetti cases; ten large official sites; three foundation-cost cases; 30-second,
one-hour, and ten-hour protocols; TNW wind states; NREL 10/15 MW curves.
Missing/conflicts: source, seeds, energy prices, penalty W, Jensen expansion,
minimum-distance multiple, batch-removal schedule, VNS radii and K, candidate
caps, conic angle, local-search stopping, and exact direction convention are
not published. The dataset README reports 26/8 turbines for the two zones of
site A, while its info.json and 8 MW/km2 zone areas imply 26/14; the latter
data/formula-consistent quotas are used. The text calls M a k-by-k matrix
although only its symmetric pair sum is used by the objective.
Reconstruction: use the official numeric dataset byte-for-byte; meteorological
direction; k=0.04; 5D new-turbine spacing; energy prices 200/40 EUR per MWh
for low/high foundation-cost influence; a lexicographically dominating
spacing penalty; progressive 5-percent removal batches; radii 3D/5D/8D;
128 nearest 1-opt candidates; 32 nearby turbine pairs with 16 candidates per
side for 2-opt; 60-degree Conic wedge; deterministic counter-keyed randomness.
When the penalty-guided PDSP seed cannot be made feasible by one-position
repairs, rebuild the exact zone cardinalities by deterministic greedy maximal
spacing sets over the PDSP order and 31 declared alternative orders.
Store the symmetric packed pair matrix, not duplicate M_ij and M_ji. Fixed
turbine wakes use quadratic superposition; the paper matrix then adds movable
pair losses linearly, exactly preserving the stated approximation.
Method semantic ID: t31_pdsp_random_conic_vns_declared_v1
Problem semantic IDs: t31_mosetti_threecase_matrix_v1;
t31_official_synthetic10_pair_matrix_v1
Protocol semantic ID: t31_3x30s_10x3x1h_10x10h_v1
Production backend: pure C++ CPU. One persistent all-core team builds diagonal
and packed pair terms, performs contribution reductions, candidate deltas,
one-opt and two-opt search. Ordered state commits preserve deterministic
fixed-work replay; formal paper runs retain the wall-time limits.
Claim boundary: academic paper/data reconstruction, not author VNS source,
unpublished parameter replay, identical random stream, or numerical replay.
Contract: shared/contracts/core99_t31_cazzaro_vns_2022.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace core99::t31 {

enum class FoundationMode {
    none,
    low_cost,
    high_cost,
};

enum class ShakeMode {
    circular,
    conic,
    directional,
    displacement,
    random,
    random_directional,
    circular_displacement,
    random_conic,
    directional_conic,
    all,
};

struct Position {
    double x_m = 0.0;
    double y_m = 0.0;
    double depth_m = 0.0;
    double foundation_eur = 0.0;
    int zone = 0;
};

struct Evaluation {
    double objective_mwh_equivalent = 0.0;
    double aep_mwh = 0.0;
    double foundation_cost_eur = 0.0;
    double spacing_violation_m = 0.0;
};

struct ProblemInfo {
    std::string case_id;
    std::string semantic_id;
    int available_positions = 0;
    int fixed_turbines = 0;
    std::vector<int> zone_quotas;
    int wind_states = 0;
};

struct RunConfig {
    int workers = 20;
    double time_limit_seconds = 3600.0;
    std::uint64_t fixed_iterations = 0;
    FoundationMode foundation_mode = FoundationMode::none;
    ShakeMode shake_mode = ShakeMode::random_conic;
    std::filesystem::path matrix_cache;
    std::vector<double> checkpoint_seconds;
};

struct TimeCheckpoint {
    double target_seconds = 0.0;
    double observed_seconds = 0.0;
    double best_objective_mwh_equivalent = 0.0;
};

struct RunResult {
    std::string case_id;
    std::string problem_semantic_id;
    std::string method_semantic_id;
    std::string protocol_semantic_id;
    FoundationMode foundation_mode = FoundationMode::none;
    ShakeMode shake_mode = ShakeMode::random_conic;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t completed_vns_iterations = 0;
    std::uint64_t matrix_pair_evaluations = 0;
    std::uint64_t wake_state_evaluations = 0;
    std::uint64_t delta_candidate_evaluations = 0;
    Evaluation initial;
    Evaluation best;
    std::vector<int> best_positions;
    std::vector<double> best_history_mwh;
    std::vector<TimeCheckpoint> time_checkpoints;
    double problem_preprocessing_seconds = 0.0;
    double matrix_seconds = 0.0;
    double initialization_seconds = 0.0;
    double shake_seconds = 0.0;
    double local_search_seconds = 0.0;
    double optimization_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    Problem(
        std::filesystem::path extracted_dataset_root,
        std::string case_id,
        FoundationMode mode,
        int workers = 20
    );

    [[nodiscard]] const ProblemInfo& info() const noexcept;
    [[nodiscard]] const std::vector<Position>& positions() const noexcept;
    [[nodiscard]] Evaluation evaluate_selection(
        const std::vector<int>& selected
    ) const;
    [[nodiscard]] double minimum_spacing_m() const noexcept;
    [[nodiscard]] FoundationMode foundation_mode() const noexcept;
    [[nodiscard]] double diagonal_value(int position) const;
    [[nodiscard]] double symmetric_pair_value(
        int first,
        int second
    ) const;
    [[nodiscard]] double preprocessing_seconds() const noexcept;
    [[nodiscard]] std::uint64_t matrix_fingerprint() const noexcept;

private:
    struct WindState {
        double direction_degrees = 0.0;
        double speed_mps = 0.0;
        double probability = 0.0;
    };
    struct CurvePoint {
        double speed_mps = 0.0;
        double power_mw = 0.0;
        double thrust = 0.0;
    };

    ProblemInfo info_;
    FoundationMode mode_ = FoundationMode::none;
    std::vector<Position> positions_;
    std::vector<Position> fixed_;
    std::vector<WindState> wind_;
    std::vector<CurvePoint> new_curve_;
    std::vector<CurvePoint> fixed_curve_;
    double new_rotor_diameter_m_ = 240.0;
    double fixed_rotor_diameter_m_ = 179.0;
    double wake_expansion_ = 0.04;
    double minimum_spacing_m_ = 1200.0;
    bool mosetti_coordinates_y_down_ = false;
    std::vector<float> fixed_deficit_squared_;
    std::vector<double> free_aep_mwh_;
    double preprocessing_seconds_ = 0.0;
    std::uint64_t matrix_fingerprint_ = 0;

    friend class PackedMatrix;
};

[[nodiscard]] RunResult run(
    const Problem& problem,
    std::uint64_t seed,
    const RunConfig& config = {}
);
[[nodiscard]] std::vector<std::string> paper_case_ids();
[[nodiscard]] std::string foundation_mode_name(FoundationMode mode);
[[nodiscard]] std::string shake_mode_name(ShakeMode mode);

}  // namespace core99::t31
