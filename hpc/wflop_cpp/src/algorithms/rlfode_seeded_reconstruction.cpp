/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: seeded FQFODE training reconstruction
Paper title: A Reinforcement Learning-Assisted Fractional-Order Differential Evolution for Solving Wind Farm Layout Optimization Problems
DOI: 10.3390/math13182935
Paper provides: one aggregated 101 by 3 initial Q-table copied into four online stage tables; actions {-0.01,0,+0.01}; alpha=0.1; gamma=0.9; epsilon=0.2; Algorithms 2-3; and FODE as the evolutionary predecessor
Public author code URL: no direct FQFODE code or pretrained-table archive was found by the bounded 2026-07-29 search recorded in docs/source-dossiers/S04.json
Public author code revision or archive hash: unavailable; target paper sha256:0b388fb055956837876f5cfffd8320ab88c0bf4ef32d386c2143a0e7b498c9a0
Public code/assets provide: archived FODE benchmark/source assets and the shared C++ Jensen/Park evaluator; no author FQFODE policy state
Known missing information: author training seeds, pretrained Q-tables, exact pretraining environment/budget/reward, adjudication of additive Algorithm 3 line 23 versus multiplicative Equation 24, and adjudication of shared-Qinit pseudocode versus independent-stage-pretraining prose
Reconstruction performed here: the audited FODE core is reused through a fractional-order controller; five agents and four rounds train one aggregated Qinit on declared fixed case WS2tn50; formal cases copy Qinit into four online stage tables and retain paper-described updates
Method evidence tier: M3_DECLARED_COMPLETION
Problem evidence tier: P0_AUTHOR_ASSET for fode_e0_common
Method semantic ID: fqfode_seeded_training_declared_reconstruction_v1
Problem semantic ID: fode_wflop_e0_legacy_v1
Step 11 sensitivity profiles are independent method semantics only. The
baseline semantic ID and behavior remain unchanged, and results from distinct
method semantics are never pooled or used for cross-semantic ranking.
Controlling contract: shared/contracts/fqfode_seeded_training_reconstruction_contract.json
Claim boundary: declared seeded reconstruction only; it is not the unavailable author-pretrained FQFODE policy and must not be reported as an exact reproduction of the paper results
Last evidence audit date: 2026-07-29
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "wflop/rlfode_reconstruction.hpp"

#include "fode/optimizer.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace wflop::rlfode_reconstruction {
namespace {

constexpr std::array<SensitivityProfileDescriptor, 5>
    kSensitivityProfiles{{
        {
            FqfodeSensitivityProfile::baseline,
            "baseline",
            "fqfode_seeded_training_declared_reconstruction_v1",
            kSharedArtifactFilename,
        },
        {
            FqfodeSensitivityProfile::multiplicative_action,
            "multiplicative-action",
            "fqfode_seeded_training_multiplicative_sensitivity_v1",
            kSharedArtifactFilename,
        },
        {
            FqfodeSensitivityProfile::fes_normalized_stage,
            "fes-normalized-stage",
            "fqfode_seeded_training_fes_normalized_stage_sensitivity_v1",
            kSharedArtifactFilename,
        },
        {
            FqfodeSensitivityProfile::wrap_after_generation_200,
            "wrap-after-generation-200",
            "fqfode_generation200_wrap_stage_sensitivity_v1",
            kSharedArtifactFilename,
        },
        {
            FqfodeSensitivityProfile::independent_stage_pretraining,
            "independent-stage-pretraining",
            "fqfode_independent_stage_pretraining_sensitivity_v1",
            "fqfode_independent_stage_ws2tn50_v1.qtable.tsv",
        },
    }};

constexpr std::array<double, kActionCount> kActions{-0.01, 0.0, 0.01};
constexpr int kTrainingAgents = 5;
constexpr int kTrainingRounds = 4;
constexpr int kPaperControlSteps = 200;
constexpr int kPretrainingInteractions = 101;
constexpr std::array<std::uint64_t, kTrainingAgents> kTrainingSeeds{
    2025091101ULL,
    2025091102ULL,
    2025091103ULL,
    2025091104ULL,
    2025091105ULL
};

std::size_t q_index(int state, int action) {
    return static_cast<std::size_t>(state * kActionCount + action);
}

double no_wake_expected_power(const fode::CaseData& data) {
    auto turbine_power = [](double velocity) {
        if (velocity < 2.0) {
            return 0.0;
        }
        if (velocity < 12.8) {
            return 0.3 * velocity * velocity * velocity;
        }
        if (velocity < 18.0) {
            return 629.1;
        }
        return 0.0;
    };
    double one_turbine = 0.0;
    const int directions = static_cast<int>(data.theta.size());
    const int speeds = static_cast<int>(data.velocity.size());
    for (int direction = 0; direction < directions; ++direction) {
        for (int speed = 0; speed < speeds; ++speed) {
            one_turbine += turbine_power(
                data.velocity[static_cast<std::size_t>(speed)]
            ) * data.probability[static_cast<std::size_t>(
                direction * speeds + speed
            )];
        }
    }
    return static_cast<double>(data.turbine_count) * one_turbine;
}

int choose_action(
    const QTable& table,
    int state,
    bool forced_no_change,
    const fode::CounterRng& rng,
    std::uint64_t control_step
) {
    if (forced_no_change) {
        return 1;
    }
    if (rng.uniform(control_step, 80, 0) < kEpsilon) {
        return rng.integer(
            0,
            kActionCount,
            control_step,
            81,
            0
        );
    }
    double maximum = -std::numeric_limits<double>::infinity();
    std::array<int, kActionCount> ties{};
    int tie_count = 0;
    for (int action = 0; action < kActionCount; ++action) {
        const double value = table[q_index(state, action)];
        if (value > maximum) {
            maximum = value;
            ties[0] = action;
            tie_count = 1;
        } else if (value == maximum) {
            ties[static_cast<std::size_t>(tie_count++)] = action;
        }
    }
    return ties[static_cast<std::size_t>(rng.integer(
        0,
        tie_count,
        control_step,
        82,
        0
    ))];
}

class QTableFractionalController final
    : public fode::FractionalOrderController {
public:
    QTableFractionalController(
        StageQTables& tables,
        const fode::CaseData& data,
        std::uint64_t seed,
        int forced_stage,
        bool offline_training,
        FqfodeSensitivityProfile profile =
            FqfodeSensitivityProfile::baseline,
        std::uint64_t initial_population = 0,
        std::uint64_t physical_fes_budget = 0
    )
        : tables_(tables),
          ideal_power_(no_wake_expected_power(data)),
          rng_(seed),
          forced_stage_(forced_stage),
          offline_training_(offline_training),
          profile_(profile),
          initial_population_(initial_population),
          physical_fes_budget_(physical_fes_budget) {
        if (!(ideal_power_ > 0.0)) {
            throw std::runtime_error(
                "FQFODE requires positive no-wake expected power"
            );
        }
    }

    double begin_generation(
        std::uint64_t generation,
        double best_expected_power_kw
    ) override {
        return begin_generation_with_fes(
            generation,
            best_expected_power_kw,
            initial_population_
        );
    }

    double begin_generation_with_fes(
        std::uint64_t generation,
        double best_expected_power_kw,
        std::uint64_t physical_fes_completed_before_generation
    ) override {
        const double normalized_best = best_expected_power_kw / ideal_power_;
        if (offline_training_) {
            active_stage_ = forced_stage_ >= 0 ? forced_stage_ : 0;
            active_state_ = state_index(fractional_order_);
            active_action_ = choose_action(
                tables_[static_cast<std::size_t>(active_stage_)],
                active_state_,
                false,
                rng_,
                generation
            );
            ++interactions_;
            ++stage_interactions_[static_cast<std::size_t>(active_stage_)];
            apply_action();
            training_before_normalized_best_ = normalized_best;
            return fractional_order_;
        }
        if (forced_stage_ >= 0) {
            active_stage_ = forced_stage_;
        } else if (
            profile_ == FqfodeSensitivityProfile::fes_normalized_stage
        ) {
            active_stage_ = fes_normalized_stage(
                physical_fes_completed_before_generation,
                initial_population_,
                physical_fes_budget_
            );
        } else if (
            profile_
                == FqfodeSensitivityProfile::wrap_after_generation_200
        ) {
            active_stage_ = static_cast<int>(
                ((generation - 1) / 50)
                % static_cast<std::uint64_t>(kStageCount)
            );
        } else {
            active_stage_ = std::min(
                kStageCount - 1,
                (static_cast<int>(
                    std::min<std::uint64_t>(
                        generation,
                        kPaperControlSteps
                    )
                ) - 1) / 50
            );
        }
        active_state_ = state_index(fractional_order_);
        active_action_ = choose_action(
            tables_[static_cast<std::size_t>(active_stage_)],
            active_state_,
            generation <= 6,
            rng_,
            generation
        );
        ++interactions_;
        ++stage_interactions_[static_cast<std::size_t>(active_stage_)];
        apply_action();
        if (generation >= 7 && have_previous_value_) {
            // Algorithm 3 uses the improvement already observable before
            // generation k: f_(k-1)-f_(k-2). It assigns that reward to the
            // action selected at generation k.
            const double reward = 100.0 * (
                normalized_best - previous_normalized_best_
            ) / std::max(
                1.0 - normalized_best + 1.0e-12,
                1.0e-12
            );
            q_update(
                tables_[static_cast<std::size_t>(active_stage_)],
                active_state_,
                active_action_,
                reward,
                state_index(fractional_order_)
            );
            ++updates_;
            ++stage_updates_[static_cast<std::size_t>(active_stage_)];
        }
        previous_normalized_best_ = normalized_best;
        have_previous_value_ = true;
        return fractional_order_;
    }

    void end_generation(
        std::uint64_t,
        double best_expected_power_kw
    ) override {
        if (!offline_training_) {
            return;
        }
        const double normalized_best =
            best_expected_power_kw / ideal_power_;
        // Algorithm 2 leaves the environment reward unspecified. The main
        // declared completion applies Equation 21 to the improvement caused
        // by the just-completed training interaction.
        const double reward = 100.0 * (
            normalized_best - training_before_normalized_best_
        ) / std::max(
            1.0 - normalized_best + 1.0e-12,
            1.0e-12
        );
        q_update(
            tables_[static_cast<std::size_t>(active_stage_)],
            active_state_,
            active_action_,
            reward,
            state_index(fractional_order_)
        );
        ++updates_;
        ++stage_updates_[static_cast<std::size_t>(active_stage_)];
    }

    void finish(double) override {
    }

    [[nodiscard]] std::uint64_t interactions() const {
        return interactions_;
    }

    [[nodiscard]] std::uint64_t updates() const {
        return updates_;
    }

    [[nodiscard]] const std::array<std::uint64_t, kStageCount>&
    stage_interactions() const {
        return stage_interactions_;
    }

    [[nodiscard]] const std::array<std::uint64_t, kStageCount>&
    stage_updates() const {
        return stage_updates_;
    }

private:
    void apply_action() {
        fractional_order_ =
            profile_ == FqfodeSensitivityProfile::multiplicative_action
            ? multiplicative_fractional_transition(
                fractional_order_,
                active_action_
            )
            : additive_fractional_transition(
                fractional_order_,
                active_action_
            );
    }

    StageQTables& tables_;
    double ideal_power_;
    fode::CounterRng rng_;
    int forced_stage_;
    bool offline_training_;
    FqfodeSensitivityProfile profile_;
    std::uint64_t initial_population_;
    std::uint64_t physical_fes_budget_;
    double fractional_order_ = kInitialFractionalOrder;
    int active_stage_ = 0;
    int active_state_ = state_index(kInitialFractionalOrder);
    int active_action_ = 1;
    double previous_normalized_best_ = 0.0;
    double training_before_normalized_best_ = 0.0;
    bool have_previous_value_ = false;
    std::uint64_t interactions_ = 0;
    std::uint64_t updates_ = 0;
    std::array<std::uint64_t, kStageCount> stage_interactions_{};
    std::array<std::uint64_t, kStageCount> stage_updates_{};
};

bool has_nonzero_value(const StageQTables& tables) {
    for (const auto& table : tables) {
        for (const double value : table) {
            if (value != 0.0) {
                return true;
            }
        }
    }
    return false;
}

bool has_identical_stage_initializers(const StageQTables& tables) {
    for (int stage = 1; stage < kStageCount; ++stage) {
        if (tables[static_cast<std::size_t>(stage)] != tables[0]) {
            return false;
        }
    }
    return true;
}

}  // namespace

const std::array<SensitivityProfileDescriptor, 5>&
sensitivity_profile_descriptors() {
    return kSensitivityProfiles;
}

const SensitivityProfileDescriptor& sensitivity_profile_descriptor(
    FqfodeSensitivityProfile profile
) {
    const auto found = std::find_if(
        kSensitivityProfiles.begin(),
        kSensitivityProfiles.end(),
        [profile](const SensitivityProfileDescriptor& descriptor) {
            return descriptor.profile == profile;
        }
    );
    if (found == kSensitivityProfiles.end()) {
        throw std::invalid_argument("unknown FQFODE sensitivity profile");
    }
    return *found;
}

FqfodeSensitivityProfile parse_sensitivity_profile(const std::string& id) {
    const auto found = std::find_if(
        kSensitivityProfiles.begin(),
        kSensitivityProfiles.end(),
        [&id](const SensitivityProfileDescriptor& descriptor) {
            return id == descriptor.id;
        }
    );
    if (found == kSensitivityProfiles.end()) {
        throw std::invalid_argument(
            "unknown FQFODE sensitivity profile ID: " + id
        );
    }
    return found->profile;
}

int state_index(double fractional_order) {
    const int state = static_cast<int>(std::floor(
        std::clamp(fractional_order, 0.0, 1.0) * 100.0 + 1.0e-12
    ));
    return std::clamp(state, 0, kStateCount - 1);
}

double action_delta(int action_index_value) {
    if (action_index_value < 0 || action_index_value >= kActionCount) {
        throw std::invalid_argument("FQFODE action index is out of range");
    }
    return kActions[static_cast<std::size_t>(action_index_value)];
}

double additive_fractional_transition(
    double fractional_order,
    int action_index_value
) {
    return std::clamp(
        fractional_order + action_delta(action_index_value),
        kMinimumFractionalOrder,
        kMaximumFractionalOrder
    );
}

double multiplicative_fractional_transition(
    double fractional_order,
    int action_index_value
) {
    return std::clamp(
        fractional_order * (1.0 + action_delta(action_index_value)),
        kMinimumFractionalOrder,
        kMaximumFractionalOrder
    );
}

int fes_normalized_stage(
    std::uint64_t physical_fes_completed_before_generation,
    std::uint64_t initial_population,
    std::uint64_t physical_fes_budget
) {
    if (physical_fes_budget <= initial_population
        || physical_fes_completed_before_generation < initial_population) {
        throw std::invalid_argument(
            "FQFODE FES-normalized stage requires an exact completed-FES "
            "counter and a budget beyond initialization"
        );
    }
    const std::uint64_t inference_span =
        physical_fes_budget - initial_population;
    const std::uint64_t progressed = std::min(
        physical_fes_completed_before_generation - initial_population,
        inference_span - 1
    );
    return std::min(
        kStageCount - 1,
        static_cast<int>(
            progressed * static_cast<std::uint64_t>(kStageCount)
            / inference_span
        )
    );
}

double fractional_history_value(
    double fractional_order,
    double current,
    const std::array<double, 4>& history,
    int available_history
) {
    if (available_history <= 0) {
        return current;
    }
    const double a = fractional_order;
    double value = a * current;
    if (available_history >= 1) {
        value += 0.5 * a * (1.0 - a) * history[0];
    }
    if (available_history >= 2) {
        value += (1.0 / 6.0) * a * (1.0 - a) * (2.0 - a)
            * history[1];
    }
    if (available_history >= 3) {
        value += (1.0 / 24.0) * a * (1.0 - a) * (2.0 - a)
            * (3.0 - a) * history[2];
    }
    if (available_history >= 4) {
        value += (1.0 / 120.0) * a * (1.0 - a) * (2.0 - a)
            * (3.0 - a) * (4.0 - a) * history[3];
    }
    return value;
}

void q_update(
    QTable& table,
    int state,
    int action,
    double reward,
    int next_state
) {
    if (state < 0 || state >= kStateCount
        || next_state < 0 || next_state >= kStateCount
        || action < 0 || action >= kActionCount) {
        throw std::invalid_argument("FQFODE Q-update index is out of range");
    }
    double next_max = -std::numeric_limits<double>::infinity();
    for (int candidate = 0; candidate < kActionCount; ++candidate) {
        next_max = std::max(
            next_max,
            table[q_index(next_state, candidate)]
        );
    }
    double& value = table[q_index(state, action)];
    value += kLearningRate * (
        reward + kDiscountFactor * next_max - value
    );
}

std::string qtable_hash(const StageQTables& tables) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& table : tables) {
        for (const double value : table) {
            const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
            for (int byte = 0; byte < 8; ++byte) {
                hash ^= (bits >> (8 * byte)) & 0xffULL;
                hash *= 1099511628211ULL;
            }
        }
    }
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0')
           << std::setw(16) << hash;
    return output.str();
}

bool validate_policy_update_sequence_fixture() {
    fode::CaseData data;
    data.case_id = "policy-sequence-fixture";
    data.turbine_count = 1;
    data.theta = {0.0};
    data.velocity = {10.0};
    data.probability = {1.0};
    StageQTables tables{};
    QTableFractionalController controller(
        tables,
        data,
        20260729ULL,
        0,
        false
    );
    const double ideal = no_wake_expected_power(data);
    for (std::uint64_t generation = 1; generation <= 6; ++generation) {
        controller.begin_generation(
            generation,
            ideal * (0.1 * static_cast<double>(generation))
        );
        if (controller.updates() != 0) {
            return false;
        }
    }
    controller.begin_generation(7, ideal * 0.7);
    if (controller.interactions() != 7 || controller.updates() != 1) {
        return false;
    }
    controller.finish(ideal * 0.8);
    if (controller.updates() != 1) {
        return false;
    }
    int nonzero = 0;
    double observed = 0.0;
    for (const double value : tables[0]) {
        if (value != 0.0) {
            ++nonzero;
            observed = value;
        }
    }
    const double expected_reward =
        100.0 * (0.7 - 0.6) / (1.0 - 0.7 + 1.0e-12);
    return nonzero == 1
        && std::abs(observed - kLearningRate * expected_reward) < 1.0e-12;
}

TrainingArtifact train_artifact(
    const fode::CaseData& data,
    int workers,
    FqfodeSensitivityProfile profile
) {
    if (workers <= 0) {
        throw std::invalid_argument(
            "FQFODE artifact training requires positive workers"
        );
    }
    if (data.case_id != kSharedTrainingCaseId) {
        throw std::invalid_argument(
            "FQFODE shared artifact must be trained on declared case "
            + std::string(kSharedTrainingCaseId)
        );
    }
    std::array<StageQTables, kTrainingAgents> agent_tables{};
    std::uint64_t training_fes = 0;
    const int initial_population = std::max(
        3,
        std::abs(77 - data.turbine_count)
    );
    const std::uint64_t episode_budget =
        static_cast<std::uint64_t>(initial_population)
        + static_cast<std::uint64_t>(kPretrainingInteractions)
            * static_cast<std::uint64_t>(initial_population + 1);
    const int trained_stage_count =
        profile
                == FqfodeSensitivityProfile::
                    independent_stage_pretraining
            ? kStageCount
            : 1;
    for (int stage = 0; stage < trained_stage_count; ++stage) {
        for (int round = 0; round < kTrainingRounds; ++round) {
            for (int agent = 0; agent < kTrainingAgents; ++agent) {
            const std::uint64_t seed =
                kTrainingSeeds[static_cast<std::size_t>(agent)]
                + static_cast<std::uint64_t>(round) * 1000003ULL
                + static_cast<std::uint64_t>(stage) * 1000033ULL;
            fode::RunConfig config;
            config.seed = seed;
            config.physical_fes_budget = episode_budget;
            config.workers = workers;
            config.maximum_generations = kPretrainingInteractions;
            QTableFractionalController controller(
                agent_tables[static_cast<std::size_t>(agent)],
                data,
                seed,
                stage,
                true,
                profile
            );
            const auto trained = fode::optimize_fode_hpc_controlled(
                data,
                config,
                controller
            );
            if (trained.generations != kPretrainingInteractions) {
                throw std::runtime_error(
                    "FQFODE pretraining did not complete 101 "
                    "state-action interactions"
                );
            }
            if (controller.interactions() != kPretrainingInteractions
                || controller.updates() != kPretrainingInteractions) {
                throw std::runtime_error(
                    "FQFODE pretraining interaction/update ledger drifted"
                );
            }
            training_fes += trained.physical_fes;
            }
        }
    }

    TrainingArtifact artifact;
    artifact.training_case_id = data.case_id;
    artifact.seed_manifest_id =
        profile
                == FqfodeSensitivityProfile::
                    independent_stage_pretraining
            ? "fqfode_independent_stage_seed_manifest_20250911_v1"
            : "fqfode_seed_manifest_20250911_v1";
    artifact.training_physical_fes = training_fes;
    for (int stage = 0; stage < kStageCount; ++stage) {
        QTable aggregated{};
        const int source_stage =
            profile
                    == FqfodeSensitivityProfile::
                        independent_stage_pretraining
                ? stage
                : 0;
        for (std::size_t index = 0; index < aggregated.size(); ++index) {
            double sum = 0.0;
            for (int agent = 0; agent < kTrainingAgents; ++agent) {
                sum += agent_tables[
                    static_cast<std::size_t>(agent)
                ][static_cast<std::size_t>(source_stage)][index];
            }
            aggregated[index] =
                sum / static_cast<double>(kTrainingAgents);
        }
        artifact.tables[static_cast<std::size_t>(stage)] = aggregated;
    }
    if (!has_nonzero_value(artifact.tables)) {
        throw std::runtime_error(
            "FQFODE training produced an uninformative all-zero artifact"
        );
    }
    artifact.table_hash = qtable_hash(artifact.tables);
    return artifact;
}

void save_artifact(
    const TrainingArtifact& artifact,
    const std::string& path
) {
    if (artifact.training_case_id != kSharedTrainingCaseId
        || artifact.seed_manifest_id.empty()
        || artifact.training_physical_fes == 0
        || !has_nonzero_value(artifact.tables)
        || (
            artifact.seed_manifest_id
                == "fqfode_seed_manifest_20250911_v1"
            && !has_identical_stage_initializers(artifact.tables)
        )
        || artifact.table_hash != qtable_hash(artifact.tables)) {
        throw std::invalid_argument(
            "cannot save an incomplete FQFODE training artifact"
        );
    }
    const std::filesystem::path target(path);
    if (!target.parent_path().empty()) {
        std::filesystem::create_directories(target.parent_path());
    }
    std::filesystem::path temporary = target;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) {
            throw std::runtime_error(
                "cannot write FQFODE artifact: " + temporary.string()
            );
        }
        output << std::setprecision(17);
        output << "FQFODE_QTABLE_V2\n";
        output << "training_case_id\t" << artifact.training_case_id << "\n";
        output << "seed_manifest_id\t"
               << artifact.seed_manifest_id << "\n";
        output << "training_physical_fes\t"
               << artifact.training_physical_fes << "\n";
        output << "table_hash\t" << artifact.table_hash << "\n";
        output << "stage\tstate\taction\tvalue\n";
        for (int stage = 0; stage < kStageCount; ++stage) {
            for (int state = 0; state < kStateCount; ++state) {
                for (int action = 0; action < kActionCount; ++action) {
                    output << stage << "\t" << state << "\t" << action
                           << "\t"
                           << artifact.tables[
                               static_cast<std::size_t>(stage)
                           ][q_index(state, action)]
                           << "\n";
                }
            }
        }
        output.flush();
        if (!output) {
            throw std::runtime_error(
                "cannot complete FQFODE artifact: " + temporary.string()
            );
        }
    }
    std::filesystem::rename(temporary, target);
}

TrainingArtifact load_artifact(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(
            "missing frozen FQFODE Q-table artifact: " + path
        );
    }
    std::string line;
    if (!std::getline(input, line) || line != "FQFODE_QTABLE_V2") {
        throw std::runtime_error("invalid FQFODE artifact version: " + path);
    }
    auto read_field = [&](const std::string& expected) {
        if (!std::getline(input, line)) {
            throw std::runtime_error(
                "truncated FQFODE artifact field: " + expected
            );
        }
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos || line.substr(0, tab) != expected) {
            throw std::runtime_error(
                "invalid FQFODE artifact field: " + expected
            );
        }
        return line.substr(tab + 1);
    };
    TrainingArtifact artifact;
    artifact.training_case_id = read_field("training_case_id");
    artifact.seed_manifest_id = read_field("seed_manifest_id");
    artifact.training_physical_fes = std::stoull(
        read_field("training_physical_fes")
    );
    artifact.table_hash = read_field("table_hash");
    if (artifact.training_case_id != kSharedTrainingCaseId) {
        throw std::runtime_error(
            "FQFODE artifact training-case mismatch: expected "
            + std::string(kSharedTrainingCaseId)
            + ", found " + artifact.training_case_id
        );
    }
    if (!std::getline(input, line)
        || line != "stage\tstate\taction\tvalue") {
        throw std::runtime_error(
            "invalid FQFODE artifact table header: " + path
        );
    }
    std::array<std::array<bool, kStateCount * kActionCount>, kStageCount>
        seen{};
    int count = 0;
    int stage = 0;
    int state = 0;
    int action = 0;
    double value = 0.0;
    while (input >> stage >> state >> action >> value) {
        if (stage < 0 || stage >= kStageCount
            || state < 0 || state >= kStateCount
            || action < 0 || action >= kActionCount
            || !std::isfinite(value)) {
            throw std::runtime_error(
                "invalid FQFODE artifact table row: " + path
            );
        }
        const std::size_t index = q_index(state, action);
        if (seen[static_cast<std::size_t>(stage)][index]) {
            throw std::runtime_error(
                "duplicate FQFODE artifact table row: " + path
            );
        }
        seen[static_cast<std::size_t>(stage)][index] = true;
        artifact.tables[static_cast<std::size_t>(stage)][index] = value;
        ++count;
    }
    const bool baseline_manifest =
        artifact.seed_manifest_id
        == "fqfode_seed_manifest_20250911_v1";
    const bool independent_manifest =
        artifact.seed_manifest_id
        == "fqfode_independent_stage_seed_manifest_20250911_v1";
    if (count != kStageCount * kStateCount * kActionCount
        || artifact.training_physical_fes == 0
        || (!baseline_manifest && !independent_manifest)
        || !has_nonzero_value(artifact.tables)
        || (baseline_manifest
            && !has_identical_stage_initializers(artifact.tables))
        || artifact.table_hash != qtable_hash(artifact.tables)) {
        throw std::runtime_error(
            "FQFODE artifact integrity check failed: " + path
        );
    }
    return artifact;
}

}  // namespace wflop::rlfode_reconstruction

namespace wflop {

RunResult optimize_rlfode_seeded_training_reconstruction(
    const fode::CaseData& data,
    const RunConfig& config
) {
    const std::uint64_t initial_population = static_cast<std::uint64_t>(
        std::max(3, std::abs(77 - data.turbine_count))
    );
    if (config.physical_fes_budget < initial_population
        || config.workers <= 0) {
        throw std::invalid_argument(
            "seeded FQFODE reconstruction requires a budget no smaller than "
            "the FODE initial population and a positive worker count"
        );
    }
    const auto started = std::chrono::steady_clock::now();
    const auto& sensitivity =
        rlfode_reconstruction::sensitivity_profile_descriptor(
            config.fqfode_sensitivity_profile
        );
    const std::filesystem::path artifact_path =
        std::filesystem::path(config.rlfode_model_root)
        / sensitivity.artifact_filename;
    const auto artifact = rlfode_reconstruction::load_artifact(
        artifact_path.string()
    );
    rlfode_reconstruction::StageQTables online_tables = artifact.tables;
    const std::uint64_t algorithm_seed = config.seed ^ 0x4651464f4445ULL;
    rlfode_reconstruction::QTableFractionalController controller(
        online_tables,
        data,
        algorithm_seed,
        -1,
        false,
        config.fqfode_sensitivity_profile,
        initial_population,
        config.physical_fes_budget
    );
    fode::RunConfig fode_config;
    fode_config.seed = algorithm_seed;
    fode_config.physical_fes_budget = config.physical_fes_budget;
    fode_config.workers = config.workers;
    const auto inference = fode::optimize_fode_hpc_controlled(
        data,
        fode_config,
        controller
    );

    RunResult result;
    result.algorithm_id = config.algorithm_id;
    result.method_id =
        config.fqfode_sensitivity_profile
                == FqfodeSensitivityProfile::baseline
            ? "FQFODE_SEEDED_TRAINING_DECLARED_RECONSTRUCTION_V1"
            : sensitivity.effective_semantics_id;
    const auto& identity = algorithm_descriptor(config.algorithm_id);
    const auto& problem = problem_descriptor(config.problem_id);
    result.algorithm_provenance = identity.provenance;
    result.effective_semantics_id =
        config.fqfode_sensitivity_profile
                == FqfodeSensitivityProfile::baseline
            ? identity.semantics_id
            : sensitivity.effective_semantics_id;
    result.problem_id = problem.id;
    result.problem_semantics_id = problem.semantics_id;
    result.case_id = data.case_id;
    result.seed = config.seed;
    result.physical_fes = inference.physical_fes;
    result.training_physical_fes = 0;
    result.offline_training_physical_fes =
        artifact.training_physical_fes;
    result.inference_physical_fes = inference.physical_fes;
    result.policy_interactions = controller.interactions();
    result.policy_updates = controller.updates();
    result.policy_stage_interactions = controller.stage_interactions();
    result.policy_stage_updates = controller.stage_updates();
    result.generations = inference.generations;
    result.initial_population = inference.initial_population;
    result.final_population = inference.final_population;
    result.requested_workers = config.workers;
    result.observed_workers = inference.observed_workers;
    result.best_expected_power_kw = inference.best_expected_power_kw;
    result.best_layout_1based = inference.best_layout_1based;
    result.total_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started
    ).count();
    result.evaluator_seconds = inference.evaluator_seconds;
    result.algorithm_seconds =
        std::max(0.0, result.total_seconds - result.evaluator_seconds);
    result.pretrained_artifact_hash = artifact.table_hash;
    result.learned_state_hash =
        rlfode_reconstruction::qtable_hash(online_tables);
    return result;
}

}  // namespace wflop
