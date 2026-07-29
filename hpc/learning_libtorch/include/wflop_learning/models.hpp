/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Plan-004 typed LibTorch learning-model and artifact bridge interface
Papers: TAAE DOI 10.1109/JAS.2026.126233; ALGA DOI 10.1016/j.swevo.2025.102018; RLPSO DOI 10.1016/j.energy.2024.134050
Public code: RLPSO author archive sha256 44e89c033e90f5aaaa9b84c826c95f29d3b8ad73dd363ff68de99418cdfa93a2; TAAE and ALGA author implementations unavailable
Provided components: paper-visible TAAE Transformer dimensions/losses, ALGA attention equations, and RLPSO actor/critic/PPO profile
Missing components and completions: exactly those frozen in shared/contracts/plan004_*_architecture.json
Semantic IDs: taae_transformer_declared_reconstruction_v1; alga_attention_declared_reconstruction_v1; rlpso_paper_corrected_training_reconstruction_v1
Claim boundary: typed M3 declared reconstructions and target optimization bridges; not author checkpoints or reproduction of reported numerical results
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <torch/torch.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wflop_learning {

struct TaaeConfig {
    std::int64_t vocabulary = 400;
    std::int64_t sequence_length = 15;
    std::int64_t model_dimension = 64;
    std::int64_t latent_dimension = 64;
    std::int64_t heads = 4;
    std::int64_t encoder_layers = 6;
    std::int64_t decoder_layers = 6;
    std::int64_t feed_forward_width = 256;
};

struct TaaeOutput {
    torch::Tensor logits;
    torch::Tensor latent;
    torch::Tensor regression;
};

struct TaaeLoss {
    torch::Tensor reconstruction;
    torch::Tensor regression;
    torch::Tensor metric_alignment;
    torch::Tensor total;
};

struct TaaeTransformerImpl : torch::nn::Module {
    explicit TaaeTransformerImpl(
        TaaeConfig config = {},
        std::uint64_t seed = 0x5943365441414501ULL
    );

    TaaeOutput forward(const torch::Tensor& tokens);
    torch::Tensor encode(const torch::Tensor& tokens);
    torch::Tensor decode_teacher(
        const torch::Tensor& latent,
        const torch::Tensor& tokens
    );
    torch::Tensor decode_argmax(const torch::Tensor& latent);
    const TaaeConfig& config() const noexcept;

    TaaeConfig config_;
    torch::nn::Embedding encoder_embedding{nullptr};
    torch::nn::Embedding decoder_embedding{nullptr};
    torch::nn::ModuleList encoder{nullptr};
    torch::nn::ModuleList decoder{nullptr};
    torch::nn::Linear latent_projection{nullptr};
    torch::nn::Linear memory_projection{nullptr};
    torch::nn::Linear vocabulary_projection{nullptr};
    torch::nn::Linear regression_hidden{nullptr};
    torch::nn::Linear regression_output{nullptr};
    torch::Tensor encoder_position;
    torch::Tensor decoder_position;
    torch::Tensor decoder_bos;
};
TORCH_MODULE(TaaeTransformer);

TaaeLoss taae_loss(
    const TaaeOutput& output,
    const torch::Tensor& tokens,
    const torch::Tensor& relative_fitness,
    std::uint64_t metric_pair_seed = 0x5943365041495253ULL
);

struct AlgaConfig {
    std::int64_t population_size = 30;
    std::int64_t turbine_count = 20;
    std::int64_t attention_heads = 8;
    std::int64_t projection_width = 1;
};

struct AlgaOutput {
    torch::Tensor prediction;
    torch::Tensor attention;
    torch::Tensor attended;
};

struct AlgaAttentionImpl : torch::nn::Module {
    explicit AlgaAttentionImpl(
        AlgaConfig config = {},
        std::uint64_t seed = 0x543435414c474101ULL
    );
    AlgaOutput forward(const torch::Tensor& population);
    const AlgaConfig& config() const noexcept;

    AlgaConfig config_;
    torch::Tensor query_weight;
    torch::Tensor key_weight;
    torch::Tensor value_weight;
    torch::Tensor output_weight;
    torch::Tensor output_bias;
};
TORCH_MODULE(AlgaAttention);

torch::Tensor alga_loss(
    const AlgaOutput& output,
    const torch::Tensor& normalized_fitness
);

struct RlpsoOutput {
    torch::Tensor logits;
    torch::Tensor probabilities;
    torch::Tensor value;
};

struct RlpsoActorCriticImpl : torch::nn::Module {
    explicit RlpsoActorCriticImpl(
        std::uint64_t seed = 0x543432524c505301ULL
    );
    RlpsoOutput forward(const torch::Tensor& state);

    torch::nn::Sequential actor{nullptr};
    torch::nn::Sequential critic{nullptr};
};
TORCH_MODULE(RlpsoActorCritic);

struct PpoBatch {
    torch::Tensor state;
    torch::Tensor action;
    torch::Tensor old_log_probability;
    torch::Tensor advantage;
    torch::Tensor returns;
};

struct PpoLoss {
    torch::Tensor actor;
    torch::Tensor critic;
    torch::Tensor entropy;
    torch::Tensor total;
};

inline constexpr double kRlpsoGamma = 0.99;
inline constexpr double kRlpsoClipEpsilon = 0.2;
inline constexpr double kRlpsoEntropyCoefficient = 0.01;
inline constexpr double kRlpsoValueCoefficient = 0.5;
inline constexpr std::int64_t kRlpsoUpdateEpochs = 80;
inline constexpr std::int64_t kRlpsoUpdateInterval = 500;

torch::Tensor rlpso_discounted_normalized_returns(
    const torch::Tensor& reward,
    const torch::Tensor& terminal
);

PpoLoss rlpso_ppo_loss(
    const RlpsoOutput& output,
    const PpoBatch& batch
);

enum class ModelKind : std::int64_t {
    Taae = 1,
    Alga = 2,
    Rlpso = 3,
};

struct ArtifactMetadata {
    ModelKind kind;
    std::uint64_t seed = 0;
    std::uint64_t optimizer_step = 0;
    std::uint64_t rollout_cursor = 0;
    std::uint64_t counter_rng_state = 0;
};

void save_artifact(
    torch::nn::Module& model,
    torch::optim::Optimizer& optimizer,
    const ArtifactMetadata& metadata,
    const std::string& path
);

ArtifactMetadata load_artifact(
    torch::nn::Module& model,
    torch::optim::Optimizer& optimizer,
    ModelKind expected_kind,
    const std::string& path,
    const torch::Device& device
);

std::string learned_state_hash(const torch::nn::Module& model);

torch::Tensor transfer_tensor(
    torch::Tensor tensor,
    const torch::Device& device,
    bool pinned_async
);

std::vector<torch::Tensor> transfer_bounded_queue(
    std::vector<torch::Tensor> tensors,
    const torch::Device& device,
    bool pinned_async,
    std::size_t queue_capacity = 2
);

void synchronize(const torch::Device& device);

torch::Tensor taae_optimization_transition(
    TaaeTransformer& model,
    const torch::Tensor& parent_tokens
);

torch::Tensor alga_optimization_transition(
    AlgaAttention& model,
    const torch::Tensor& population,
    std::int64_t grid_cardinality
);

struct RlpsoTransition {
    torch::Tensor action;
    torch::Tensor next_weights;
    torch::Tensor candidate;
};

RlpsoTransition rlpso_optimization_transition(
    RlpsoActorCritic& model,
    const torch::Tensor& state,
    const torch::Tensor& personal_best,
    const torch::Tensor& global_best,
    double counter_keyed_uniform_draw
);

}  // namespace wflop_learning
