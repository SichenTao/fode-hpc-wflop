/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE declared-reconstruction Transformer model interface
Paper title: Transformer Autoencoder-Assisted Evolutionary Framework for Constrained Multiobjective 3D Wind Farm Layout Optimization
DOI: 10.1109/JAS.2026.126233
Public author method source/checkpoint: unavailable as recorded in docs/source-dossiers/Y36.json
Missing choices completed here: FFN width 256, post-norm, zero dropout, Xavier-uniform initialization, mean encoder pooling, separate encoder/decoder embeddings, deterministic metric-pair seed, per-parameter Adam age, checkpoint format, and exact fixed-order CPU batch execution
Reconstruction status: engineering reconstruction with declared completion choices
Method evidence tier: M3_DECLARED_COMPLETION
Problem evidence tier: P3_DECLARED_PROXY
Method semantic ID: taae_transformer_declared_reconstruction_v1
Problem semantic ID: taae_zhangbei_structured_declared_proxy_v1
Controlling contract: shared/contracts/taae_transformer_declared_reconstruction_contract.json
Claim boundary: trainable mathematical kernel only; the distinct evolution reconstruction is governed separately, original taae remains blocked, and no author-result, optimizer, performance, or GPU claim is made
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fode {
class PersistentExecutor;
}

namespace taae {

struct ModelConfig {
    int vocabulary = 400;
    int sequence_length = 15;
    int model_dimension = 64;
    int latent_dimension = 64;
    int heads = 4;
    int encoder_layers = 6;
    int decoder_layers = 6;
    int ffn_width = 256;
    int regression_hidden_width = 64;
    double dropout = 0.0;
};

struct TrainingProfile {
    std::string id;
    std::uint64_t corpus_seed = 0;
    std::uint64_t split_seed = 0;
    std::uint64_t batch_seed = 0;
    std::uint64_t fine_tune_seed = 0;
    std::uint64_t corpus_layouts = 0;
    std::uint64_t train_layouts = 0;
    std::uint64_t test_layouts = 0;
    int pretraining_epochs = 0;
    int pretraining_batch_size = 0;
    int fine_tuning_epochs_per_generation = 10;
    int fine_tuning_batch_size = 0;
    double learning_rate = 1.0e-3;
    double beta1 = 0.9;
    double beta2 = 0.999;
    double epsilon = 1.0e-8;
};

struct TrainingWork {
    std::uint64_t corpus_samples = 0;
    std::uint64_t token_operations = 0;
    std::uint64_t optimizer_steps = 0;
    std::uint64_t pretraining_epochs = 0;
    std::uint64_t fine_tuning_epochs = 0;
    std::uint64_t training_physical_fes = 0;
    double wall_seconds = 0.0;
};

struct LossWeights {
    double reconstruction = 1.0;
    double regression = 30.0;
    double metric_smoothness = 1.0;
    std::uint64_t metric_pair_seed = 0x5943365041495253ULL;
};

struct BatchLoss {
    double reconstruction = 0.0;
    double regression = 0.0;
    double metric_smoothness = 0.0;
    double total = 0.0;
};

struct CheckpointMetadata {
    std::string method_semantic_id;
    std::string problem_semantic_id;
    std::string training_profile_id;
    std::uint64_t initialization_seed = 0;
    TrainingWork work;
    std::string parameter_fnv1a64;
    std::string file_sha256;
};

class TransformerAutoencoder {
public:
    TransformerAutoencoder(ModelConfig config, std::uint64_t seed);
    TransformerAutoencoder(TransformerAutoencoder&&) noexcept;
    TransformerAutoencoder& operator=(TransformerAutoencoder&&) noexcept;
    ~TransformerAutoencoder();

    TransformerAutoencoder(const TransformerAutoencoder&) = delete;
    TransformerAutoencoder& operator=(const TransformerAutoencoder&) = delete;

    [[nodiscard]] const ModelConfig& config() const noexcept;
    [[nodiscard]] std::vector<double> encode(
        const std::vector<int>& tokens
    ) const;
    [[nodiscard]] std::vector<int> decode_argmax(
        const std::vector<double>& latent
    ) const;
    [[nodiscard]] bool argmax_softmax_equivalence_fixture(
        const std::vector<double>& latent
    ) const;
    [[nodiscard]] double reconstruction_loss(
        const std::vector<std::vector<int>>& layouts,
        fode::PersistentExecutor* executor = nullptr
    ) const;
    BatchLoss train_batch(
        const std::vector<std::vector<int>>& layouts,
        const std::vector<double>& relative_fitness,
        const LossWeights& weights,
        double learning_rate,
        double beta1,
        double beta2,
        double epsilon,
        bool freeze_decoder,
        fode::PersistentExecutor* executor = nullptr
    );

    [[nodiscard]] std::string parameter_hash() const;
    CheckpointMetadata save_checkpoint(
        const std::string& path,
        const std::string& training_profile_id,
        std::uint64_t initialization_seed,
        const TrainingWork& work
    ) const;
    static TransformerAutoencoder load_checkpoint(
        const std::string& path,
        CheckpointMetadata& metadata
    );

    [[nodiscard]] std::vector<std::string> parameter_names() const;
    [[nodiscard]] double parameter_value(
        const std::string& name,
        std::size_t index
    ) const;
    [[nodiscard]] std::size_t parameter_size(
        const std::string& name
    ) const;
    void set_parameter_value(
        const std::string& name,
        std::size_t index,
        double value
    );
    [[nodiscard]] double parameter_gradient(
        const std::string& name,
        std::size_t index
    ) const;
    [[nodiscard]] std::uint64_t parameter_update_step(
        const std::string& name
    ) const;
    BatchLoss gradient_only(
        const std::vector<std::vector<int>>& layouts,
        const std::vector<double>& relative_fitness,
        const LossWeights& weights,
        bool freeze_decoder
    );

    [[nodiscard]] bool causal_mask_fixture() const;
    [[nodiscard]] bool shape_fixture() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

TrainingProfile paper_scale_training_profile();
TrainingProfile bounded_smoke_training_profile();
std::vector<std::vector<int>> deterministic_layout_corpus(
    std::uint64_t count,
    const ModelConfig& config,
    std::uint64_t seed
);
TrainingWork pretrain(
    TransformerAutoencoder& model,
    const TrainingProfile& profile,
    fode::PersistentExecutor& executor
);
bool run_model_gradient_checks(std::string& report);

}  // namespace taae
