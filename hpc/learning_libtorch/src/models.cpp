/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Plan-004 typed LibTorch TAAE, ALGA, RLPSO models, losses, artifacts, and optimization transitions
Papers: TAAE DOI 10.1109/JAS.2026.126233; ALGA DOI 10.1016/j.swevo.2025.102018; RLPSO DOI 10.1016/j.energy.2024.134050
Public code: RLPSO author archive sha256 44e89c033e90f5aaaa9b84c826c95f29d3b8ad73dd363ff68de99418cdfa93a2; TAAE and ALGA author implementations unavailable
Provided components: TAAE six-layer encoder/decoder, four heads and three losses; ALGA Eqs. 19-23 and eight heads; RLPSO 2-256-64 actor/critic and PPO profile
Missing components and completions: exact choices are frozen and cited in shared/contracts/plan004_*_architecture.json
Semantic IDs: taae_transformer_declared_reconstruction_v1; alga_attention_declared_reconstruction_v1; rlpso_paper_corrected_training_reconstruction_v1
Claim boundary: M3 declared reconstruction, not author code/checkpoint replay or reproduction of reported results
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "wflop_learning/models.hpp"

#include <torch/cuda.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace wflop_learning {
namespace {

torch::Tensor causal_mask(
    std::int64_t sequence_length,
    const torch::TensorOptions& options
) {
    return torch::triu(
        torch::full(
            {sequence_length, sequence_length},
            -std::numeric_limits<double>::infinity(),
            options
        ),
        1
    );
}

torch::Tensor normalize_latent(const torch::Tensor& latent) {
    return latent / torch::sqrt(
        (latent * latent).sum(-1, true) + 1.0e-12
    );
}

std::uint64_t splitmix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::uint64_t stable_name_hash(const std::string& name) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char value : name) {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

double counter_uniform(
    std::uint64_t seed,
    const std::string& name,
    std::uint64_t index,
    double low,
    double high
) {
    const std::uint64_t bits = splitmix64(
        seed ^ stable_name_hash(name)
        ^ (index * 0x9e3779b97f4a7c15ULL)
    );
    const double unit = static_cast<double>(bits >> 11U)
        * (1.0 / 9007199254740992.0);
    return low + (high - low) * unit;
}

void counter_keyed_xavier_uniform_(
    torch::nn::Module& model,
    std::uint64_t seed
) {
    torch::NoGradGuard no_grad;
    for (const auto& item : model.named_parameters()) {
        torch::Tensor parameter = item.value();
        if (
            parameter.dim() == 1
            && item.key().find("norm") != std::string::npos
            && item.key().find("weight") != std::string::npos
        ) {
            parameter.fill_(1.0);
            continue;
        }
        if (
            parameter.dim() == 1
            && item.key().find("bias") != std::string::npos
        ) {
            parameter.zero_();
            continue;
        }
        const std::int64_t fan_in =
            parameter.dim() >= 2 ? parameter.size(-1) : parameter.numel();
        const std::int64_t fan_out =
            parameter.dim() >= 2 ? parameter.size(-2) : 1;
        const double limit = std::sqrt(
            6.0 / static_cast<double>(fan_in + fan_out)
        );
        torch::Tensor values = torch::empty(
            parameter.sizes(),
            torch::TensorOptions()
                .dtype(torch::kFloat64)
                .device(torch::kCPU)
        );
        auto* data = values.data_ptr<double>();
        for (std::int64_t index = 0; index < values.numel(); ++index) {
            data[index] = counter_uniform(
                seed,
                item.key(),
                static_cast<std::uint64_t>(index),
                -limit,
                limit
            );
        }
        parameter.copy_(
            values.to(parameter.device(), parameter.scalar_type())
        );
    }
}

void counter_keyed_kaiming_uniform_(
    torch::nn::Module& model,
    std::uint64_t seed
) {
    torch::NoGradGuard no_grad;
    for (const auto& item : model.named_parameters()) {
        torch::Tensor parameter = item.value();
        std::int64_t fan_in = 1;
        if (parameter.dim() >= 2) {
            fan_in = parameter.size(-1);
        } else {
            const std::string weight_name =
                item.key().substr(0, item.key().rfind('.') + 1) + "weight";
            const auto weights = model.named_parameters();
            if (weights.contains(weight_name)) {
                fan_in = weights[weight_name].size(-1);
            }
        }
        const double bound =
            1.0 / std::sqrt(static_cast<double>(fan_in));
        torch::Tensor values = torch::empty(
            parameter.sizes(),
            torch::TensorOptions()
                .dtype(torch::kFloat64)
                .device(torch::kCPU)
        );
        auto* data = values.data_ptr<double>();
        for (std::int64_t index = 0; index < values.numel(); ++index) {
            data[index] = counter_uniform(
                seed,
                item.key(),
                static_cast<std::uint64_t>(index),
                -bound,
                bound
            );
        }
        parameter.copy_(
            values.to(parameter.device(), parameter.scalar_type())
        );
    }
}

torch::Tensor metadata_tensor(
    const ArtifactMetadata& metadata
) {
    return torch::tensor(
        {
            static_cast<std::int64_t>(metadata.kind),
            static_cast<std::int64_t>(metadata.seed),
            static_cast<std::int64_t>(metadata.optimizer_step),
            static_cast<std::int64_t>(metadata.rollout_cursor),
            static_cast<std::int64_t>(metadata.counter_rng_state),
        },
        torch::TensorOptions().dtype(torch::kInt64).device(torch::kCPU)
    );
}

ArtifactMetadata parse_metadata(
    const torch::Tensor& stored,
    ModelKind expected_kind
) {
    const torch::Tensor value =
        stored.to(torch::kCPU, torch::kInt64).contiguous();
    if (value.dim() != 1 || value.numel() != 5) {
        throw std::runtime_error("invalid Plan-004 artifact metadata shape");
    }
    const auto* fields = value.const_data_ptr<std::int64_t>();
    if (fields[0] != static_cast<std::int64_t>(expected_kind)) {
        throw std::runtime_error("Plan-004 artifact semantic kind mismatch");
    }
    return ArtifactMetadata{
        expected_kind,
        static_cast<std::uint64_t>(fields[1]),
        static_cast<std::uint64_t>(fields[2]),
        static_cast<std::uint64_t>(fields[3]),
        static_cast<std::uint64_t>(fields[4]),
    };
}

std::string method_semantic_id(ModelKind kind) {
    if (kind == ModelKind::Taae) {
        return "taae_transformer_declared_reconstruction_v1";
    }
    if (kind == ModelKind::Alga) {
        return "alga_attention_declared_reconstruction_v1";
    }
    return "rlpso_paper_corrected_training_reconstruction_v1";
}

std::string problem_semantic_id(ModelKind kind) {
    if (kind == ModelKind::Taae) {
        return "taae_zhangbei_structured_declared_proxy_v1";
    }
    if (kind == ModelKind::Alga) {
        return "alga_guishan_3d_declared_proxy_v1";
    }
    return "rpso2024_source_problem_ws1_ws4_v1";
}

torch::Tensor configuration_tensor(
    ModelKind kind,
    const torch::nn::Module& model
) {
    std::vector<std::int64_t> fields;
    if (kind == ModelKind::Taae) {
        const auto* typed =
            dynamic_cast<const TaaeTransformerImpl*>(&model);
        if (typed == nullptr) {
            throw std::runtime_error("TAAE artifact module type mismatch");
        }
        const TaaeConfig& config = typed->config();
        fields = {
            config.vocabulary,
            config.sequence_length,
            config.model_dimension,
            config.latent_dimension,
            config.heads,
            config.encoder_layers,
            config.decoder_layers,
            config.feed_forward_width,
        };
    } else if (kind == ModelKind::Alga) {
        const auto* typed =
            dynamic_cast<const AlgaAttentionImpl*>(&model);
        if (typed == nullptr) {
            throw std::runtime_error("ALGA artifact module type mismatch");
        }
        const AlgaConfig& config = typed->config();
        fields = {
            config.population_size,
            config.turbine_count,
            config.attention_heads,
            config.projection_width,
        };
    } else {
        fields = {2, 256, 64, 4, 256, 64, 1};
    }
    return torch::tensor(
        fields,
        torch::TensorOptions().dtype(torch::kInt64)
    );
}

std::string parameter_hash(const torch::nn::Module& model) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto consume = [&](const unsigned char* bytes, std::size_t size) {
        for (std::size_t index = 0; index < size; ++index) {
            hash ^= static_cast<std::uint64_t>(bytes[index]);
            hash *= 1099511628211ULL;
        }
    };
    for (const auto& item : model.named_parameters()) {
        consume(
            reinterpret_cast<const unsigned char*>(item.key().data()),
            item.key().size()
        );
        const torch::Tensor values =
            item.value().detach().to(torch::kCPU, torch::kFloat64).contiguous();
        const auto* data = values.const_data_ptr<double>();
        for (std::int64_t index = 0; index < values.numel(); ++index) {
            const std::uint64_t bits =
                std::bit_cast<std::uint64_t>(data[index]);
            consume(
                reinterpret_cast<const unsigned char*>(&bits),
                sizeof(bits)
            );
        }
    }
    std::ostringstream result;
    result << "fnv1a64:" << std::hex << std::setfill('0')
           << std::setw(16) << hash;
    return result.str();
}

}  // namespace

TaaeTransformerImpl::TaaeTransformerImpl(
    TaaeConfig config,
    std::uint64_t seed
)
    : config_(config),
      encoder_embedding(register_module(
          "encoder_embedding",
          torch::nn::Embedding(config.vocabulary, config.model_dimension)
      )),
      decoder_embedding(register_module(
          "decoder_embedding",
          torch::nn::Embedding(config.vocabulary, config.model_dimension)
      )),
      encoder(register_module("encoder", torch::nn::ModuleList())),
      decoder(register_module("decoder", torch::nn::ModuleList())),
      latent_projection(register_module(
          "latent_projection",
          torch::nn::Linear(
              config.model_dimension,
              config.latent_dimension
          )
      )),
      memory_projection(register_module(
          "memory_projection",
          torch::nn::Linear(
              config.latent_dimension,
              config.sequence_length * config.model_dimension
          )
      )),
      vocabulary_projection(register_module(
          "vocabulary_projection",
          torch::nn::Linear(config.model_dimension, config.vocabulary)
      )),
      regression_hidden(register_module(
          "regression_hidden",
          torch::nn::Linear(config.latent_dimension, 64)
      )),
      regression_output(register_module(
          "regression_output",
          torch::nn::Linear(64, 1)
      )) {
    if (
        config_.vocabulary <= 0 || config_.sequence_length <= 0
        || config_.model_dimension <= 0 || config_.latent_dimension <= 0
        || config_.heads <= 0
        || config_.model_dimension % config_.heads != 0
        || config_.encoder_layers <= 0 || config_.decoder_layers <= 0
        || config_.feed_forward_width <= 0
    ) {
        throw std::invalid_argument("invalid TAAE Transformer dimensions");
    }
    encoder_position = register_parameter(
        "encoder_position",
        torch::empty({config_.sequence_length, config_.model_dimension})
    );
    decoder_position = register_parameter(
        "decoder_position",
        torch::empty({config_.sequence_length, config_.model_dimension})
    );
    decoder_bos = register_parameter(
        "decoder_bos",
        torch::empty({1, 1, config_.model_dimension})
    );
    for (std::int64_t layer = 0; layer < config_.encoder_layers; ++layer) {
        encoder->push_back(torch::nn::TransformerEncoderLayer(
            torch::nn::TransformerEncoderLayerOptions(
                config_.model_dimension,
                config_.heads
            )
                .dim_feedforward(config_.feed_forward_width)
                .dropout(0.0)
                .activation(torch::kReLU)
        ));
    }
    for (std::int64_t layer = 0; layer < config_.decoder_layers; ++layer) {
        decoder->push_back(torch::nn::TransformerDecoderLayer(
            torch::nn::TransformerDecoderLayerOptions(
                config_.model_dimension,
                config_.heads
            )
                .dim_feedforward(config_.feed_forward_width)
                .dropout(0.0)
                .activation(torch::kReLU)
        ));
    }
    counter_keyed_xavier_uniform_(*this, seed);
}

torch::Tensor TaaeTransformerImpl::encode(
    const torch::Tensor& tokens
) {
    if (
        tokens.dim() != 2
        || tokens.size(1) != config_.sequence_length
        || tokens.scalar_type() != torch::kInt64
    ) {
        throw std::invalid_argument("TAAE token tensor contract mismatch");
    }
    torch::Tensor hidden =
        encoder_embedding(tokens)
        + encoder_position.unsqueeze(0);
    hidden = hidden.transpose(0, 1);
    for (const auto& module : *encoder) {
        hidden =
            module->as<torch::nn::TransformerEncoderLayer>()->forward(hidden);
    }
    return latent_projection(hidden.mean(0));
}

torch::Tensor TaaeTransformerImpl::decode_teacher(
    const torch::Tensor& latent,
    const torch::Tensor& tokens
) {
    const std::int64_t batch = tokens.size(0);
    torch::Tensor embedded = decoder_embedding(tokens);
    torch::Tensor shifted = torch::cat(
        {
            decoder_bos.expand({batch, 1, config_.model_dimension}),
            embedded.slice(1, 0, config_.sequence_length - 1),
        },
        1
    );
    torch::Tensor target =
        (shifted + decoder_position.unsqueeze(0)).transpose(0, 1);
    torch::Tensor memory = memory_projection(latent)
        .view({batch, config_.sequence_length, config_.model_dimension})
        .transpose(0, 1);
    const torch::Tensor mask = causal_mask(
        config_.sequence_length,
        target.options()
    );
    for (const auto& module : *decoder) {
        target =
            module->as<torch::nn::TransformerDecoderLayer>()->forward(
                target,
                memory,
                mask
            );
    }
    return vocabulary_projection(target.transpose(0, 1));
}

TaaeOutput TaaeTransformerImpl::forward(
    const torch::Tensor& tokens
) {
    torch::Tensor latent = encode(tokens);
    torch::Tensor logits = decode_teacher(latent, tokens);
    torch::Tensor regression = regression_output(
        torch::relu(regression_hidden(normalize_latent(latent)))
    ).squeeze(-1);
    return {logits, latent, regression};
}

torch::Tensor TaaeTransformerImpl::decode_argmax(
    const torch::Tensor& latent
) {
    torch::Tensor tokens = torch::zeros(
        {latent.size(0), config_.sequence_length},
        torch::TensorOptions()
            .dtype(torch::kInt64)
            .device(latent.device())
    );
    for (std::int64_t position = 0;
         position < config_.sequence_length;
         ++position) {
        torch::Tensor logits = decode_teacher(latent, tokens);
        torch::Tensor selected =
            logits.select(1, position).argmax(-1);
        tokens.select(1, position).copy_(selected);
    }
    return tokens;
}

const TaaeConfig& TaaeTransformerImpl::config() const noexcept {
    return config_;
}

TaaeLoss taae_loss(
    const TaaeOutput& output,
    const torch::Tensor& tokens,
    const torch::Tensor& relative_fitness,
    std::uint64_t metric_pair_seed
) {
    torch::Tensor reconstruction =
        torch::nn::functional::cross_entropy(
            output.logits.reshape(
                {-1, output.logits.size(-1)}
            ),
            tokens.reshape({-1})
        );
    torch::Tensor regression =
        torch::mse_loss(output.regression, relative_fitness);
    torch::Tensor normalized = normalize_latent(output.latent);
    const std::int64_t count = normalized.size(0);
    if (count < 2) {
        torch::Tensor zero = output.latent.sum() * 0.0;
        torch::Tensor total = reconstruction + 30.0 * regression;
        return {reconstruction, regression, zero, total};
    }
    std::vector<std::int64_t> left(
        static_cast<std::size_t>(count)
    );
    std::vector<std::int64_t> right(
        static_cast<std::size_t>(count)
    );
    std::uint64_t state =
        metric_pair_seed ^ (static_cast<std::uint64_t>(count) << 32U);
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
    const torch::TensorOptions index_options =
        torch::TensorOptions()
            .dtype(torch::kInt64)
            .device(normalized.device());
    const torch::Tensor left_index = torch::tensor(left, index_options);
    const torch::Tensor right_index = torch::tensor(right, index_options);
    torch::Tensor left_latent = normalized.index_select(0, left_index);
    torch::Tensor paired_latent =
        normalized.index_select(0, right_index);
    torch::Tensor latent_distance = torch::linalg_vector_norm(
        left_latent - paired_latent,
        2,
        {-1},
        false
    );
    latent_distance = torch::sqrt(
        latent_distance * latent_distance + 1.0e-12
    );
    torch::Tensor fitness_distance = torch::abs(
        relative_fitness.index_select(0, left_index)
        - relative_fitness.index_select(0, right_index)
    );
    torch::Tensor metric_alignment =
        torch::mse_loss(latent_distance, fitness_distance);
    torch::Tensor total =
        reconstruction + 30.0 * regression + metric_alignment;
    return {reconstruction, regression, metric_alignment, total};
}

AlgaAttentionImpl::AlgaAttentionImpl(
    AlgaConfig config,
    std::uint64_t seed
)
    : config_(config) {
    if (
        config_.population_size <= 0 || config_.turbine_count <= 0
        || config_.attention_heads != 8
        || config_.projection_width != 1
    ) {
        throw std::invalid_argument("invalid ALGA attention dimensions");
    }
    query_weight = register_parameter(
        "query_weight",
        torch::empty({config_.attention_heads, config_.turbine_count})
    );
    key_weight = register_parameter(
        "key_weight",
        torch::empty({config_.attention_heads, config_.turbine_count})
    );
    value_weight = register_parameter(
        "value_weight",
        torch::empty({config_.attention_heads, config_.turbine_count})
    );
    output_weight = register_parameter(
        "output_weight",
        torch::empty({config_.attention_heads})
    );
    output_bias = register_parameter("output_bias", torch::zeros({1}));
    counter_keyed_xavier_uniform_(*this, seed);
}

AlgaOutput AlgaAttentionImpl::forward(
    const torch::Tensor& population
) {
    if (
        population.dim() != 2
        || population.size(0) != config_.population_size
        || population.size(1) != config_.turbine_count
    ) {
        throw std::invalid_argument("ALGA population tensor mismatch");
    }
    torch::Tensor query =
        torch::matmul(population, query_weight.transpose(0, 1))
            .transpose(0, 1);
    torch::Tensor key =
        torch::matmul(population, key_weight.transpose(0, 1))
            .transpose(0, 1);
    torch::Tensor value =
        torch::matmul(population, value_weight.transpose(0, 1))
            .transpose(0, 1);
    torch::Tensor scores =
        query.unsqueeze(2) * key.unsqueeze(1);
    torch::Tensor attention = torch::softmax(scores, 2);
    torch::Tensor attended = torch::matmul(
        attention,
        value.unsqueeze(2)
    ).squeeze(2);
    torch::Tensor prediction =
        (attended.transpose(0, 1) * output_weight).sum(1)
        + output_bias.squeeze();
    return {prediction, attention, attended};
}

const AlgaConfig& AlgaAttentionImpl::config() const noexcept {
    return config_;
}

torch::Tensor alga_loss(
    const AlgaOutput& output,
    const torch::Tensor& normalized_fitness
) {
    return torch::mse_loss(output.prediction, normalized_fitness);
}

RlpsoActorCriticImpl::RlpsoActorCriticImpl(std::uint64_t seed)
    : actor(register_module(
          "actor",
          torch::nn::Sequential(
              torch::nn::Linear(2, 256),
              torch::nn::ReLU(),
              torch::nn::Linear(256, 64),
              torch::nn::ReLU(),
              torch::nn::Linear(64, 4)
          )
      )),
      critic(register_module(
          "critic",
          torch::nn::Sequential(
              torch::nn::Linear(2, 256),
              torch::nn::ReLU(),
              torch::nn::Linear(256, 64),
              torch::nn::ReLU(),
              torch::nn::Linear(64, 1),
              torch::nn::ReLU()
          )
      )) {
    counter_keyed_kaiming_uniform_(*this, seed);
}

RlpsoOutput RlpsoActorCriticImpl::forward(
    const torch::Tensor& state
) {
    if (state.dim() != 2 || state.size(1) != 2) {
        throw std::invalid_argument("RLPSO state tensor mismatch");
    }
    torch::Tensor logits = actor->forward(state);
    return {
        logits,
        torch::softmax(logits, -1),
        critic->forward(state).squeeze(-1),
    };
}

PpoLoss rlpso_ppo_loss(
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
        * torch::mse_loss(output.value, batch.returns);
    return {actor, critic, entropy, actor + critic};
}

torch::Tensor rlpso_discounted_normalized_returns(
    const torch::Tensor& reward,
    const torch::Tensor& terminal
) {
    if (
        reward.dim() != 1 || terminal.dim() != 1
        || reward.numel() != terminal.numel() || reward.numel() == 0
    ) {
        throw std::invalid_argument("RLPSO rollout tensor mismatch");
    }
    const torch::Tensor reward_cpu =
        reward.detach().to(torch::kCPU, torch::kFloat64).contiguous();
    const torch::Tensor terminal_cpu =
        terminal.detach().to(torch::kCPU, torch::kBool).contiguous();
    const auto* rewards = reward_cpu.const_data_ptr<double>();
    const auto* terminals = terminal_cpu.const_data_ptr<bool>();
    std::vector<double> discounted(
        static_cast<std::size_t>(reward.numel()),
        0.0
    );
    double running = 0.0;
    for (std::int64_t index = reward.numel(); index-- > 0;) {
        if (terminals[index]) {
            running = 0.0;
        }
        running = rewards[index] + kRlpsoGamma * running;
        discounted[static_cast<std::size_t>(index)] = running;
    }
    const double count = static_cast<double>(discounted.size());
    double mean = 0.0;
    for (const double value : discounted) {
        mean += value;
    }
    mean /= count;
    double squared = 0.0;
    for (const double value : discounted) {
        const double centered = value - mean;
        squared += centered * centered;
    }
    const double denominator = discounted.size() > 1
        ? static_cast<double>(discounted.size() - 1)
        : 1.0;
    const double standard_deviation =
        std::sqrt(squared / denominator);
    for (double& value : discounted) {
        value = (value - mean) / (standard_deviation + 1.0e-5);
    }
    return torch::from_blob(
        discounted.data(),
        {static_cast<std::int64_t>(discounted.size())},
        torch::TensorOptions().dtype(torch::kFloat64)
    ).clone().to(reward.device(), reward.scalar_type());
}

void save_artifact(
    torch::nn::Module& model,
    torch::optim::Optimizer& optimizer,
    const ArtifactMetadata& metadata,
    const std::string& path
) {
    torch::serialize::OutputArchive root;
    root.write("plan004_schema", torch::tensor(
        {1},
        torch::TensorOptions().dtype(torch::kInt64)
    ));
    root.write("metadata", metadata_tensor(metadata));
    root.write(
        "method_semantic_id",
        c10::IValue(method_semantic_id(metadata.kind))
    );
    root.write(
        "problem_semantic_id",
        c10::IValue(problem_semantic_id(metadata.kind))
    );
    root.write(
        "model_config",
        configuration_tensor(metadata.kind, model)
    );
    root.write(
        "training_work",
        torch::tensor(
            {
                static_cast<std::int64_t>(metadata.optimizer_step),
                static_cast<std::int64_t>(metadata.rollout_cursor),
            },
            torch::TensorOptions().dtype(torch::kInt64)
        )
    );
    root.write(
        "learned_state_fnv1a64",
        c10::IValue(parameter_hash(model))
    );
    torch::serialize::OutputArchive model_archive;
    model.save(model_archive);
    root.write("model", model_archive);
    torch::serialize::OutputArchive optimizer_archive;
    optimizer.save(optimizer_archive);
    root.write("optimizer", optimizer_archive);
    root.save_to(path);
}

ArtifactMetadata load_artifact(
    torch::nn::Module& model,
    torch::optim::Optimizer& optimizer,
    ModelKind expected_kind,
    const std::string& path,
    const torch::Device& device
) {
    torch::serialize::InputArchive root;
    root.load_from(path, device);
    torch::Tensor schema;
    root.read("plan004_schema", schema);
    if (schema.to(torch::kCPU).item<std::int64_t>() != 1) {
        throw std::runtime_error("unsupported Plan-004 artifact schema");
    }
    torch::Tensor metadata;
    root.read("metadata", metadata);
    ArtifactMetadata parsed = parse_metadata(metadata, expected_kind);
    c10::IValue stored_method;
    root.read("method_semantic_id", stored_method);
    if (stored_method.toStringRef() != method_semantic_id(expected_kind)) {
        throw std::runtime_error("artifact method semantic ID mismatch");
    }
    c10::IValue stored_problem;
    root.read("problem_semantic_id", stored_problem);
    if (stored_problem.toStringRef() != problem_semantic_id(expected_kind)) {
        throw std::runtime_error("artifact problem semantic ID mismatch");
    }
    torch::Tensor stored_config;
    root.read("model_config", stored_config);
    if (
        !torch::equal(
            stored_config.to(torch::kCPU),
            configuration_tensor(expected_kind, model)
        )
    ) {
        throw std::runtime_error("artifact model config mismatch");
    }
    c10::IValue stored_hash;
    root.read("learned_state_fnv1a64", stored_hash);
    torch::serialize::InputArchive model_archive;
    root.read("model", model_archive);
    model.load(model_archive);
    if (stored_hash.toStringRef() != parameter_hash(model)) {
        throw std::runtime_error("artifact learned-state hash mismatch");
    }
    torch::serialize::InputArchive optimizer_archive;
    root.read("optimizer", optimizer_archive);
    optimizer.load(optimizer_archive);
    return parsed;
}

std::string learned_state_hash(const torch::nn::Module& model) {
    return parameter_hash(model);
}

torch::Tensor transfer_tensor(
    torch::Tensor tensor,
    const torch::Device& device,
    bool pinned_async
) {
    if (pinned_async && device.is_cuda()) {
        tensor = tensor.pin_memory();
    }
    return tensor.to(
        device,
        tensor.scalar_type(),
        pinned_async && device.is_cuda(),
        true
    );
}

std::vector<torch::Tensor> transfer_bounded_queue(
    std::vector<torch::Tensor> tensors,
    const torch::Device& device,
    bool pinned_async,
    std::size_t queue_capacity
) {
    if (queue_capacity == 0) {
        throw std::invalid_argument(
            "hybrid transfer queue capacity must be positive"
        );
    }
    std::vector<torch::Tensor> transferred;
    transferred.reserve(tensors.size());
    for (std::size_t begin = 0;
         begin < tensors.size();
         begin += queue_capacity) {
        const std::size_t end =
            std::min(begin + queue_capacity, tensors.size());
        for (std::size_t index = begin; index < end; ++index) {
            transferred.push_back(
                transfer_tensor(
                    std::move(tensors[index]),
                    device,
                    pinned_async
                )
            );
        }
        synchronize(device);
    }
    return transferred;
}

void synchronize(const torch::Device& device) {
    if (device.is_cuda()) {
        torch::cuda::synchronize(device.index());
    }
}

torch::Tensor taae_optimization_transition(
    TaaeTransformer& model,
    const torch::Tensor& parent_tokens
) {
    if (parent_tokens.size(0) < 3) {
        throw std::invalid_argument(
            "TAAE transition requires at least three parents"
        );
    }
    torch::NoGradGuard no_grad;
    torch::Tensor parent_latent = model->encode(parent_tokens);
    torch::Tensor mutant = parent_latent
        + 0.3 * (
            parent_latent.roll({-1}, {0})
            - parent_latent.roll({-2}, {0})
        );
    torch::Tensor offspring_latent = parent_latent.clone();
    for (std::int64_t row = 0; row < offspring_latent.size(0); ++row) {
        const std::int64_t coordinate =
            row % offspring_latent.size(1);
        offspring_latent.index_put_(
            {row, coordinate},
            mutant.index({row, coordinate})
        );
    }
    torch::Tensor decoded =
        model->decode_argmax(offspring_latent).to(torch::kCPU);
    auto* values = decoded.data_ptr<std::int64_t>();
    const std::int64_t vocabulary = model->config().vocabulary;
    const std::int64_t length = model->config().sequence_length;
    for (std::int64_t row = 0; row < decoded.size(0); ++row) {
        std::vector<bool> used(
            static_cast<std::size_t>(vocabulary),
            false
        );
        for (std::int64_t column = 0; column < length; ++column) {
            std::int64_t value = values[row * length + column];
            value = std::clamp<std::int64_t>(
                value,
                0,
                vocabulary - 1
            );
            if (used[static_cast<std::size_t>(value)]) {
                value = 0;
                while (
                    value < vocabulary
                    && used[static_cast<std::size_t>(value)]
                ) {
                    ++value;
                }
            }
            if (value >= vocabulary) {
                throw std::runtime_error("TAAE repair exhausted vocabulary");
            }
            used[static_cast<std::size_t>(value)] = true;
            values[row * length + column] = value;
        }
        std::sort(values + row * length, values + (row + 1) * length);
    }
    return decoded.to(parent_tokens.device());
}

torch::Tensor alga_optimization_transition(
    AlgaAttention& model,
    const torch::Tensor& population,
    std::int64_t grid_cardinality
) {
    if (grid_cardinality <= 0) {
        throw std::invalid_argument("ALGA grid cardinality must be positive");
    }
    torch::NoGradGuard no_grad;
    AlgaOutput output = model->forward(population);
    torch::Tensor next = population.clone();
    const std::int64_t elite =
        output.prediction.argmax().item<std::int64_t>();
    torch::Tensor received = output.attention.mean(0).mean(0);
    const std::int64_t replaced =
        received.argmin().item<std::int64_t>();
    const std::int64_t coordinate =
        replaced % population.size(1);
    next.index_put_(
        {replaced, coordinate},
        population.index({elite, coordinate})
    );
    return torch::remainder(next, static_cast<double>(grid_cardinality));
}

RlpsoTransition rlpso_optimization_transition(
    RlpsoActorCritic& model,
    const torch::Tensor& state,
    const torch::Tensor& personal_best,
    const torch::Tensor& global_best,
    double counter_keyed_uniform_draw
) {
    if (
        !(counter_keyed_uniform_draw >= 0.0)
        || !(counter_keyed_uniform_draw < 1.0)
    ) {
        throw std::invalid_argument(
            "RLPSO categorical draw must be in [0,1)"
        );
    }
    torch::NoGradGuard no_grad;
    RlpsoOutput output = model->forward(state);
    torch::Tensor cumulative =
        output.probabilities.cumsum(-1);
    torch::Tensor action = (
        cumulative
        < counter_keyed_uniform_draw
    ).sum(-1).to(torch::kInt64);
    torch::Tensor next = state.clone();
    const torch::Tensor step = torch::full_like(
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
        next.select(1, 0).unsqueeze(1) * personal_best
        + next.select(1, 1).unsqueeze(1) * global_best;
    return {action, next, candidate};
}

}  // namespace wflop_learning
