/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: shared algorithm/problem/training/backend registry interface
Paper title and DOI: multipaper; authoritative identities are in
docs/paper_package_completion.tsv
Paper/source basis: 23 scoped PDFs and per-paper source dossiers
Public asset: URLs, revisions, hashes, and licenses are in source dossiers
Missing/conflicts: retained per profile; no silent merging
Reconstruction: independent registry and compatibility contracts
Method/problem semantic IDs: registry_defined; registry_defined
Controlling contract and claim boundary: docs/paper_package_completion.tsv;
registration does not imply executable or formally complete status
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/case.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace wflop {

enum class FqfodeSensitivityProfile {
    baseline,
    multiplicative_action,
    fes_normalized_stage,
    wrap_after_generation_200,
    independent_stage_pretraining,
};

struct RunConfig {
    std::string algorithm_id;
    std::string problem_id = "fode_e0_common";
    std::string compute_backend = "cpu";
    std::string paper_protocol_id = "unregistered_cli_protocol";
    std::string training_artifact_id = "not_applicable";
    std::uint64_t seed = 20260728;
    std::uint64_t physical_fes_budget = 24000;
    int workers = 20;
    std::string sugga_model_root = "shared/models/sugga_cpp";
    std::string rlfode_model_root = "shared/models/fqfode_seeded";
    FqfodeSensitivityProfile fqfode_sensitivity_profile =
        FqfodeSensitivityProfile::baseline;
    int alga_attention_hidden_width = 1;
};

struct RunResult {
    std::string algorithm_id;
    std::string method_id;
    std::string algorithm_provenance;
    std::string effective_semantics_id;
    std::string problem_id;
    std::string problem_semantics_id;
    std::string paper_protocol_id;
    std::string training_artifact_id;
    std::string backend_id;
    std::string case_id;
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    std::uint64_t training_physical_fes = 0;
    std::uint64_t offline_training_physical_fes = 0;
    std::uint64_t inference_physical_fes = 0;
    std::uint64_t policy_interactions = 0;
    std::uint64_t policy_updates = 0;
    std::array<std::uint64_t, 4> policy_stage_interactions{};
    std::array<std::uint64_t, 4> policy_stage_updates{};
    int alga_attention_hidden_width = 0;
    std::uint64_t generations = 0;
    int initial_population = 0;
    int final_population = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    double best_expected_power_kw = 0.0;
    std::vector<int> best_layout_1based;
    double total_seconds = 0.0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double policy_training_seconds = 0.0;
    double policy_update_seconds = 0.0;
    std::string pso_update_semantics;
    std::string pretrained_artifact_hash;
    std::string learned_state_hash;
};

struct AlgorithmDescriptor {
    std::string id;
    std::string display_label;
    std::string paper_doi;
    std::string provenance;
    std::string semantics_id;
    std::vector<std::string> compatible_problem_ids;
};

struct AlgorithmState {
    std::string algorithm_semantic_id;
    std::uint64_t completed_physical_fes = 0;
    std::uint64_t generation = 0;
};

struct ProblemDescriptor {
    std::string id;
    std::string display_label;
    std::string semantics_id;
    std::string objective;
};

struct TrainingDescriptor {
    std::string id;
    std::string algorithm_id;
    std::string lifecycle;
    bool counts_physical_fes = false;
};

struct BackendDescriptor {
    std::string id;
    std::string capability;
    bool executable = false;
};

struct CompatibilityDescriptor {
    std::string algorithm_id;
    std::string problem_id;
    bool compatible = false;
    std::string reason;
};

const std::vector<AlgorithmDescriptor>& algorithm_descriptors();
const AlgorithmDescriptor& algorithm_descriptor(const std::string& id);
const std::vector<std::string>& algorithm_ids();
const std::vector<ProblemDescriptor>& problem_descriptors();
const ProblemDescriptor& problem_descriptor(const std::string& id);
const std::vector<TrainingDescriptor>& training_descriptors();
const std::vector<BackendDescriptor>& backend_descriptors();
const BackendDescriptor& backend_descriptor(const std::string& id);
CompatibilityDescriptor explain_compatibility(
    const std::string& algorithm_id,
    const std::string& problem_id
);
bool algorithm_supports_problem(
    const std::string& algorithm_id,
    const std::string& problem_id
);
RunResult optimize(const fode::CaseData& data, const RunConfig& config);
RunResult optimize_rlpso_reconstruction(
    const fode::CaseData& data,
    const RunConfig& config
);
RunResult optimize_rlpso_paper_corrected_training_reconstruction(
    const fode::CaseData& data,
    const RunConfig& config
);
RunResult optimize_alga_attention_declared_reconstruction(
    const fode::CaseData& data,
    const RunConfig& config
);

}  // namespace wflop
