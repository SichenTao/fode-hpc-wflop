/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE end-to-end declared-reconstruction method interface
Paper title: Transformer Autoencoder-Assisted Evolutionary Framework for Constrained Multiobjective 3D Wind Farm Layout Optimization
DOI: 10.1109/JAS.2026.126233
Public author method source/checkpoint: unavailable as recorded in docs/source-dossiers/Y36.json
Missing choices completed here: SPEA2 density k, CDP and tournament ties, raw-latent mutation bounds, Gaussian covariance regularization, repair/refill caps, partial batches, and checkpoint admission
Reconstruction status: bounded executable M3 engineering reconstruction on the declared P3 problem proxy
Method evidence tier: M3_DECLARED_COMPLETION
Method semantic ID: taae_transformer_evolution_declared_reconstruction_v1
Kernel semantic ID: taae_transformer_declared_reconstruction_v1
Problem semantic ID: taae_zhangbei_structured_declared_proxy_v1
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
inline constexpr const char* kProblemSemanticId =
    "taae_zhangbei_structured_declared_proxy_v1";

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
};

struct IndividualRecord {
    std::vector<int> layout_1based;
    wflop::taae::CompleteEvaluation evaluation;
    double relative_fitness = 0.0;
    int nondomination_rank = 0;
    double crowding_distance = 0.0;
};

struct EvolutionResult {
    std::string method_semantic_id;
    std::string kernel_semantic_id;
    std::string problem_semantic_id;
    std::string problem_semantic_hash;
    std::string case_id;
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    std::uint64_t generations = 0;
    int requested_workers = 0;
    std::string training_state_profile_id;
    ModelConfig model_config;
    TrainingWork training_work;
    double evaluator_wall_seconds = 0.0;
    double total_wall_seconds = 0.0;
    std::string model_hash;
    std::string population_layout_hash;
    std::string front_hash;
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
