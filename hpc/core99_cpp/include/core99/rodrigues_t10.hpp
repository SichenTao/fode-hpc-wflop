/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T10 multi-objective discrete WFLOP, five constraint-
handling regimes, four target MOEAs, and multi-resolution API
Paper/DOI: Multi-Objective Optimization of Wind Farm Layouts--Complexity,
Constraint Handling and Scalability; 10.1016/j.rser.2016.07.021
Primary paper asset: publisher PDF SHA-256
7a3144fb6fc2c584efa04859cea4906b1e05e23ee326af10c4db1b5aebead08d;
Eqs. (2)-(9), Figs. 4, 6-17 and Tables 2-5 were consumed.
Public source: the paper is openly available from Elsevier and CWI, but the
paper contains no code/data statement or executable-asset link. Exact-title,
author, institutional, GitHub and local-corpus searches found no paper-linked
author implementation or numeric result archive.
Provided facts: binary grid occupancy; four square areas A-D; 2D, 4D and 8D
grid steps; Vestas V164-8 MW tabulated power/thrust curves; 12-direction North
Sea wind resource; partial-overlap Katic-Jensen wake; normalized energy and
efficiency objectives; 8D minimum separation; no-constraint, domination,
penalty, random-removal repair and 100-try resampling; farthest-site feasible
initialization; online-MI MOGOMEA, geographic offline o-MOGOMEA, NSGA-II and
objective-clustered c-NSGA-II; five clusters of initial size four; population
growth; an unrestricted nondominated archive; one million physical objective
evaluations per objective; normalized hypervolume at reference (0,0); ten
independent runs; and 8D-to-4D-to-2D multi-resolution transfer.
Missing/conflicts: source, random states, wind-direction convention, precise
UPGMA tie rules and MI normalization, archive capacity, cluster tie rules,
numeric NIS base, stopping-history implementation, resampling random stream,
paper-result arrays and exact multi-resolution constraint-handler selection
are absent. The prose calls five CHTs, while the case study uses four active
CHTs at 2D/4D plus the naturally feasible 8D no-constraint construction; Figs.
13-15 nevertheless repeat the four CHT labels at every resolution.
Declared reconstruction: directions are meteorological wind-from and are
converted to flow-to. Binary MI in nats and average-link UPGMA with stable
index ties define the online tree; Euclidean geographic distance and the same
UPGMA rules define the offline tree. tau=0.5 follows algebraically from the
paper's c=(2/k)floor(tau*n), k=5, initial n=20 and stated initial c=4.
Archive capacity is unbounded modulo exact layout/objective duplicates. The
FI threshold 1+floor(log10(n)) also supplies the otherwise unnamed NIS window;
formal fixed-grid termination uses twice that current-population window.
Multi-resolution uses each algorithm's paper-observed best CHT: repair for
MOGOMEA, resample for o-MOGOMEA, and constraint domination for both NSGA-II
variants. Every completion, rejected alternative and execution receipt is
emitted; no author numerical replay is claimed.
HPC design: immutable geometry, conflict adjacency, rotated projections and
ideal powers are precomputed. Population objective evaluations, offspring
construction, dominance rows, linkage pair statistics and independent
individual GOM trajectories use one persistent all-core team where legal.
Sequential state transitions inside one GOM trajectory, archive commits,
population growth, resolution transfer and hypervolume history retain paper
ordering. Counter-keyed random events and fixed-index reductions require exact
one/all-core scientific identity. Production selects every Waffle core;
one-core work is limited to sparse H6 baselines.
Method semantic IDs: t10_mogomea_online_mi_v1;
t10_omogomea_offline_geographic_v1; t10_nsgaii_archive_growth_v1;
t10_clustered_nsgaii_archive_growth_v1
Problem semantic ID: t10_mowflop_katic_binary_grid_v1
Protocol semantic ID: t10_196_role_1960_receipt_v1
Controlling contract: shared/contracts/core99_t10_rodrigues_2016.json
Claim boundary: flexible equation-level academic reproduction of every paper
problem, target algorithm, CHT and multi-resolution role; not author Python,
random stream, exact UPGMA/termination implementation, result trajectory,
figure data or hardware timing replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace core99::t10 {

enum class Algorithm {
    mogomea,
    offline_mogomea,
    nsgaii,
    clustered_nsgaii,
};

enum class ConstraintHandling {
    no_constraints,
    constraint_domination,
    penalty,
    repair,
    resample,
};

struct Evaluation {
    double normalized_energy = 0.0;
    double efficiency = 0.0;
    int occupied_turbines = 0;
    int violating_pairs = 0;
    bool physically_evaluated = false;
};

struct PaperCase {
    std::string case_id;
    char farm = 'A';
    int grid_step_diameters = 8;
    int side_points = 4;
    int variables = 16;
    int maximum_packing = 16;
    double side_m = 3936.0;
    double step_m = 1312.0;
    double area_km2 = 15.49;
};

struct RunConfig {
    Algorithm algorithm = Algorithm::offline_mogomea;
    ConstraintHandling constraint = ConstraintHandling::repair;
    std::uint64_t seed = 201607021ULL;
    int workers = 20;
    std::uint64_t maximum_physical_fes = 1000000ULL;
    int maximum_generations = 100000;
    bool multi_resolution = false;
    double mutation_probability_override = 0.0;
};

struct FrontPoint {
    double normalized_energy = 0.0;
    double efficiency = 0.0;
    int occupied_turbines = 0;
    int violating_pairs = 0;
    std::vector<std::uint64_t> occupancy_words;
};

struct RunReceipt {
    std::string case_id;
    std::string algorithm;
    std::string constraint_handling;
    std::string problem_semantic_id;
    std::string method_semantic_id;
    std::uint64_t seed = 0;
    int requested_workers = 1;
    int observed_workers = 1;
    std::uint64_t physical_fes = 0;
    std::uint64_t attempted_candidates = 0;
    std::uint64_t rejected_infeasible_without_evaluation = 0;
    int generations = 0;
    int final_grid_step_diameters = 8;
    int final_population = 0;
    int archive_size = 0;
    double hypervolume = 0.0;
    double evaluator_seconds = 0.0;
    double linkage_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<FrontPoint> archive;
};

class Problem {
public:
    // Public only so the translation unit's equation kernel can consume one
    // immutable precomputed data object; callers still receive no Data handle.
    struct Data;

    explicit Problem(std::string case_id);

    [[nodiscard]] const PaperCase& paper_case() const noexcept;
    [[nodiscard]] int word_count() const noexcept;
    [[nodiscard]] const std::vector<std::pair<int, int>>& conflicts() const noexcept;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<std::uint64_t>& layout,
        ConstraintHandling constraint,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_population(
        const std::vector<std::vector<std::uint64_t>>& layouts,
        ConstraintHandling constraint,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] Evaluation evaluate_direct(
        const std::vector<std::uint64_t>& layout,
        ConstraintHandling constraint
    ) const;
    [[nodiscard]] std::vector<std::uint64_t> initial_layout(
        std::uint64_t seed,
        std::uint64_t individual
    ) const;
    [[nodiscard]] std::vector<std::uint64_t> repair(
        std::vector<std::uint64_t> layout,
        std::uint64_t seed,
        std::uint64_t event
    ) const;
    [[nodiscard]] int violation_count(
        const std::vector<std::uint64_t>& layout
    ) const noexcept;
    [[nodiscard]] bool feasible(
        const std::vector<std::uint64_t>& layout
    ) const noexcept;

private:
    std::shared_ptr<const Data> data_;
};

[[nodiscard]] RunReceipt optimize(
    const Problem& problem,
    const RunConfig& config
);

[[nodiscard]] std::vector<std::string> paper_case_ids();
[[nodiscard]] std::vector<std::string> paper_role_ids(bool include_multi_resolution);
[[nodiscard]] const char* algorithm_name(Algorithm value) noexcept;
[[nodiscard]] const char* constraint_name(ConstraintHandling value) noexcept;
[[nodiscard]] Algorithm parse_algorithm(const std::string& value);
[[nodiscard]] ConstraintHandling parse_constraint(const std::string& value);

}  // namespace core99::t10
