/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Seeded C++ PPO mathematical kernel for the T42 RLPSO reconstruction
Paper title: Reinforcement Learning-Based Particle Swarm Optimization for Wind Farm Layout Problems
DOI: 10.1016/j.energy.2024.134050
Paper provides: two-state/four-action PPO control, gamma=0.99, clip=0.2, Adam
  lr=0.001 with betas=(0.9,0.999), and K=80 policy-update epochs
Public author code URL: https://raw.githubusercontent.com/toyamaailab/toyamaailab.github.io/main/resource/RPSO_Wind_Code.zip
Public author code revision or archive hash: sha256:44e89c033e90f5aaaa9b84c826c95f29d3b8ad73dd363ff68de99418cdfa93a2
Public code provides: 2-256-64-4 ReLU/softmax actor, 2-256-64-1 ReLU
  critic, categorical sampling, discounted-return normalization, PPO loss,
  and the stated Adam/PPO hyperparameters
Known missing information: a frozen author policy/checkpoint and a complete
  cross-runtime seed lifecycle for the reported experiments
Known source conflicts: the public evaluate routine returns an argmax action
  index where PPO requires the sampled action log probability; the public
  training lifecycle is unseeded; the paper action step is 0.001 while the
  public environment executes 0.01
Reconstruction performed here: deterministic parameter initialization and
  categorical sampling keyed entirely by an externally supplied CounterRng,
  correct sampled-action log probabilities, clipped PPO gradients, discounted
  returns, Adam updates, and thread-count-invariant fixed-shard batch training
Method semantic ID: rlpso_paper_corrected_training_reconstruction_v1
Problem semantic ID: rpso2024_source_problem_ws1_ws4_v1
Controlling contract: shared/contracts/rlpso_reconstruction_execution_contract.json
Implementation authority/provenance: official author source plus paper
  equations, with declared corrections for the documented source conflicts
Method evidence tier: M3_DECLARED_COMPLETION
Claim boundary: reusable PPO mathematical kernel only; it is not an author
  checkpoint, author-policy replay, complete RLPSO integration, or reproduction
  of the paper's reported optimization results
Last evidence audit date: 2026-07-29
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#pragma once

#include "fode/rng.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace fode {
class PersistentExecutor;
}

namespace wflop::ppo {

struct Hyperparameters {
    static constexpr std::size_t state_dimension = 2;
    static constexpr std::size_t first_hidden_width = 256;
    static constexpr std::size_t second_hidden_width = 64;
    static constexpr std::size_t action_dimension = 4;

    double gamma = 0.99;
    double clip_epsilon = 0.2;
    double learning_rate = 0.001;
    double adam_beta1 = 0.9;
    double adam_beta2 = 0.999;
    double adam_epsilon = 1.0e-8;
    int update_epochs = 80;
    double value_loss_coefficient = 0.5;
    double entropy_coefficient = 0.01;
    bool normalize_returns = true;
    bool literal_source_argmax_logprob_bug = false;
};

struct RngKey {
    std::uint64_t generation = 0;
    std::uint64_t phase = 0;
    std::uint64_t individual = 0;
    std::uint64_t coordinate = 0;
    std::uint64_t draw = 0;
};

struct PolicyEvaluation {
    std::array<double, Hyperparameters::action_dimension> probabilities{};
    double value = 0.0;
};

struct ActionSample {
    int action = 0;
    double log_probability = 0.0;
    PolicyEvaluation evaluation;
};

struct Transition {
    std::array<double, Hyperparameters::state_dimension> state{};
    int action = 0;
    double old_log_probability = 0.0;
    double reward = 0.0;
    bool terminal = false;
};

struct TrainingReport {
    int epochs = 0;
    std::size_t transitions = 0;
    std::uint64_t adam_step = 0;
    double mean_return = 0.0;
    double actor_loss = 0.0;
    double critic_loss = 0.0;
    double entropy = 0.0;
};

struct ActorObjective {
    double loss = 0.0;
    double surrogate = 0.0;
    double entropy = 0.0;
    std::array<double, Hyperparameters::action_dimension> logit_gradient{};
};

struct CriticObjective {
    double loss = 0.0;
    double value_gradient = 0.0;
};

[[nodiscard]] std::vector<double> discounted_returns(
    const std::vector<Transition>& trajectory,
    double gamma
);

[[nodiscard]] double clipped_surrogate(
    double probability_ratio,
    double advantage,
    double clip_epsilon
);

[[nodiscard]] ActorObjective clipped_actor_objective(
    const std::array<double, Hyperparameters::action_dimension>& logits,
    int action,
    double old_log_probability,
    double advantage,
    double clip_epsilon,
    double entropy_coefficient
);

[[nodiscard]] CriticObjective critic_squared_error_objective(
    double value,
    double target,
    double value_loss_coefficient
);

class SeededPpo {
public:
    explicit SeededPpo(
        const fode::CounterRng& initialization_rng,
        std::uint64_t initialization_stream = 0,
        Hyperparameters hyperparameters = {}
    );
    ~SeededPpo();

    SeededPpo(const SeededPpo&) = delete;
    SeededPpo& operator=(const SeededPpo&) = delete;
    SeededPpo(SeededPpo&&) noexcept;
    SeededPpo& operator=(SeededPpo&&) noexcept;

    [[nodiscard]] const Hyperparameters& hyperparameters() const noexcept;
    [[nodiscard]] PolicyEvaluation evaluate(
        const std::array<double, Hyperparameters::state_dimension>& state
    ) const;
    [[nodiscard]] ActionSample sample_action(
        const std::array<double, Hyperparameters::state_dimension>& state,
        const fode::CounterRng& sampling_rng,
        const RngKey& key
    ) const;
    [[nodiscard]] TrainingReport update(
        const std::vector<Transition>& trajectory
    );
    [[nodiscard]] TrainingReport update_parallel(
        const std::vector<Transition>& trajectory,
        fode::PersistentExecutor& executor,
        int logical_shards = 20
    );

    [[nodiscard]] double parameter_checksum() const noexcept;
    [[nodiscard]] std::uint64_t parameter_hash() const noexcept;
    [[nodiscard]] std::uint64_t adam_step() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace wflop::ppo
