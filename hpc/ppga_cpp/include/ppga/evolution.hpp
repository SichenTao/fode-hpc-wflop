/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: PPGA Nantong-structured declared 3D evolutionary interface
Paper title: Advanced 3D Wind Farm Layout Optimization Framework via Power-Law Perturbation-Based Genetic Algorithm
DOI: 10.1109/JAS.2025.125351
Public author method source/data: unavailable as recorded in docs/source-dossiers/T43.json
Paper-preserved fields: population 30, threshold zero, crossover 0.8, mutation 0.1, power-law exponent 2.5, second-generation perturbation, elite survival, and exact complete-layout fitness calls
Declared M3 completions: normalized fitness, occupied-site distance, offspring-versus-parent stagnant proportion, strict probabilistic perturbation gate, per-dimension finite-support power-law sampler, elite count three, counter-keyed RNG, partial terminal batch, stable ties, and CPU execution receipts
Method evidence tier: M3_DECLARED_COMPLETION
Method semantic ID: ppga_nantong_structured_3d_declared_reconstruction_v2
Problem semantic ID: ppga_nantong_structured_3d_declared_proxy_v1
Controlling contract: shared/contracts/ppga_nantong_structured_3d_declared_reconstruction_contract.json
Claim boundary: bounded declared reconstruction only; existing ppga_declared_reconstruction_fode_e0_v1 is unchanged and original Nantong method/results remain blocked
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "ppga/problem.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ppga {

inline constexpr const char* kMethodSemanticId =
    "ppga_nantong_structured_3d_declared_reconstruction_v2";

namespace mechanism {

bool perturbation_gate(
    double theta,
    double threshold,
    double uniform_draw
);
int finite_support_power_law_step(
    int maximum,
    double uniform_draw
);
std::vector<int> perturb_every_dimension_unrepaired(
    const std::vector<int>& layout,
    int grid_dimension,
    const std::vector<double>& step_uniform_draws,
    const std::vector<double>& sign_uniform_draws
);
double offspring_parent_stagnant_proportion(
    const std::vector<double>& parent_fitness,
    const std::vector<double>& offspring_fitness
);

}  // namespace mechanism

struct EvolutionConfig {
    std::uint64_t seed = 1;
    std::uint64_t physical_fes = 1500;
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
    std::uint64_t pairwise_layout_distances = 0;
    std::uint64_t crossover_gene_choices = 0;
    std::uint64_t mutation_gene_trials = 0;
    std::uint64_t mutation_events = 0;
    std::uint64_t perturbation_gate_draws = 0;
    std::uint64_t perturbed_individuals = 0;
    std::uint64_t power_law_gene_steps = 0;
    std::uint64_t stagnation_parent_offspring_comparisons = 0;
    std::uint64_t duplicate_repairs = 0;
};

struct EvolutionResult {
    std::string method_semantic_id;
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
    StageReceipt diversity_adaptation_stage;
    StageReceipt variation_repair_stage;
    StageReceipt evaluator_stage;
    StageReceipt selection_other_stage;
    WorkReceipt work;
    std::vector<int> best_layout_1based;
    LayoutEvaluation best_evaluation;
    std::string population_layout_hash;
    std::string best_layout_hash;
};

EvolutionResult run(
    const EvolutionConfig& config,
    const Problem& problem
);
std::string result_to_json(const EvolutionResult& result);

}  // namespace ppga
