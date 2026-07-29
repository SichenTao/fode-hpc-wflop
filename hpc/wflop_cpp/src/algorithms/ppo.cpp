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
Reconstruction performed here: deterministic Kaiming-uniform parameter
  initialization and categorical sampling keyed by an external CounterRng,
  sampled-action likelihood ratios, clipped PPO gradients, discounted returns,
  entropy regularization, value regression, bias-corrected Adam updates, and
  fixed logical-shard batch training with deterministic ordered reduction
Implementation authority/provenance: official author source plus paper
  equations, with declared corrections for the documented source conflicts
Method evidence tier: M3_DECLARED_COMPLETION
Claim boundary: reusable PPO mathematical kernel only; it is not an author
  checkpoint, author-policy replay, complete RLPSO integration, or reproduction
  of the paper's reported optimization results
Last evidence audit date: 2026-07-29
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "wflop/ppo.hpp"

#include "fode/executor.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace wflop::ppo {
namespace {

constexpr double minimum_probability =
    std::numeric_limits<double>::min();

void require_finite(double value, const char* name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(name) + " must be finite");
    }
}

void validate_hyperparameters(const Hyperparameters& parameters) {
    if (!(parameters.gamma >= 0.0 && parameters.gamma <= 1.0)) {
        throw std::invalid_argument("PPO gamma must be in [0, 1]");
    }
    if (!(parameters.clip_epsilon > 0.0
          && parameters.clip_epsilon < 1.0)) {
        throw std::invalid_argument("PPO clip epsilon must be in (0, 1)");
    }
    if (!(parameters.learning_rate > 0.0)) {
        throw std::invalid_argument("PPO learning rate must be positive");
    }
    if (!(parameters.adam_beta1 >= 0.0 && parameters.adam_beta1 < 1.0)
        || !(parameters.adam_beta2 >= 0.0
             && parameters.adam_beta2 < 1.0)) {
        throw std::invalid_argument("PPO Adam betas must be in [0, 1)");
    }
    if (!(parameters.adam_epsilon > 0.0)) {
        throw std::invalid_argument("PPO Adam epsilon must be positive");
    }
    if (parameters.update_epochs <= 0) {
        throw std::invalid_argument("PPO update epochs must be positive");
    }
    if (parameters.value_loss_coefficient < 0.0
        || parameters.entropy_coefficient < 0.0) {
        throw std::invalid_argument(
            "PPO loss coefficients must be nonnegative"
        );
    }
}

std::array<double, Hyperparameters::action_dimension> softmax(
    const std::vector<double>& logits
) {
    if (logits.size() != Hyperparameters::action_dimension) {
        throw std::logic_error("PPO actor emitted the wrong number of logits");
    }
    const double maximum =
        *std::max_element(logits.begin(), logits.end());
    std::array<double, Hyperparameters::action_dimension> probabilities{};
    double sum = 0.0;
    for (std::size_t index = 0; index < probabilities.size(); ++index) {
        probabilities[index] = std::exp(logits[index] - maximum);
        sum += probabilities[index];
    }
    if (!(sum > 0.0) || !std::isfinite(sum)) {
        throw std::runtime_error("PPO softmax normalization failed");
    }
    for (double& probability : probabilities) {
        probability /= sum;
    }
    return probabilities;
}

struct LayerCache {
    std::vector<double> input;
    std::vector<double> preactivation;
    std::vector<double> output;
};

struct NetworkCache {
    LayerCache first;
    LayerCache second;
    LayerCache third;
};

class DenseLayer {
public:
    DenseLayer(
        std::size_t input_width,
        std::size_t output_width,
        const fode::CounterRng& rng,
        std::uint64_t stream,
        std::uint64_t network_id,
        std::uint64_t layer_id
    )
        : input_width_(input_width),
          output_width_(output_width),
          weights_(input_width * output_width),
          biases_(output_width),
          weight_gradients_(weights_.size(), 0.0),
          bias_gradients_(biases_.size(), 0.0),
          weight_first_moment_(weights_.size(), 0.0),
          bias_first_moment_(biases_.size(), 0.0),
          weight_second_moment_(weights_.size(), 0.0),
          bias_second_moment_(biases_.size(), 0.0) {
        const double bound = 1.0 / std::sqrt(
            static_cast<double>(input_width_)
        );
        for (std::size_t index = 0; index < weights_.size(); ++index) {
            weights_[index] = bound * (
                2.0 * rng.uniform(
                    stream, network_id, layer_id,
                    static_cast<std::uint64_t>(index), 0
                ) - 1.0
            );
        }
        for (std::size_t index = 0; index < biases_.size(); ++index) {
            biases_[index] = bound * (
                2.0 * rng.uniform(
                    stream, network_id, layer_id,
                    static_cast<std::uint64_t>(index), 1
                ) - 1.0
            );
        }
    }

    [[nodiscard]] LayerCache forward(
        const std::vector<double>& input,
        bool relu
    ) const {
        if (input.size() != input_width_) {
            throw std::invalid_argument("PPO dense-layer input width mismatch");
        }
        LayerCache cache;
        cache.input = input;
        cache.preactivation.assign(output_width_, 0.0);
        cache.output.assign(output_width_, 0.0);
        for (std::size_t output = 0; output < output_width_; ++output) {
            double value = biases_[output];
            const std::size_t offset = output * input_width_;
            for (std::size_t input_index = 0;
                 input_index < input_width_;
                 ++input_index) {
                value += weights_[offset + input_index] * input[input_index];
            }
            cache.preactivation[output] = value;
            cache.output[output] = relu ? std::max(0.0, value) : value;
        }
        return cache;
    }

    [[nodiscard]] std::vector<double> backward(
        const LayerCache& cache,
        std::vector<double> output_gradient,
        bool relu
    ) {
        if (output_gradient.size() != output_width_) {
            throw std::invalid_argument(
                "PPO dense-layer output-gradient width mismatch"
            );
        }
        if (relu) {
            for (std::size_t output = 0;
                 output < output_width_;
                 ++output) {
                if (cache.preactivation[output] <= 0.0) {
                    output_gradient[output] = 0.0;
                }
            }
        }
        std::vector<double> input_gradient(input_width_, 0.0);
        for (std::size_t output = 0; output < output_width_; ++output) {
            const double gradient = output_gradient[output];
            bias_gradients_[output] += gradient;
            const std::size_t offset = output * input_width_;
            for (std::size_t input_index = 0;
                 input_index < input_width_;
                 ++input_index) {
                weight_gradients_[offset + input_index] +=
                    gradient * cache.input[input_index];
                input_gradient[input_index] +=
                    weights_[offset + input_index] * gradient;
            }
        }
        return input_gradient;
    }

    void clear_gradients() {
        std::fill(
            weight_gradients_.begin(), weight_gradients_.end(), 0.0
        );
        std::fill(
            bias_gradients_.begin(), bias_gradients_.end(), 0.0
        );
    }

    void scale_gradients(double scale) {
        for (double& gradient : weight_gradients_) {
            gradient *= scale;
        }
        for (double& gradient : bias_gradients_) {
            gradient *= scale;
        }
    }

    void adam_update(
        const Hyperparameters& parameters,
        std::uint64_t step
    ) {
        const double first_correction =
            1.0 - std::pow(parameters.adam_beta1, static_cast<double>(step));
        const double second_correction =
            1.0 - std::pow(parameters.adam_beta2, static_cast<double>(step));
        update_parameter_vector(
            weights_,
            weight_gradients_,
            weight_first_moment_,
            weight_second_moment_,
            parameters,
            first_correction,
            second_correction
        );
        update_parameter_vector(
            biases_,
            bias_gradients_,
            bias_first_moment_,
            bias_second_moment_,
            parameters,
            first_correction,
            second_correction
        );
    }

    [[nodiscard]] double checksum(std::uint64_t& index) const noexcept {
        double result = 0.0;
        for (const double value : weights_) {
            result += value * checksum_weight(index++);
        }
        for (const double value : biases_) {
            result += value * checksum_weight(index++);
        }
        return result;
    }

    void append_hash(std::uint64_t& value) const noexcept {
        for (const double parameter : weights_) {
            hash_double(value, parameter);
        }
        for (const double parameter : biases_) {
            hash_double(value, parameter);
        }
    }

private:
    std::size_t input_width_;
    std::size_t output_width_;
    std::vector<double> weights_;
    std::vector<double> biases_;
    std::vector<double> weight_gradients_;
    std::vector<double> bias_gradients_;
    std::vector<double> weight_first_moment_;
    std::vector<double> bias_first_moment_;
    std::vector<double> weight_second_moment_;
    std::vector<double> bias_second_moment_;

    static double checksum_weight(std::uint64_t index) noexcept {
        const std::uint64_t folded =
            (index * 11400714819323198485ULL) >> 48;
        return 1.0 + static_cast<double>(folded) / 65536.0;
    }

    static void hash_double(
        std::uint64_t& hash,
        double parameter
    ) noexcept {
        const std::uint64_t bits = std::bit_cast<std::uint64_t>(parameter);
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (bits >> (8 * byte)) & 0xffULL;
            hash *= 1099511628211ULL;
        }
    }

    static void update_parameter_vector(
        std::vector<double>& values,
        const std::vector<double>& gradients,
        std::vector<double>& first_moments,
        std::vector<double>& second_moments,
        const Hyperparameters& parameters,
        double first_correction,
        double second_correction
    ) {
        for (std::size_t index = 0; index < values.size(); ++index) {
            first_moments[index] =
                parameters.adam_beta1 * first_moments[index]
                + (1.0 - parameters.adam_beta1) * gradients[index];
            second_moments[index] =
                parameters.adam_beta2 * second_moments[index]
                + (1.0 - parameters.adam_beta2)
                    * gradients[index] * gradients[index];
            const double corrected_first =
                first_moments[index] / first_correction;
            const double corrected_second =
                second_moments[index] / second_correction;
            values[index] -= parameters.learning_rate * corrected_first
                / (
                    std::sqrt(corrected_second)
                    + parameters.adam_epsilon
                );
        }
    }
};

class Network {
public:
    Network(
        std::size_t output_width,
        bool final_relu,
        const fode::CounterRng& rng,
        std::uint64_t stream,
        std::uint64_t network_id
    )
        : first_(
              Hyperparameters::state_dimension,
              Hyperparameters::first_hidden_width,
              rng,
              stream,
              network_id,
              0
          ),
          second_(
              Hyperparameters::first_hidden_width,
              Hyperparameters::second_hidden_width,
              rng,
              stream,
              network_id,
              1
          ),
          third_(
              Hyperparameters::second_hidden_width,
              output_width,
              rng,
              stream,
              network_id,
              2
          ),
          final_relu_(final_relu) {}

    [[nodiscard]] NetworkCache forward(
        const std::array<double, Hyperparameters::state_dimension>& state
    ) const {
        for (const double value : state) {
            require_finite(value, "PPO state");
        }
        NetworkCache cache;
        cache.first = first_.forward(
            std::vector<double>(state.begin(), state.end()), true
        );
        cache.second = second_.forward(cache.first.output, true);
        cache.third = third_.forward(cache.second.output, final_relu_);
        return cache;
    }

    void backward(
        const NetworkCache& cache,
        const std::vector<double>& output_gradient
    ) {
        auto second_gradient =
            third_.backward(cache.third, output_gradient, final_relu_);
        auto first_gradient =
            second_.backward(cache.second, std::move(second_gradient), true);
        static_cast<void>(
            first_.backward(cache.first, std::move(first_gradient), true)
        );
    }

    void clear_gradients() {
        first_.clear_gradients();
        second_.clear_gradients();
        third_.clear_gradients();
    }

    void scale_gradients(double scale) {
        first_.scale_gradients(scale);
        second_.scale_gradients(scale);
        third_.scale_gradients(scale);
    }

    void adam_update(
        const Hyperparameters& parameters,
        std::uint64_t step
    ) {
        first_.adam_update(parameters, step);
        second_.adam_update(parameters, step);
        third_.adam_update(parameters, step);
    }

    [[nodiscard]] double checksum(std::uint64_t& index) const noexcept {
        return first_.checksum(index)
            + second_.checksum(index)
            + third_.checksum(index);
    }

    void append_hash(std::uint64_t& value) const noexcept {
        first_.append_hash(value);
        second_.append_hash(value);
        third_.append_hash(value);
    }

private:
    DenseLayer first_;
    DenseLayer second_;
    DenseLayer third_;
    bool final_relu_;
};

double categorical_entropy(
    const std::array<double, Hyperparameters::action_dimension>& probabilities
) {
    double entropy = 0.0;
    for (const double probability : probabilities) {
        if (probability > 0.0) {
            entropy -= probability * std::log(probability);
        }
    }
    return entropy;
}

std::vector<double> normalize(
    std::vector<double> values
) {
    if (values.empty()) {
        return values;
    }
    const double mean =
        std::accumulate(values.begin(), values.end(), 0.0)
        / static_cast<double>(values.size());
    double squared_deviation = 0.0;
    for (const double value : values) {
        const double deviation = value - mean;
        squared_deviation += deviation * deviation;
    }
    const double denominator =
        values.size() > 1
        ? static_cast<double>(values.size() - 1)
        : 1.0;
    const double standard_deviation =
        std::sqrt(squared_deviation / denominator);
    for (double& value : values) {
        value = (value - mean) / (standard_deviation + 1.0e-5);
    }
    return values;
}

}  // namespace

std::vector<double> discounted_returns(
    const std::vector<Transition>& trajectory,
    double gamma
) {
    if (!(gamma >= 0.0 && gamma <= 1.0)) {
        throw std::invalid_argument("discount gamma must be in [0, 1]");
    }
    std::vector<double> returns(trajectory.size(), 0.0);
    double discounted = 0.0;
    for (std::size_t reverse_index = trajectory.size();
         reverse_index > 0;
         --reverse_index) {
        const std::size_t index = reverse_index - 1;
        require_finite(trajectory[index].reward, "PPO reward");
        if (trajectory[index].terminal) {
            discounted = 0.0;
        }
        discounted =
            trajectory[index].reward + gamma * discounted;
        returns[index] = discounted;
    }
    return returns;
}

double clipped_surrogate(
    double probability_ratio,
    double advantage,
    double clip_epsilon
) {
    if (!(probability_ratio >= 0.0)
        || !std::isfinite(probability_ratio)) {
        throw std::invalid_argument(
            "PPO probability ratio must be finite and nonnegative"
        );
    }
    require_finite(advantage, "PPO advantage");
    if (!(clip_epsilon > 0.0 && clip_epsilon < 1.0)) {
        throw std::invalid_argument("PPO clip epsilon must be in (0, 1)");
    }
    const double clipped = std::clamp(
        probability_ratio,
        1.0 - clip_epsilon,
        1.0 + clip_epsilon
    );
    return std::min(
        probability_ratio * advantage,
        clipped * advantage
    );
}

ActorObjective clipped_actor_objective(
    const std::array<double, Hyperparameters::action_dimension>& logits,
    int action,
    double old_log_probability,
    double advantage,
    double clip_epsilon,
    double entropy_coefficient
) {
    if (action < 0
        || action >= static_cast<int>(Hyperparameters::action_dimension)) {
        throw std::invalid_argument("PPO actor action is outside [0, 4)");
    }
    require_finite(old_log_probability, "PPO old log probability");
    require_finite(advantage, "PPO advantage");
    if (entropy_coefficient < 0.0
        || !std::isfinite(entropy_coefficient)) {
        throw std::invalid_argument(
            "PPO entropy coefficient must be finite and nonnegative"
        );
    }
    for (const double logit : logits) {
        require_finite(logit, "PPO actor logit");
    }
    const auto probabilities = softmax(
        std::vector<double>(logits.begin(), logits.end())
    );
    const std::size_t selected_action = static_cast<std::size_t>(action);
    const double log_probability = std::log(std::max(
        probabilities[selected_action], minimum_probability
    ));
    const double ratio = std::exp(
        log_probability - old_log_probability
    );
    if (!std::isfinite(ratio)) {
        throw std::invalid_argument(
            "PPO likelihood ratio must remain finite"
        );
    }
    ActorObjective result;
    result.surrogate =
        clipped_surrogate(ratio, advantage, clip_epsilon);
    result.entropy = categorical_entropy(probabilities);
    result.loss =
        -result.surrogate - entropy_coefficient * result.entropy;
    const bool ratio_gradient_active =
        (advantage >= 0.0 && ratio <= 1.0 + clip_epsilon)
        || (advantage < 0.0 && ratio >= 1.0 - clip_epsilon);
    const double log_probability_gradient =
        ratio_gradient_active ? -ratio * advantage : 0.0;
    for (std::size_t output = 0;
         output < result.logit_gradient.size();
         ++output) {
        const double selected = output == selected_action ? 1.0 : 0.0;
        result.logit_gradient[output] =
            log_probability_gradient
            * (selected - probabilities[output]);
        result.logit_gradient[output] +=
            entropy_coefficient
            * probabilities[output]
            * (
                std::log(std::max(
                    probabilities[output], minimum_probability
                )) + result.entropy
            );
    }
    return result;
}

CriticObjective critic_squared_error_objective(
    double value,
    double target,
    double value_loss_coefficient
) {
    require_finite(value, "PPO critic value");
    require_finite(target, "PPO critic target");
    if (value_loss_coefficient < 0.0
        || !std::isfinite(value_loss_coefficient)) {
        throw std::invalid_argument(
            "PPO value-loss coefficient must be finite and nonnegative"
        );
    }
    const double error = value - target;
    return CriticObjective{
        value_loss_coefficient * error * error,
        2.0 * value_loss_coefficient * error
    };
}

class SeededPpo::Impl {
public:
    Impl(
        const fode::CounterRng& rng,
        std::uint64_t initialization_stream,
        Hyperparameters hyperparameters
    )
        : parameters_(hyperparameters),
          actor_(4, false, rng, initialization_stream, 0),
          critic_(1, true, rng, initialization_stream, 1) {
        validate_hyperparameters(parameters_);
    }

    [[nodiscard]] PolicyEvaluation evaluate(
        const std::array<double, Hyperparameters::state_dimension>& state
    ) const {
        const auto actor_cache = actor_.forward(state);
        const auto critic_cache = critic_.forward(state);
        PolicyEvaluation result;
        result.probabilities = softmax(actor_cache.third.output);
        result.value = critic_cache.third.output.front();
        return result;
    }

    [[nodiscard]] ActionSample sample_action(
        const std::array<double, Hyperparameters::state_dimension>& state,
        const fode::CounterRng& rng,
        const RngKey& key
    ) const {
        ActionSample result;
        result.evaluation = evaluate(state);
        const double draw = rng.uniform(
            key.generation,
            key.phase,
            key.individual,
            key.coordinate,
            key.draw
        );
        double cumulative = 0.0;
        result.action = static_cast<int>(
            Hyperparameters::action_dimension - 1
        );
        for (std::size_t action = 0;
             action < result.evaluation.probabilities.size();
             ++action) {
            cumulative += result.evaluation.probabilities[action];
            if (draw < cumulative) {
                result.action = static_cast<int>(action);
                break;
            }
        }
        result.log_probability = std::log(std::max(
            result.evaluation.probabilities[
                static_cast<std::size_t>(result.action)
            ],
            minimum_probability
        ));
        return result;
    }

    [[nodiscard]] TrainingReport update(
        const std::vector<Transition>& trajectory
    ) {
        if (trajectory.empty()) {
            throw std::invalid_argument(
                "PPO update requires at least one transition"
            );
        }
        for (const Transition& transition : trajectory) {
            if (transition.action < 0
                || transition.action
                    >= static_cast<int>(Hyperparameters::action_dimension)) {
                throw std::invalid_argument(
                    "PPO transition action is outside [0, 4)"
                );
            }
            require_finite(
                transition.old_log_probability,
                "PPO old log probability"
            );
            for (const double state_value : transition.state) {
                require_finite(state_value, "PPO transition state");
            }
        }
        const std::vector<double> raw_returns =
            discounted_returns(trajectory, parameters_.gamma);
        const double mean_return =
            std::accumulate(raw_returns.begin(), raw_returns.end(), 0.0)
            / static_cast<double>(raw_returns.size());
        const std::vector<double> targets =
            parameters_.normalize_returns
            ? normalize(raw_returns)
            : raw_returns;

        TrainingReport report;
        report.epochs = parameters_.update_epochs;
        report.transitions = trajectory.size();
        report.mean_return = mean_return;
        for (int epoch = 0; epoch < parameters_.update_epochs; ++epoch) {
            actor_.clear_gradients();
            critic_.clear_gradients();
            double actor_loss = 0.0;
            double critic_loss = 0.0;
            double entropy_total = 0.0;
            for (std::size_t index = 0;
                 index < trajectory.size();
                 ++index) {
                const Transition& transition = trajectory[index];
                const auto actor_cache = actor_.forward(transition.state);
                const auto critic_cache = critic_.forward(transition.state);
                const double value = critic_cache.third.output.front();
                const double advantage = targets[index] - value;
                std::array<
                    double,
                    Hyperparameters::action_dimension
                > logits{};
                std::copy_n(
                    actor_cache.third.output.begin(),
                    Hyperparameters::action_dimension,
                    logits.begin()
                );
                const ActorObjective actor_objective =
                    clipped_actor_objective(
                        logits,
                        transition.action,
                        transition.old_log_probability,
                        advantage,
                        parameters_.clip_epsilon,
                        parameters_.entropy_coefficient
                    );
                const CriticObjective critic_objective =
                    critic_squared_error_objective(
                        value,
                        targets[index],
                        parameters_.value_loss_coefficient
                    );
                actor_loss += actor_objective.loss;
                critic_loss += critic_objective.loss;
                entropy_total += actor_objective.entropy;
                actor_.backward(
                    actor_cache,
                    std::vector<double>(
                        actor_objective.logit_gradient.begin(),
                        actor_objective.logit_gradient.end()
                    )
                );
                critic_.backward(
                    critic_cache,
                    std::vector<double>{critic_objective.value_gradient}
                );
            }
            const double inverse_batch =
                1.0 / static_cast<double>(trajectory.size());
            actor_.scale_gradients(inverse_batch);
            critic_.scale_gradients(inverse_batch);
            ++adam_step_;
            actor_.adam_update(parameters_, adam_step_);
            critic_.adam_update(parameters_, adam_step_);
            report.actor_loss = actor_loss * inverse_batch;
            report.critic_loss = critic_loss * inverse_batch;
            report.entropy = entropy_total * inverse_batch;
        }
        report.adam_step = adam_step_;
        return report;
    }

    [[nodiscard]] double parameter_checksum() const noexcept {
        std::uint64_t index = 1;
        return actor_.checksum(index) + critic_.checksum(index);
    }

    [[nodiscard]] std::uint64_t parameter_hash() const noexcept {
        std::uint64_t value = 1469598103934665603ULL;
        actor_.append_hash(value);
        critic_.append_hash(value);
        return value;
    }

    Hyperparameters parameters_;
    Network actor_;
    Network critic_;
    std::uint64_t adam_step_ = 0;
};

SeededPpo::SeededPpo(
    const fode::CounterRng& initialization_rng,
    std::uint64_t initialization_stream,
    Hyperparameters hyperparameters
)
    : impl_(std::make_unique<Impl>(
          initialization_rng,
          initialization_stream,
          hyperparameters
      )) {}

SeededPpo::~SeededPpo() = default;
SeededPpo::SeededPpo(SeededPpo&&) noexcept = default;
SeededPpo& SeededPpo::operator=(SeededPpo&&) noexcept = default;

const Hyperparameters& SeededPpo::hyperparameters() const noexcept {
    return impl_->parameters_;
}

PolicyEvaluation SeededPpo::evaluate(
    const std::array<double, Hyperparameters::state_dimension>& state
) const {
    return impl_->evaluate(state);
}

ActionSample SeededPpo::sample_action(
    const std::array<double, Hyperparameters::state_dimension>& state,
    const fode::CounterRng& sampling_rng,
    const RngKey& key
) const {
    return impl_->sample_action(state, sampling_rng, key);
}

TrainingReport SeededPpo::update(
    const std::vector<Transition>& trajectory
) {
    return impl_->update(trajectory);
}

double SeededPpo::parameter_checksum() const noexcept {
    return impl_->parameter_checksum();
}

std::uint64_t SeededPpo::parameter_hash() const noexcept {
    return impl_->parameter_hash();
}

std::uint64_t SeededPpo::adam_step() const noexcept {
    return impl_->adam_step_;
}

}  // namespace wflop::ppo
