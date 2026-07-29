/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Plan-004 independent direct-formula H5 reference tests
Evidence contracts: shared/contracts/plan004_taae_transformer_architecture.json; shared/contracts/plan004_alga_attention_architecture.json; shared/contracts/plan004_rlpso_ppo_architecture.json
Reference boundary: the reference path below evaluates frozen equations directly from cloned named tensors; it never calls TaaeTransformerImpl::forward, AlgaAttentionImpl::forward, RlpsoActorCriticImpl::forward, or any optimized transition helper
Comparison scope: complete forward tensors, component and total losses, every named gradient, one optimizer step, artifact reload, and one artifact-driven transition
Semantic IDs: taae_transformer_declared_reconstruction_v1; alga_attention_declared_reconstruction_v1; rlpso_paper_corrected_training_reconstruction_v1
Claim boundary: bounded CPU numerical equivalence only; not author-checkpoint, formal-quality, or H6 performance evidence
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "wflop_learning/models.hpp"

#include <torch/torch.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace wflop_learning;
using Parameters = std::map<std::string, torch::Tensor>;

struct ErrorLedger {
    double forward = 0.0;
    double loss = 0.0;
    double gradient = 0.0;
    double optimizer_step = 0.0;
    double artifact_reload = 0.0;
    double transition = 0.0;
    std::size_t named_parameters = 0;

    double maximum() const {
        return std::max(
            {
                forward,
                loss,
                gradient,
                optimizer_step,
                artifact_reload,
                transition,
            }
        );
    }
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

double maximum_error(
    const torch::Tensor& left,
    const torch::Tensor& right
) {
    require(left.sizes() == right.sizes(), "comparison shape mismatch");
    if (left.numel() == 0) {
        return 0.0;
    }
    if (
        left.scalar_type() == torch::kInt64
        || left.scalar_type() == torch::kBool
    ) {
        return torch::equal(left, right)
            ? 0.0
            : std::numeric_limits<double>::infinity();
    }
    return (left - right).abs().max().item<double>();
}

Parameters clone_parameters(torch::nn::Module& model) {
    Parameters result;
    for (const auto& item : model.named_parameters()) {
        torch::Tensor value =
            item.value().detach().clone().set_requires_grad(true);
        result.emplace(item.key(), std::move(value));
    }
    return result;
}

const torch::Tensor& parameter(
    const Parameters& parameters,
    const std::string& name
) {
    const auto found = parameters.find(name);
    if (found == parameters.end()) {
        throw std::runtime_error("reference parameter absent: " + name);
    }
    return found->second;
}

torch::Tensor direct_linear(
    const torch::Tensor& input,
    const Parameters& parameters,
    const std::string& prefix
) {
    return torch::matmul(
        input,
        parameter(parameters, prefix + ".weight").transpose(0, 1)
    ) + parameter(parameters, prefix + ".bias");
}

torch::Tensor direct_layer_norm(
    const torch::Tensor& input,
    const Parameters& parameters,
    const std::string& prefix
) {
    const torch::Tensor mean = input.mean(-1, true);
    const torch::Tensor centered = input - mean;
    const torch::Tensor variance =
        (centered * centered).mean(-1, true);
    return centered / torch::sqrt(variance + 1.0e-5)
        * parameter(parameters, prefix + ".weight")
        + parameter(parameters, prefix + ".bias");
}

torch::Tensor direct_embedding(
    const torch::Tensor& tokens,
    const torch::Tensor& weight
) {
    return weight.index_select(0, tokens.reshape({-1})).reshape(
        {tokens.size(0), tokens.size(1), weight.size(1)}
    );
}

torch::Tensor direct_multihead_attention(
    const torch::Tensor& query_input,
    const torch::Tensor& key_value_input,
    const Parameters& parameters,
    const std::string& prefix,
    std::int64_t heads,
    bool causal
) {
    const std::int64_t batch = query_input.size(0);
    const std::int64_t query_count = query_input.size(1);
    const std::int64_t key_count = key_value_input.size(1);
    const std::int64_t dimension = query_input.size(2);
    const std::int64_t head_dimension = dimension / heads;
    const torch::Tensor& packed_weight =
        parameter(parameters, prefix + ".in_proj_weight");
    const torch::Tensor& packed_bias =
        parameter(parameters, prefix + ".in_proj_bias");
    auto project = [&](const torch::Tensor& input, int part) {
        return torch::matmul(
            input,
            packed_weight
                .slice(0, part * dimension, (part + 1) * dimension)
                .transpose(0, 1)
        ) + packed_bias.slice(
            0,
            part * dimension,
            (part + 1) * dimension
        );
    };
    torch::Tensor query = project(query_input, 0)
        .reshape({batch, query_count, heads, head_dimension})
        .permute({0, 2, 1, 3});
    torch::Tensor key = project(key_value_input, 1)
        .reshape({batch, key_count, heads, head_dimension})
        .permute({0, 2, 1, 3});
    torch::Tensor value = project(key_value_input, 2)
        .reshape({batch, key_count, heads, head_dimension})
        .permute({0, 2, 1, 3});
    torch::Tensor scores = torch::matmul(
        query,
        key.transpose(-2, -1)
    ) / std::sqrt(static_cast<double>(head_dimension));
    if (causal) {
        torch::Tensor blocked = torch::ones(
            {query_count, key_count},
            torch::TensorOptions().dtype(torch::kBool)
        ).triu(1);
        scores = scores.masked_fill(
            blocked,
            -std::numeric_limits<double>::infinity()
        );
    }
    torch::Tensor probability = torch::softmax(scores, -1);
    torch::Tensor concatenated = torch::matmul(probability, value)
        .permute({0, 2, 1, 3})
        .reshape({batch, query_count, dimension});
    return direct_linear(
        concatenated,
        parameters,
        prefix + ".out_proj"
    );
}

torch::Tensor direct_encoder_layer(
    const torch::Tensor& input,
    const Parameters& parameters,
    const std::string& prefix,
    std::int64_t heads
) {
    torch::Tensor hidden = direct_layer_norm(
        input + direct_multihead_attention(
            input,
            input,
            parameters,
            prefix + ".self_attn",
            heads,
            false
        ),
        parameters,
        prefix + ".norm1"
    );
    torch::Tensor feed_forward = direct_linear(
        torch::relu(direct_linear(
            hidden,
            parameters,
            prefix + ".linear1"
        )),
        parameters,
        prefix + ".linear2"
    );
    return direct_layer_norm(
        hidden + feed_forward,
        parameters,
        prefix + ".norm2"
    );
}

torch::Tensor direct_decoder_layer(
    const torch::Tensor& input,
    const torch::Tensor& memory,
    const Parameters& parameters,
    const std::string& prefix,
    std::int64_t heads
) {
    torch::Tensor hidden = direct_layer_norm(
        input + direct_multihead_attention(
            input,
            input,
            parameters,
            prefix + ".self_attn",
            heads,
            true
        ),
        parameters,
        prefix + ".norm1"
    );
    hidden = direct_layer_norm(
        hidden + direct_multihead_attention(
            hidden,
            memory,
            parameters,
            prefix + ".multihead_attn",
            heads,
            false
        ),
        parameters,
        prefix + ".norm2"
    );
    torch::Tensor feed_forward = direct_linear(
        torch::relu(direct_linear(
            hidden,
            parameters,
            prefix + ".linear1"
        )),
        parameters,
        prefix + ".linear2"
    );
    return direct_layer_norm(
        hidden + feed_forward,
        parameters,
        prefix + ".norm3"
    );
}

torch::Tensor direct_taae_encode(
    const torch::Tensor& tokens,
    const Parameters& parameters,
    const TaaeConfig& config
) {
    torch::Tensor hidden = direct_embedding(
        tokens,
        parameter(parameters, "encoder_embedding.weight")
    ) + parameter(parameters, "encoder_position").unsqueeze(0);
    for (std::int64_t layer = 0;
         layer < config.encoder_layers;
         ++layer) {
        hidden = direct_encoder_layer(
            hidden,
            parameters,
            "encoder." + std::to_string(layer),
            config.heads
        );
    }
    return direct_linear(
        hidden.mean(1),
        parameters,
        "latent_projection"
    );
}

torch::Tensor direct_taae_decode_teacher(
    const torch::Tensor& latent,
    const torch::Tensor& tokens,
    const Parameters& parameters,
    const TaaeConfig& config
) {
    const std::int64_t batch = tokens.size(0);
    torch::Tensor embedded = direct_embedding(
        tokens,
        parameter(parameters, "decoder_embedding.weight")
    );
    torch::Tensor shifted = torch::cat(
        {
            parameter(parameters, "decoder_bos").expand(
                {batch, 1, config.model_dimension}
            ),
            embedded.slice(1, 0, config.sequence_length - 1),
        },
        1
    );
    torch::Tensor hidden =
        shifted + parameter(parameters, "decoder_position").unsqueeze(0);
    torch::Tensor memory = direct_linear(
        latent,
        parameters,
        "memory_projection"
    ).reshape(
        {
            batch,
            config.sequence_length,
            config.model_dimension,
        }
    );
    for (std::int64_t layer = 0;
         layer < config.decoder_layers;
         ++layer) {
        hidden = direct_decoder_layer(
            hidden,
            memory,
            parameters,
            "decoder." + std::to_string(layer),
            config.heads
        );
    }
    return direct_linear(
        hidden,
        parameters,
        "vocabulary_projection"
    );
}

TaaeOutput direct_taae_forward(
    const torch::Tensor& tokens,
    const Parameters& parameters,
    const TaaeConfig& config
) {
    torch::Tensor latent =
        direct_taae_encode(tokens, parameters, config);
    torch::Tensor normalized = latent / torch::sqrt(
        (latent * latent).sum(-1, true) + 1.0e-12
    );
    torch::Tensor regression = direct_linear(
        torch::relu(direct_linear(
            normalized,
            parameters,
            "regression_hidden"
        )),
        parameters,
        "regression_output"
    ).squeeze(-1);
    return {
        direct_taae_decode_teacher(
            latent,
            tokens,
            parameters,
            config
        ),
        latent,
        regression,
    };
}

std::pair<torch::Tensor, torch::Tensor> direct_metric_pairs(
    std::int64_t count,
    std::uint64_t seed
) {
    std::vector<std::int64_t> left(static_cast<std::size_t>(count));
    std::vector<std::int64_t> right(static_cast<std::size_t>(count));
    std::uint64_t state =
        seed ^ (static_cast<std::uint64_t>(count) << 32U);
    const auto next = [&state]() {
        state += 0x9e3779b97f4a7c15ULL;
        std::uint64_t value = state;
        value =
            (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value =
            (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    };
    for (std::int64_t pair = 0; pair < count; ++pair) {
        const std::int64_t first = static_cast<std::int64_t>(
            next() % static_cast<std::uint64_t>(count)
        );
        std::int64_t second = static_cast<std::int64_t>(
            next() % static_cast<std::uint64_t>(count - 1)
        );
        if (second >= first) {
            ++second;
        }
        left[static_cast<std::size_t>(pair)] = first;
        right[static_cast<std::size_t>(pair)] = second;
    }
    const auto options = torch::TensorOptions().dtype(torch::kInt64);
    return {
        torch::tensor(left, options),
        torch::tensor(right, options),
    };
}

TaaeLoss direct_taae_loss(
    const TaaeOutput& output,
    const torch::Tensor& tokens,
    const torch::Tensor& fitness,
    std::uint64_t pair_seed
) {
    torch::Tensor log_probability =
        torch::log_softmax(output.logits, -1);
    torch::Tensor reconstruction = -log_probability.gather(
        -1,
        tokens.unsqueeze(-1)
    ).mean();
    torch::Tensor regression =
        ((output.regression - fitness).pow(2)).mean();
    torch::Tensor normalized = output.latent / torch::sqrt(
        (output.latent * output.latent).sum(-1, true) + 1.0e-12
    );
    const auto [left, right] = direct_metric_pairs(
        normalized.size(0),
        pair_seed
    );
    torch::Tensor difference =
        normalized.index_select(0, left)
        - normalized.index_select(0, right);
    torch::Tensor latent_distance = torch::sqrt(
        (difference * difference).sum(-1) + 1.0e-12
    );
    torch::Tensor fitness_distance = torch::abs(
        fitness.index_select(0, left)
        - fitness.index_select(0, right)
    );
    torch::Tensor metric =
        ((latent_distance - fitness_distance).pow(2)).mean();
    return {
        reconstruction,
        regression,
        metric,
        reconstruction + 30.0 * regression + metric,
    };
}

torch::Tensor direct_taae_decode_argmax(
    const torch::Tensor& latent,
    const Parameters& parameters,
    const TaaeConfig& config
) {
    torch::Tensor tokens = torch::zeros(
        {latent.size(0), config.sequence_length},
        torch::TensorOptions().dtype(torch::kInt64)
    );
    for (std::int64_t position = 0;
         position < config.sequence_length;
         ++position) {
        torch::Tensor logits = direct_taae_decode_teacher(
            latent,
            tokens,
            parameters,
            config
        );
        tokens.select(1, position).copy_(
            logits.select(1, position).argmax(-1)
        );
    }
    return tokens;
}

torch::Tensor direct_taae_transition(
    const torch::Tensor& tokens,
    const Parameters& parameters,
    const TaaeConfig& config
) {
    torch::NoGradGuard no_grad;
    torch::Tensor latent =
        direct_taae_encode(tokens, parameters, config);
    torch::Tensor mutant = latent
        + 0.3 * (latent.roll({-1}, {0}) - latent.roll({-2}, {0}));
    torch::Tensor offspring = latent.clone();
    for (std::int64_t row = 0; row < offspring.size(0); ++row) {
        const std::int64_t coordinate = row % offspring.size(1);
        offspring.index_put_(
            {row, coordinate},
            mutant.index({row, coordinate})
        );
    }
    torch::Tensor decoded = direct_taae_decode_argmax(
        offspring,
        parameters,
        config
    ).contiguous();
    auto* values = decoded.data_ptr<std::int64_t>();
    for (std::int64_t row = 0; row < decoded.size(0); ++row) {
        std::vector<bool> used(
            static_cast<std::size_t>(config.vocabulary),
            false
        );
        for (std::int64_t column = 0;
             column < config.sequence_length;
             ++column) {
            std::int64_t value =
                values[row * config.sequence_length + column];
            value = std::clamp<std::int64_t>(
                value,
                0,
                config.vocabulary - 1
            );
            if (used[static_cast<std::size_t>(value)]) {
                value = 0;
                while (
                    value < config.vocabulary
                    && used[static_cast<std::size_t>(value)]
                ) {
                    ++value;
                }
            }
            require(
                value < config.vocabulary,
                "direct TAAE repair exhausted vocabulary"
            );
            used[static_cast<std::size_t>(value)] = true;
            values[row * config.sequence_length + column] = value;
        }
        std::sort(
            values + row * config.sequence_length,
            values + (row + 1) * config.sequence_length
        );
    }
    return decoded;
}

AlgaOutput direct_alga_forward(
    const torch::Tensor& population,
    const Parameters& parameters
) {
    torch::Tensor query = torch::matmul(
        population,
        parameter(parameters, "query_weight").transpose(0, 1)
    ).transpose(0, 1);
    torch::Tensor key = torch::matmul(
        population,
        parameter(parameters, "key_weight").transpose(0, 1)
    ).transpose(0, 1);
    torch::Tensor value = torch::matmul(
        population,
        parameter(parameters, "value_weight").transpose(0, 1)
    ).transpose(0, 1);
    torch::Tensor attention = torch::softmax(
        query.unsqueeze(2) * key.unsqueeze(1),
        2
    );
    torch::Tensor attended =
        torch::matmul(attention, value.unsqueeze(2)).squeeze(2);
    torch::Tensor prediction =
        (attended.transpose(0, 1)
         * parameter(parameters, "output_weight")).sum(1)
        + parameter(parameters, "output_bias").squeeze();
    return {prediction, attention, attended};
}

torch::Tensor direct_alga_transition(
    const torch::Tensor& population,
    const Parameters& parameters,
    std::int64_t grid_cardinality
) {
    torch::NoGradGuard no_grad;
    const AlgaOutput output =
        direct_alga_forward(population, parameters);
    torch::Tensor next = population.clone();
    const std::int64_t elite =
        output.prediction.argmax().item<std::int64_t>();
    const std::int64_t replaced =
        output.attention.mean(0).mean(0).argmin().item<std::int64_t>();
    const std::int64_t coordinate =
        replaced % population.size(1);
    next.index_put_(
        {replaced, coordinate},
        population.index({elite, coordinate})
    );
    return torch::remainder(
        next,
        static_cast<double>(grid_cardinality)
    );
}

RlpsoOutput direct_rlpso_forward(
    const torch::Tensor& state,
    const Parameters& parameters
) {
    torch::Tensor actor = torch::relu(
        direct_linear(state, parameters, "actor.0")
    );
    actor = torch::relu(
        direct_linear(actor, parameters, "actor.2")
    );
    torch::Tensor logits =
        direct_linear(actor, parameters, "actor.4");
    torch::Tensor critic = torch::relu(
        direct_linear(state, parameters, "critic.0")
    );
    critic = torch::relu(
        direct_linear(critic, parameters, "critic.2")
    );
    torch::Tensor value = torch::relu(
        direct_linear(critic, parameters, "critic.4")
    ).squeeze(-1);
    return {logits, torch::softmax(logits, -1), value};
}

torch::Tensor direct_discounted_returns(
    const torch::Tensor& reward,
    const torch::Tensor& terminal
) {
    const auto* reward_values = reward.data_ptr<double>();
    const auto* terminal_values = terminal.data_ptr<bool>();
    std::vector<double> values(
        static_cast<std::size_t>(reward.numel()),
        0.0
    );
    double running = 0.0;
    for (std::int64_t index = reward.numel(); index-- > 0;) {
        if (terminal_values[index]) {
            running = 0.0;
        }
        running = reward_values[index]
            + kRlpsoGamma * running;
        values[static_cast<std::size_t>(index)] = running;
    }
    torch::Tensor result = torch::tensor(
        values,
        torch::TensorOptions().dtype(torch::kFloat64)
    );
    return (result - result.mean())
        / (result.std(true) + 1.0e-5);
}

PpoLoss direct_ppo_loss(
    const RlpsoOutput& output,
    const PpoBatch& batch
) {
    torch::Tensor log_probability =
        torch::log_softmax(output.logits, -1)
            .gather(1, batch.action.unsqueeze(1))
            .squeeze(1);
    torch::Tensor ratio =
        torch::exp(log_probability - batch.old_log_probability);
    torch::Tensor unclipped = ratio * batch.advantage;
    torch::Tensor clipped = torch::clamp(
        ratio,
        1.0 - kRlpsoClipEpsilon,
        1.0 + kRlpsoClipEpsilon
    ) * batch.advantage;
    torch::Tensor entropy = -(
        output.probabilities
        * torch::log(torch::clamp_min(output.probabilities, 1.0e-12))
    ).sum(-1).mean();
    torch::Tensor actor =
        -torch::minimum(unclipped, clipped).mean()
        - kRlpsoEntropyCoefficient * entropy;
    torch::Tensor critic =
        kRlpsoValueCoefficient
        * ((output.value - batch.returns).pow(2)).mean();
    return {actor, critic, entropy, actor + critic};
}

RlpsoTransition direct_rlpso_transition(
    const torch::Tensor& state,
    const torch::Tensor& personal,
    const torch::Tensor& global,
    const Parameters& parameters,
    double draw
) {
    torch::NoGradGuard no_grad;
    const RlpsoOutput output =
        direct_rlpso_forward(state, parameters);
    torch::Tensor action =
        (output.probabilities.cumsum(-1) < draw)
            .sum(-1)
            .to(torch::kInt64);
    torch::Tensor next = state.clone();
    torch::Tensor step = torch::full_like(
        action,
        0.001,
        torch::TensorOptions().dtype(state.scalar_type())
    );
    next.select(1, 0).add_(
        torch::where(
            action.eq(0),
            step,
            torch::where(action.eq(2), -step, torch::zeros_like(step))
        )
    );
    next.select(1, 1).add_(
        torch::where(
            action.eq(3),
            step,
            torch::where(action.eq(1), -step, torch::zeros_like(step))
        )
    );
    next.clamp_(0.0, 1.0);
    torch::Tensor candidate =
        next.select(1, 0).unsqueeze(1) * personal
        + next.select(1, 1).unsqueeze(1) * global;
    return {action, next, candidate};
}

void compare_gradients(
    torch::nn::Module& optimized,
    const Parameters& reference,
    ErrorLedger& errors
) {
    const auto optimized_parameters = optimized.named_parameters();
    require(
        optimized_parameters.size() == reference.size(),
        "named parameter count mismatch"
    );
    for (const auto& item : optimized_parameters) {
        const torch::Tensor candidate_gradient = item.value().grad();
        const torch::Tensor reference_gradient =
            parameter(reference, item.key()).grad();
        require(
            candidate_gradient.defined()
                && reference_gradient.defined(),
            "undefined named gradient: " + item.key()
        );
        errors.gradient = std::max(
            errors.gradient,
            maximum_error(candidate_gradient, reference_gradient)
        );
    }
    errors.named_parameters = reference.size();
}

void compare_adam_step(
    torch::nn::Module& optimized,
    const Parameters& before,
    const Parameters& reference,
    ErrorLedger& errors,
    double learning_rate = 1.0e-3,
    double epsilon = 1.0e-8
) {
    for (const auto& item : optimized.named_parameters()) {
        const torch::Tensor gradient =
            parameter(reference, item.key()).grad();
        torch::Tensor expected = parameter(before, item.key())
            - learning_rate * gradient
                / (gradient.abs() + epsilon);
        errors.optimizer_step = std::max(
            errors.optimizer_step,
            maximum_error(item.value().detach(), expected.detach())
        );
    }
}

void compare_sgd_step(
    torch::nn::Module& optimized,
    const Parameters& before,
    const Parameters& reference,
    ErrorLedger& errors,
    double learning_rate = 1.0e-3
) {
    for (const auto& item : optimized.named_parameters()) {
        torch::Tensor expected = parameter(before, item.key())
            - learning_rate * parameter(reference, item.key()).grad();
        errors.optimizer_step = std::max(
            errors.optimizer_step,
            maximum_error(item.value().detach(), expected.detach())
        );
    }
}

Parameters detached_after_adam(
    const Parameters& before,
    const Parameters& reference,
    double learning_rate = 1.0e-3,
    double epsilon = 1.0e-8
) {
    Parameters result;
    for (const auto& [name, value] : before) {
        const torch::Tensor gradient = parameter(reference, name).grad();
        result.emplace(
            name,
            (
                value - learning_rate * gradient
                    / (gradient.abs() + epsilon)
            ).detach()
        );
    }
    return result;
}

Parameters detached_after_sgd(
    const Parameters& before,
    const Parameters& reference,
    double learning_rate = 1.0e-3
) {
    Parameters result;
    for (const auto& [name, value] : before) {
        result.emplace(
            name,
            (
                value
                - learning_rate * parameter(reference, name).grad()
            ).detach()
        );
    }
    return result;
}

std::filesystem::path artifact_path(const std::string& method) {
    return std::filesystem::temp_directory_path()
        / ("plan004_h5_reference_" + method + ".pt");
}

ErrorLedger test_taae() {
    constexpr std::uint64_t seed = 4001;
    constexpr std::uint64_t pair_seed = 0x5943365041495253ULL;
    constexpr double tolerance = 2.0e-9;
    const TaaeConfig config;
    TaaeTransformer model(config, seed);
    model->to(torch::kFloat64);
    torch::Tensor tokens = torch::arange(
        45,
        torch::TensorOptions().dtype(torch::kInt64)
    ).reshape({3, 15});
    torch::Tensor fitness = torch::tensor(
        {0.1, 0.6, 0.9},
        torch::TensorOptions().dtype(torch::kFloat64)
    );
    Parameters reference = clone_parameters(*model);
    Parameters before;
    for (const auto& [name, value] : reference) {
        before.emplace(name, value.detach().clone());
    }
    torch::optim::Adam optimizer(
        model->parameters(),
        torch::optim::AdamOptions(1.0e-3)
            .betas(std::make_tuple(0.9, 0.999))
            .eps(1.0e-8)
    );
    optimizer.zero_grad();
    const TaaeOutput optimized_output = model->forward(tokens);
    const TaaeOutput reference_output =
        direct_taae_forward(tokens, reference, config);
    ErrorLedger errors;
    errors.forward = std::max(
        {
            maximum_error(
                optimized_output.logits,
                reference_output.logits
            ),
            maximum_error(
                optimized_output.latent,
                reference_output.latent
            ),
            maximum_error(
                optimized_output.regression,
                reference_output.regression
            ),
        }
    );
    const TaaeLoss optimized_loss =
        taae_loss(optimized_output, tokens, fitness, pair_seed);
    const TaaeLoss reference_loss =
        direct_taae_loss(reference_output, tokens, fitness, pair_seed);
    errors.loss = std::max(
        {
            maximum_error(
                optimized_loss.reconstruction,
                reference_loss.reconstruction
            ),
            maximum_error(
                optimized_loss.regression,
                reference_loss.regression
            ),
            maximum_error(
                optimized_loss.metric_alignment,
                reference_loss.metric_alignment
            ),
            maximum_error(
                optimized_loss.total,
                reference_loss.total
            ),
        }
    );
    optimized_loss.total.backward();
    reference_loss.total.backward();
    compare_gradients(*model, reference, errors);
    optimizer.step();
    compare_adam_step(*model, before, reference, errors);
    Parameters stepped = detached_after_adam(before, reference);

    const std::filesystem::path artifact = artifact_path("taae");
    save_artifact(
        *model,
        optimizer,
        ArtifactMetadata{ModelKind::Taae, seed, 1, 0, seed},
        artifact.string()
    );
    TaaeTransformer restored(config, seed);
    restored->to(torch::kFloat64);
    torch::optim::Adam restored_optimizer(
        restored->parameters(),
        torch::optim::AdamOptions(1.0e-3)
    );
    static_cast<void>(load_artifact(
        *restored,
        restored_optimizer,
        ModelKind::Taae,
        artifact.string(),
        torch::Device(torch::kCPU)
    ));
    errors.artifact_reload = maximum_error(
        model->forward(tokens).logits.detach(),
        restored->forward(tokens).logits.detach()
    );
    errors.transition = maximum_error(
        taae_optimization_transition(restored, tokens),
        direct_taae_transition(tokens, stepped, config)
    );
    std::filesystem::remove(artifact);
    require(errors.maximum() <= tolerance, "TAAE H5 tolerance exceeded");
    return errors;
}

ErrorLedger test_alga() {
    constexpr std::uint64_t seed = 4501;
    constexpr double tolerance = 2.0e-10;
    AlgaAttention model(AlgaConfig{}, seed);
    model->to(torch::kFloat64);
    torch::Tensor population = torch::arange(
        600,
        torch::TensorOptions().dtype(torch::kFloat64)
    ).reshape({30, 20}) / 600.0 - 0.5;
    torch::Tensor fitness = torch::linspace(
        0.0,
        1.0,
        30,
        torch::TensorOptions().dtype(torch::kFloat64)
    );
    Parameters reference = clone_parameters(*model);
    Parameters before;
    for (const auto& [name, value] : reference) {
        before.emplace(name, value.detach().clone());
    }
    torch::optim::SGD optimizer(
        model->parameters(),
        torch::optim::SGDOptions(1.0e-3)
    );
    optimizer.zero_grad();
    const AlgaOutput optimized_output = model->forward(population);
    const AlgaOutput reference_output =
        direct_alga_forward(population, reference);
    ErrorLedger errors;
    errors.forward = std::max(
        {
            maximum_error(
                optimized_output.prediction,
                reference_output.prediction
            ),
            maximum_error(
                optimized_output.attention,
                reference_output.attention
            ),
            maximum_error(
                optimized_output.attended,
                reference_output.attended
            ),
        }
    );
    torch::Tensor optimized_loss =
        alga_loss(optimized_output, fitness);
    torch::Tensor reference_loss =
        ((reference_output.prediction - fitness).pow(2)).mean();
    errors.loss = maximum_error(optimized_loss, reference_loss);
    optimized_loss.backward();
    reference_loss.backward();
    compare_gradients(*model, reference, errors);
    optimizer.step();
    compare_sgd_step(*model, before, reference, errors);
    Parameters stepped = detached_after_sgd(before, reference);

    const std::filesystem::path artifact = artifact_path("alga");
    save_artifact(
        *model,
        optimizer,
        ArtifactMetadata{ModelKind::Alga, seed, 1, 0, seed},
        artifact.string()
    );
    AlgaAttention restored(AlgaConfig{}, seed);
    restored->to(torch::kFloat64);
    torch::optim::SGD restored_optimizer(
        restored->parameters(),
        torch::optim::SGDOptions(1.0e-3)
    );
    static_cast<void>(load_artifact(
        *restored,
        restored_optimizer,
        ModelKind::Alga,
        artifact.string(),
        torch::Device(torch::kCPU)
    ));
    errors.artifact_reload = maximum_error(
        model->forward(population).prediction.detach(),
        restored->forward(population).prediction.detach()
    );
    errors.transition = maximum_error(
        alga_optimization_transition(restored, population, 400),
        direct_alga_transition(population, stepped, 400)
    );
    std::filesystem::remove(artifact);
    require(errors.maximum() <= tolerance, "ALGA H5 tolerance exceeded");
    return errors;
}

ErrorLedger test_rlpso() {
    constexpr std::uint64_t seed = 4201;
    constexpr double tolerance = 2.0e-10;
    RlpsoActorCritic model(seed);
    model->to(torch::kFloat64);
    torch::Tensor state = torch::tensor(
        {{0.5, 0.5}, {0.51, 0.49}, {0.49, 0.51}, {0.52, 0.48}},
        torch::TensorOptions().dtype(torch::kFloat64)
    );
    torch::Tensor action = torch::tensor(
        {0, 1, 2, 3},
        torch::TensorOptions().dtype(torch::kInt64)
    );
    torch::Tensor old_log_probability = torch::full(
        {4},
        -std::log(4.0),
        torch::TensorOptions().dtype(torch::kFloat64)
    );
    torch::Tensor reward = torch::tensor(
        {0.2, -0.1, 0.4, 0.3},
        torch::TensorOptions().dtype(torch::kFloat64)
    );
    torch::Tensor terminal = torch::tensor(
        {false, false, false, true},
        torch::TensorOptions().dtype(torch::kBool)
    );
    torch::Tensor optimized_returns =
        rlpso_discounted_normalized_returns(reward, terminal);
    torch::Tensor reference_returns =
        direct_discounted_returns(reward, terminal);
    Parameters reference = clone_parameters(*model);
    Parameters before;
    for (const auto& [name, value] : reference) {
        before.emplace(name, value.detach().clone());
    }
    const RlpsoOutput optimized_output = model->forward(state);
    const RlpsoOutput reference_output =
        direct_rlpso_forward(state, reference);
    torch::Tensor advantage =
        reference_returns - reference_output.value.detach();
    const PpoBatch optimized_batch{
        state,
        action,
        old_log_probability,
        advantage,
        optimized_returns,
    };
    const PpoBatch reference_batch{
        state,
        action,
        old_log_probability,
        advantage,
        reference_returns,
    };
    const PpoLoss optimized_loss =
        rlpso_ppo_loss(optimized_output, optimized_batch);
    const PpoLoss reference_loss =
        direct_ppo_loss(reference_output, reference_batch);
    ErrorLedger errors;
    errors.forward = std::max(
        {
            maximum_error(
                optimized_output.logits,
                reference_output.logits
            ),
            maximum_error(
                optimized_output.probabilities,
                reference_output.probabilities
            ),
            maximum_error(
                optimized_output.value,
                reference_output.value
            ),
            maximum_error(optimized_returns, reference_returns),
        }
    );
    errors.loss = std::max(
        {
            maximum_error(
                optimized_loss.actor,
                reference_loss.actor
            ),
            maximum_error(
                optimized_loss.critic,
                reference_loss.critic
            ),
            maximum_error(
                optimized_loss.entropy,
                reference_loss.entropy
            ),
            maximum_error(
                optimized_loss.total,
                reference_loss.total
            ),
        }
    );
    torch::optim::Adam optimizer(
        model->parameters(),
        torch::optim::AdamOptions(1.0e-3)
            .betas(std::make_tuple(0.9, 0.999))
            .eps(1.0e-8)
    );
    optimizer.zero_grad();
    optimized_loss.total.backward();
    reference_loss.total.backward();
    compare_gradients(*model, reference, errors);
    optimizer.step();
    compare_adam_step(*model, before, reference, errors);
    Parameters stepped = detached_after_adam(before, reference);

    const std::filesystem::path artifact = artifact_path("rlpso");
    save_artifact(
        *model,
        optimizer,
        ArtifactMetadata{ModelKind::Rlpso, seed, 1, 4, seed},
        artifact.string()
    );
    RlpsoActorCritic restored(seed);
    restored->to(torch::kFloat64);
    torch::optim::Adam restored_optimizer(
        restored->parameters(),
        torch::optim::AdamOptions(1.0e-3)
    );
    static_cast<void>(load_artifact(
        *restored,
        restored_optimizer,
        ModelKind::Rlpso,
        artifact.string(),
        torch::Device(torch::kCPU)
    ));
    errors.artifact_reload = maximum_error(
        model->forward(state).logits.detach(),
        restored->forward(state).logits.detach()
    );
    torch::Tensor personal = torch::ones(
        {4, 6},
        torch::TensorOptions().dtype(torch::kFloat64)
    );
    torch::Tensor global = torch::full({4, 6}, 2.0, personal.options());
    const RlpsoTransition optimized_transition =
        rlpso_optimization_transition(
            restored,
            state,
            personal,
            global,
            0.375
        );
    const RlpsoTransition reference_transition =
        direct_rlpso_transition(
            state,
            personal,
            global,
            stepped,
            0.375
        );
    errors.transition = std::max(
        {
            maximum_error(
                optimized_transition.action,
                reference_transition.action
            ),
            maximum_error(
                optimized_transition.next_weights,
                reference_transition.next_weights
            ),
            maximum_error(
                optimized_transition.candidate,
                reference_transition.candidate
            ),
        }
    );
    std::filesystem::remove(artifact);
    require(errors.maximum() <= tolerance, "RLPSO H5 tolerance exceeded");
    return errors;
}

void print_result(
    const std::string& method,
    const ErrorLedger& errors,
    double absolute_tolerance
) {
    std::cout << std::setprecision(17)
              << '{'
              << "\"status\":\"pass\","
              << "\"method\":\"" << method << "\","
              << "\"reference\":\"independent_direct_formula_cpp\","
              << "\"does_not_call_candidate\":true,"
              << "\"named_parameter_count\":"
              << errors.named_parameters << ','
              << "\"maximum_absolute_error\":"
              << errors.maximum() << ','
              << "\"absolute_tolerance_per_tensor\":"
              << absolute_tolerance << ','
              << "\"errors\":{"
              << "\"forward_tensors\":" << errors.forward << ','
              << "\"losses\":" << errors.loss << ','
              << "\"all_named_parameter_gradients\":"
              << errors.gradient << ','
              << "\"one_optimizer_step\":"
              << errors.optimizer_step << ','
              << "\"artifact_reload\":"
              << errors.artifact_reload << ','
              << "\"artifact_driven_transition\":"
              << errors.transition
              << "},"
              << "\"tolerances\":{"
              << "\"forward_tensors\":" << absolute_tolerance << ','
              << "\"losses\":" << absolute_tolerance << ','
              << "\"all_named_parameter_gradients\":"
              << absolute_tolerance << ','
              << "\"one_optimizer_step\":"
              << absolute_tolerance << ','
              << "\"artifact_reload\":"
              << absolute_tolerance << ','
              << "\"artifact_driven_transition\":"
              << absolute_tolerance
              << "},"
              << "\"coverage\":{"
              << "\"forward_tensors\":\"passed\","
              << "\"losses\":\"passed\","
              << "\"all_named_parameter_gradients\":\"passed\","
              << "\"one_optimizer_step\":\"passed\","
              << "\"artifact_reload\":\"passed\","
              << "\"artifact_driven_transition\":\"passed\""
              << "}}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3 || std::string(argv[1]) != "--method") {
            throw std::invalid_argument(
                "usage: plan004_learning_reference_equivalence "
                "--method taae|alga|rlpso"
            );
        }
        torch::set_num_threads(1);
        const std::string method = argv[2];
        if (method == "taae") {
            print_result(method, test_taae(), 2.0e-9);
        } else if (method == "alga") {
            print_result(method, test_alga(), 2.0e-10);
        } else if (method == "rlpso") {
            print_result(method, test_rlpso(), 2.0e-10);
        } else {
            throw std::invalid_argument("unknown method");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
