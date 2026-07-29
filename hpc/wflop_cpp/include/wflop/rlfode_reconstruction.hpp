/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: public interface for seeded FQFODE training reconstruction
Paper title: A Reinforcement Learning-Assisted Fractional-Order Differential Evolution for Solving Wind Farm Layout Optimization Problems
DOI: 10.3390/math13182935
Paper provides: one aggregated 101 by 3 Qinit copied into four online stage tables, three actions, five agents, four rounds, and Q-learning hyperparameters
Public author code URL: no direct FQFODE code or pretrained Q-table archive was found by the bounded 2026-07-29 search in docs/source-dossiers/S04.json
Known missing information: author seeds, pretrained tables, exact pretraining environment/budget/reward, and two paper-internal conflicts recorded by the controlling contract
Reconstruction performed here: declared deterministic Qinit training on WS2tn50, four stage copies for formal online updates, and reuse of the audited FODE controlled-core interface
Method evidence tier: M3_DECLARED_COMPLETION
Method semantic ID: fqfode_seeded_training_declared_reconstruction_v1
Claim boundary: declared reconstruction only; no exact author-policy claim
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#pragma once

#include "wflop/algorithms.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace wflop::rlfode_reconstruction {

constexpr int kStateCount = 101;
constexpr int kActionCount = 3;
constexpr int kStageCount = 4;
constexpr double kLearningRate = 0.1;
constexpr double kDiscountFactor = 0.9;
constexpr double kEpsilon = 0.2;
constexpr double kMinimumFractionalOrder = 0.1;
constexpr double kMaximumFractionalOrder = 0.9;
constexpr double kInitialFractionalOrder = 0.8;
inline constexpr const char* kSharedTrainingCaseId = "WS2tn50";
inline constexpr const char* kSharedArtifactFilename =
    "fqfode_shared_ws2tn50_v1.qtable.tsv";

using QTable = std::array<
    double,
    static_cast<std::size_t>(kStateCount * kActionCount)
>;
using StageQTables = std::array<QTable, kStageCount>;

struct TrainingArtifact {
    std::string training_case_id;
    std::string seed_manifest_id;
    std::uint64_t training_physical_fes = 0;
    StageQTables tables{};
    std::string table_hash;
};

[[nodiscard]] int state_index(double fractional_order);
[[nodiscard]] double action_delta(int action_index);
[[nodiscard]] double additive_fractional_transition(
    double fractional_order,
    int action_index
);
[[nodiscard]] double fractional_history_value(
    double fractional_order,
    double current,
    const std::array<double, 4>& history,
    int available_history
);
void q_update(
    QTable& table,
    int state,
    int action,
    double reward,
    int next_state
);
[[nodiscard]] std::string qtable_hash(const StageQTables& tables);
[[nodiscard]] bool validate_policy_update_sequence_fixture();
[[nodiscard]] TrainingArtifact train_artifact(
    const fode::CaseData& data,
    int workers
);
void save_artifact(
    const TrainingArtifact& artifact,
    const std::string& path
);
[[nodiscard]] TrainingArtifact load_artifact(
    const std::string& path
);

}  // namespace wflop::rlfode_reconstruction

namespace wflop {

RunResult optimize_rlfode_seeded_training_reconstruction(
    const fode::CaseData& data,
    const RunConfig& config
);

}  // namespace wflop
