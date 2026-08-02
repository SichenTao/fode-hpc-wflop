/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: GeoGA Anholt-structured declared execution interface
Paper title: A Geometric Mutation-Based Genetic Algorithm for Irregular Large-Scale Offshore Wind Farm Layout Optimization
DOI: 10.1109/CBD69312.2025.00059
Public asset/source: no author implementation or numerical Anholt data found; evidence dossier docs/source-dossiers/L0726.json
Missing information: original Anholt boundary, candidate set, wind arrays, turbine curves, and author implementation
Reconstruction: declared M3 method completion applied only to the declared P3 proxy problem
Method semantic ID: geoga_declared_reconstruction_v1
Problem semantic ID: geoga_anholt_structured_declared_proxy_v1
Controlling contract: shared/contracts/geoga_anholt_structured_execution_contract.json
Evidence tiers: admitted M3 declared completion on a distinct P3 declared proxy
Reused controls: roulette AEP selection, one-point crossover, deterministic duplicate repair, one top-five-nearest-free geometric mutation per child, parent-plus-offspring best-50 survival, exact physical FES, counter-keyed RNG, and stable ties
Claim boundary: reuses the admitted GeoGA operator semantics without changing its historical GGA-asset execution or claiming the original Anholt experiment
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "geoga/problem.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace geoga {

inline constexpr const char* kMethodSemanticId =
    "geoga_declared_reconstruction_v1";
inline constexpr const char* kExecutionProfileId =
    "geoga_anholt_structured_p3_execution_v1";

namespace mechanism {

int nearest_free_replacement(
    const Problem& problem,
    const std::vector<int>& layout_0based,
    int mutation_position,
    double uniform_draw
);

}  // namespace mechanism

struct EvolutionConfig {
    std::uint64_t seed = 1;
    std::uint64_t physical_fes = 10000;
    int workers = 0;
    std::string backend = "cpu";
};

struct StageReceipt {
    double wall_seconds = 0.0;
    std::uint64_t parallel_regions = 0;
    std::uint64_t task_items = 0;
    std::uint64_t participant_activations = 0;
    int distinct_participants = 0;
    int peak_region_participants = 0;
};

struct WorkReceipt {
    std::uint64_t roulette_parent_draws = 0;
    std::uint64_t crossover_gene_copies = 0;
    std::uint64_t duplicate_repairs = 0;
    std::uint64_t geometry_distance_checks = 0;
    std::uint64_t geometry_mutations = 0;
    std::uint64_t survivor_candidates_ranked = 0;
};

struct EvolutionResult {
    std::string method_semantic_id;
    std::string execution_profile_id;
    std::string problem_semantic_id;
    std::string problem_semantic_hash;
    std::string case_id;
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    std::uint64_t generations = 0;
    int requested_workers = 0;
    int resolved_workers = 0;
    double total_wall_seconds = 0.0;
    StageReceipt initialization_stage;
    StageReceipt variation_repair_stage;
    StageReceipt evaluator_stage;
    StageReceipt selection_other_stage;
    WorkReceipt work;
    std::vector<int> best_layout_0based;
    LayoutEvaluation best_evaluation;
    std::string best_layout_hash;
    std::string population_layout_hash;
};

EvolutionResult run(const EvolutionConfig& config, const Problem& problem);
std::string result_to_json(const EvolutionResult& result);

}  // namespace geoga
