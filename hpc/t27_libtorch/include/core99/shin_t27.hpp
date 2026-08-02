/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T27 paper-first C++/LibTorch diffusion WFLOP contract
Paper: Wind Farm Layout Optimization with Diffusion Models
Paper DOI: 10.1145/3711896.3737181
Public source: paper-linked https://github.com/dbsxodud-11/layopt
Audited source revision: 19ff38950ec6d23b6241875c1e5936878508a10d
Official simulator source: FLORIS 4.1.1, revision
2c3be8fd91fdb2ce519e2f3139444a7045f50473, BSD-3-Clause.
What the paper provides: the iterative dataset/diffusion search, fixed and
diverse wind cases, fully-connected three-layer 1024-wide GAT denoiser,
conditional EDM hyperparameters, 10 rounds, 5000 initial layouts, 1000
samples per round, 10000 training steps per round, and three independent runs.
What the public source provides: executable Python structure, GATConv/GELU
details, score normalization, 1000-step Adam spacing repair, EMA=0.995 every
10 steps, and the exact FLORIS GCH input.
Missing/conflicting facts: no requirements file despite README reference, no
datasets/checkpoints, no license, source defaults 1000 initial layouts,
5000 steps and lr=1e-4 rather than paper 5000/10000/5e-4, paper says
LeakyReLU while source applies GELU, and the source round-to-round dataset
and checkpoint lifecycle is not internally closed.
Reconstruction and resolution: independently implement the paper profile; use source only to
complete facts absent from the paper, retain a separately named source
activation profile, rebuild cumulative datasets and checkpoint transitions,
train from scratch, and never redistribute unlicensed source.
HPC design: dense complete-graph GAT, EDM training/sampling and constraint
repair execute as batched LibTorch C++ tensor programs on optimized CPU or
CUDA; the zero-yaw GCH evaluator is a native vectorized C++ reconstruction,
parallel over independent layouts and validated against pinned official
FLORIS 4.1.1 fixtures.
Semantic IDs: shin2025_conditional_edm_gat_paper_profile_v1;
shin2025_floris411_gch_rectangular_v1;
shin2025_iterative_dataset_protocol_repaired_v1.
Claim boundary: academic paper-first reconstruction, not author-original
software and not a claim of bitwise identity with author-private artifacts.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <torch/torch.h>

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t27 {

struct Layout {
    std::vector<double> x_unit;
    std::vector<double> y_unit;
};

struct WindScenario {
    double speed_mps = 8.0;
    double direction_degrees = 60.0;
};

struct Evaluation {
    double farm_power_w = 0.0;
    double annual_energy_mwh = 0.0;
    double minimum_spacing_m = 0.0;
    std::vector<double> turbine_power_w;
};

class Floris411Gch {
public:
    explicit Floris411Gch(int workers = 20);
    [[nodiscard]] Evaluation evaluate(
        const Layout& layout,
        double side_length_m,
        WindScenario wind
    ) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_batch(
        const std::vector<Layout>& layouts,
        double side_length_m,
        WindScenario wind
    ) const;
    [[nodiscard]] std::vector<Evaluation> evaluate_batch(
        const std::vector<Layout>& layouts,
        double side_length_m,
        const std::vector<WindScenario>& winds
    ) const;
    [[nodiscard]] int workers() const noexcept;

private:
    int workers_;
};

enum class ActivationProfile {
    paper_leaky_relu,
    source_gelu,
};

enum class ArchitectureProfile {
    gnn,
    mlp,
};

struct ModelConfig {
    int hidden_width = 1024;
    int layers = 3;
    int time_width = 128;
    int fourier_width = 16;
    int turbine_count = 30;
    ActivationProfile activation = ActivationProfile::paper_leaky_relu;
    ArchitectureProfile architecture = ArchitectureProfile::gnn;
};

struct DenseGatLayerImpl : torch::nn::Module {
    DenseGatLayerImpl(int input_width, int output_width);
    torch::Tensor forward(const torch::Tensor& input);
    torch::nn::Linear projection{nullptr};
    torch::nn::Linear residual{nullptr};
    torch::Tensor attention_source;
    torch::Tensor attention_target;
};
TORCH_MODULE(DenseGatLayer);

struct ConditionalGatDenoiserImpl : torch::nn::Module {
    explicit ConditionalGatDenoiserImpl(ModelConfig config = {});
    torch::Tensor forward(
        const torch::Tensor& layouts,
        const torch::Tensor& noise_time,
        const torch::Tensor& score_condition,
        const torch::Tensor& wind_condition
    );
    [[nodiscard]] const ModelConfig& config() const noexcept;

    ModelConfig config_;
    torch::nn::Linear layout_projection{nullptr};
    torch::nn::Linear score_projection{nullptr};
    torch::nn::Linear time_hidden{nullptr};
    torch::nn::Linear time_output{nullptr};
    torch::nn::Linear wind_speed_hidden{nullptr};
    torch::nn::Linear wind_speed_output{nullptr};
    torch::nn::Linear wind_direction_hidden{nullptr};
    torch::nn::Linear wind_direction_output{nullptr};
    torch::nn::ModuleList gat_layers{nullptr};
    torch::nn::Linear decoder_hidden{nullptr};
    torch::nn::Linear decoder_output{nullptr};
    torch::nn::Linear mlp_layout_projection{nullptr};
    torch::nn::Linear mlp_input{nullptr};
    torch::nn::ModuleList mlp_residual_layers{nullptr};
    torch::nn::Linear mlp_output{nullptr};
    torch::Tensor fourier_weights;
};
TORCH_MODULE(ConditionalGatDenoiser);

struct EdmConfig {
    int sample_steps = 128;
    double sigma_min = 0.002;
    double sigma_max = 80.0;
    double sigma_data = 1.0;
    double rho = 7.0;
    double p_mean = -1.2;
    double p_std = 1.2;
    double churn = 80.0;
    double churn_min = 0.05;
    double churn_max = 50.0;
    double sampling_noise = 1.003;
    double guidance = 2.0;
};

class ConditionalEdm {
public:
    ConditionalEdm(
        ConditionalGatDenoiser model,
        EdmConfig config = {}
    );
    [[nodiscard]] torch::Tensor training_loss(
        const torch::Tensor& layouts,
        const torch::Tensor& score_condition,
        const torch::Tensor& wind_condition,
        double condition_drop_probability
    );
    [[nodiscard]] torch::Tensor sample(
        int batch_size,
        int turbine_count,
        const torch::Tensor& score_condition,
        const torch::Tensor& wind_condition,
        int sample_steps = 0
    );
    [[nodiscard]] ConditionalGatDenoiser& model() noexcept;

private:
    [[nodiscard]] torch::Tensor denoise(
        const torch::Tensor& value,
        const torch::Tensor& sigma,
        const torch::Tensor& score_condition,
        const torch::Tensor& wind_condition
    );
    ConditionalGatDenoiser model_;
    EdmConfig config_;
};

struct ProtocolConfig {
    int turbine_count = 30;
    double side_length_m = 3000.0;
    int initial_layouts = 5000;
    int rounds = 10;
    int generated_per_round = 1000;
    int training_steps_per_round = 10000;
    int batch_size = 256;
    int repair_steps = 1000;
    double learning_rate = 5.0e-4;
    double repair_learning_rate = 1.0e-3;
    double condition_drop_probability = 0.1;
    double ema_decay = 0.995;
    int ema_update_every = 10;
    int workers = 20;
    std::uint64_t seed = 0;
    WindScenario wind{};
    bool diverse_wind_training = false;
    double training_wind_speed_min = 6.0;
    double training_wind_speed_max = 10.0;
    double training_wind_direction_min = 0.0;
    double training_wind_direction_max = 120.0;
    std::vector<int> transfer_turbine_counts;
};

struct TransferResult {
    int turbine_count = 0;
    double best_aep_mwh = 0.0;
    std::uint64_t physical_layout_evaluations = 0;
};

struct RunResult {
    std::string backend;
    int observed_cpu_threads = 0;
    int completed_rounds = 0;
    std::uint64_t optimizer_steps = 0;
    std::uint64_t physical_layout_evaluations = 0;
    double best_aep_mwh = 0.0;
    double initial_best_aep_mwh = 0.0;
    double data_generation_seconds = 0.0;
    double training_seconds = 0.0;
    double sampling_repair_seconds = 0.0;
    double evaluation_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    Layout best_layout;
    std::vector<TransferResult> transfer;
};

[[nodiscard]] torch::Tensor repair_spacing(
    torch::Tensor layouts,
    double side_length_m,
    double minimum_spacing_m,
    int steps,
    double learning_rate
);

[[nodiscard]] RunResult run(
    const ProtocolConfig& protocol,
    torch::Device device,
    ModelConfig model_config = {},
    EdmConfig edm_config = {}
);

[[nodiscard]] std::string activation_profile_name(
    ActivationProfile profile
);
[[nodiscard]] std::string architecture_profile_name(
    ArchitectureProfile profile
);

}  // namespace core99::t27
