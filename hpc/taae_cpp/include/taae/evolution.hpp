/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE end-to-end declared-reconstruction method interface
Paper title: Transformer Autoencoder-Assisted Evolutionary Framework for Constrained Multiobjective 3D Wind Farm Layout Optimization
DOI: 10.1109/JAS.2026.126233
Public author method source/checkpoint: unavailable as recorded in docs/source-dossiers/Y36.json
Missing choices completed here: SPEA2 density k, CDP and tournament ties, raw-latent mutation bounds, pre-repair decoded-solution filtering, Gaussian covariance regularization, post-repair guards, no-feasible front output, partial batches, checkpoint admission, exact-trajectory CPU speculation, and stage/work receipts
Reconstruction status: bounded executable M3 engineering reconstruction on the declared P3 problem proxy
Method evidence tier: M3_DECLARED_COMPLETION
Method semantic ID: taae_transformer_evolution_declared_reconstruction_v1
Kernel semantic ID: taae_transformer_declared_reconstruction_v1
Problem semantic ID: taae_zhangbei_structured_declared_proxy_v1
Step 11 loss-weight and multiplicative-wake probes are sensitivity-only
independent method or problem semantics. Baseline semantics remain unchanged;
distinct semantics are never pooled or used for cross-semantic ranking.
Controlling contract: shared/contracts/taae_transformer_evolution_declared_reconstruction_contract.json
Claim boundary: distinct bounded end-to-end reconstruction only; original taae remains blocked, paper-scale state requires an immutable checkpoint, and no Zhangbei, reported-front, formal, performance, or GPU claim is made
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/case.hpp"
#include "taae/model.hpp"
#include "wflop/taae_problem.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace taae::evolution {

inline constexpr const char* kMethodSemanticId =
    "taae_transformer_evolution_declared_reconstruction_v1";
inline constexpr const char* kRegressionHalfSensitivitySemanticId =
    "taae_transformer_evolution_regression15_sensitivity_v1";
inline constexpr const char* kProblemSemanticId =
    "taae_zhangbei_structured_declared_proxy_v1";
inline constexpr const char* kMultiplicativeWakeProblemSemanticId =
    "taae_zhangbei_structured_multiplicative_wake_sensitivity_v1";

enum class TrainingStateProfile {
    bounded_smoke,
    paper_scale_checkpoint,
};

struct EvolutionConfig {
    std::uint64_t seed = 1;
    std::uint64_t maximum_physical_fes = 10000;
    int workers = 1;
    int population_size = 100;
    int fine_tune_epochs = 10;
    int fine_tune_batch_size = 64;
    TrainingStateProfile training_profile =
        TrainingStateProfile::bounded_smoke;
    std::string checkpoint_input;
    std::string checkpoint_sha256;
    std::string checkpoint_output;
    std::string backend = "cpu";
    ModelConfig model_config{};
    LossWeights fine_tune_loss_weights{};
    wflop::taae::WakeCombination wake_combination =
        wflop::taae::WakeCombination::root_sum_square;
};

struct IndividualRecord {
    std::vector<int> layout_1based;
    wflop::taae::CompleteEvaluation evaluation;
    double relative_fitness = 0.0;
    int nondomination_rank = 0;
    double crowding_distance = 0.0;
};

struct StageReceipt {
    double wall_seconds = 0.0;
    std::uint64_t parallel_regions = 0;
    std::uint64_t task_items = 0;
    std::uint64_t participant_activations = 0;
    int distinct_participants = 0;
    int peak_region_participants = 0;
};

struct ProposalWorkReceipt {
    std::uint64_t latent_proposal_attempts = 0;
    std::uint64_t speculative_decode_batches = 0;
    std::uint64_t speculative_decode_tasks = 0;
    std::uint64_t speculative_decode_discards = 0;
    std::uint64_t repair_rng_invalidations = 0;
    std::uint64_t accepted_latent_offspring = 0;
    std::uint64_t raw_duplicate_rejects = 0;
    std::uint64_t pre_repair_parent_rejects = 0;
    std::uint64_t post_repair_rejects = 0;
    std::uint64_t refill_attempts = 0;
    std::uint64_t refill_rejects = 0;
};

struct EvolutionResult {
    std::string method_semantic_id;
    std::string kernel_semantic_id;
    std::string problem_semantic_id;
    std::string problem_semantic_hash;
    std::string wake_combination;
    std::string case_id;
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    std::uint64_t generations = 0;
    int requested_workers = 0;
    int resolved_workers = 0;
    std::string training_state_profile_id;
    ModelConfig model_config;
    LossWeights fine_tune_loss_weights;
    TrainingWork training_work;
    double evaluator_wall_seconds = 0.0;
    double total_wall_seconds = 0.0;
    StageReceipt bounded_pretraining_stage;
    StageReceipt fine_tuning_stage;
    StageReceipt population_encoding_stage;
    StageReceipt offspring_decode_repair_variation_stage;
    StageReceipt evaluator_stage;
    StageReceipt selection_other_stage;
    ProposalWorkReceipt proposal_work;
    std::string model_hash;
    std::string population_layout_hash;
    std::string front_hash;
    std::string front_feasibility;
    double front_minimum_normalized_constraint_violation = 0.0;
    std::vector<IndividualRecord> front;
    CheckpointMetadata checkpoint;
};

EvolutionResult run_declared_reconstruction(
    const EvolutionConfig& config,
    const fode::CaseData& problem
);

bool run_scalar_selection_fixtures(std::string& report);
bool run_latent_operator_fixtures(std::string& report);
bool run_repair_and_duplicate_fixtures(
    const fode::CaseData& problem,
    std::string& report
);

std::string result_to_json(const EvolutionResult& result);

}  // namespace taae::evolution
