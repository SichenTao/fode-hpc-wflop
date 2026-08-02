/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Plan-004 LibTorch model, artifact, backend, and target-transition tests
Papers: TAAE DOI 10.1109/JAS.2026.126233; ALGA DOI
10.1016/j.swevo.2025.102018; RLPSO DOI 10.1016/j.energy.2024.134050
Public source, missing information, paper/source conflicts, and reconstruction
decisions are reused unchanged from hpc/learning_libtorch/src/models.cpp.
Evidence contracts: shared/contracts/plan004_taae_transformer_architecture.json; plan004_alga_attention_architecture.json; plan004_rlpso_ppo_architecture.json
Production backend: direct LibTorch C++ CPU/CUDA/hybrid test path; no Python
production runner.
Semantic IDs: taae_transformer_declared_reconstruction_v1; alga_attention_declared_reconstruction_v1; rlpso_paper_corrected_training_reconstruction_v1
Claim boundary: bounded architecture and backend verification only; H5 independent-reference admission is a separate Plan-004 gate
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "wflop_learning/models.hpp"

#include <torch/cuda.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace wflop_learning;

struct Arguments {
    std::string method;
    std::string backend;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Arguments parse(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + option);
        }
        const std::string value = argv[++index];
        if (option == "--method") {
            result.method = value;
        } else if (option == "--backend") {
            result.backend = value;
        } else {
            throw std::invalid_argument("unknown option " + option);
        }
    }
    if (
        result.method.empty()
        || (
            result.method != "taae" && result.method != "alga"
            && result.method != "rlpso" && result.method != "all"
        )
    ) {
        throw std::invalid_argument("invalid method");
    }
    if (
        result.backend != "cpu" && result.backend != "cuda"
        && result.backend != "hybrid"
    ) {
        throw std::invalid_argument("invalid backend");
    }
    return result;
}

torch::Device device_for(const std::string& backend) {
    if (backend == "cpu") {
        return torch::Device(torch::kCPU);
    }
    if (!torch::cuda::is_available()) {
        throw std::runtime_error("CUDA backend requested but unavailable");
    }
    return torch::Device(torch::kCUDA, 0);
}

bool hybrid(const std::string& backend) {
    return backend == "hybrid";
}

void require_finite_loss(
    const torch::Tensor& loss,
    const std::string& method
) {
    require(
        std::isfinite(loss.detach().to(torch::kCPU).item<double>()),
        method + " nonfinite loss"
    );
}

std::size_t gradient_parameter_count(torch::nn::Module& model) {
    std::size_t count = 0;
    for (const auto& parameter : model.named_parameters()) {
        if (parameter.value().grad().defined()) {
            require(
                torch::isfinite(parameter.value().grad()).all().item<bool>(),
                "nonfinite gradient " + parameter.key()
            );
            ++count;
        }
    }
    return count;
}

std::filesystem::path artifact_path(
    const std::string& method,
    const std::string& backend
) {
    return std::filesystem::temp_directory_path()
        / ("plan004_" + method + "_" + backend + "_artifact.pt");
}

void test_taae(const std::string& backend) {
    const torch::Device device = device_for(backend);
    torch::manual_seed(4001);
    TaaeTransformer model(TaaeConfig{}, 4001);
    model->to(torch::kFloat64);
    model->to(device);
    require(model->encoder->size() == 6, "TAAE encoder depth");
    require(model->decoder->size() == 6, "TAAE decoder depth");
    torch::Tensor tokens = torch::arange(
        45,
        torch::TensorOptions().dtype(torch::kInt64)
    ).reshape({3, 15});
    torch::Tensor fitness = torch::tensor(
        {0.1, 0.6, 0.9},
        torch::TensorOptions().dtype(torch::kFloat64)
    );
    std::vector<torch::Tensor> transferred = transfer_bounded_queue(
        {tokens, fitness},
        device,
        hybrid(backend),
        2
    );
    tokens = std::move(transferred[0]);
    fitness = std::move(transferred[1]);
    torch::optim::Adam optimizer(
        model->parameters(),
        torch::optim::AdamOptions(1.0e-3)
            .betas(std::make_tuple(0.9, 0.999))
            .eps(1.0e-8)
    );
    optimizer.zero_grad();
    TaaeOutput output = model->forward(tokens);
    require(
        output.logits.sizes() == torch::IntArrayRef({3, 15, 400}),
        "TAAE logits shape"
    );
    require(
        output.latent.sizes() == torch::IntArrayRef({3, 64}),
        "TAAE latent shape"
    );
    TaaeLoss losses = taae_loss(output, tokens, fitness);
    require_finite_loss(losses.total, "TAAE");
    losses.total.backward();
    const std::size_t gradient_count = gradient_parameter_count(*model);
    require(gradient_count >= 100, "TAAE incomplete gradient graph");
    optimizer.step();
    model->eval();
    torch::Tensor canonical =
        model->forward(tokens).logits.detach().to(torch::kCPU);
    const std::filesystem::path path = artifact_path("taae", backend);
    save_artifact(
        *model,
        optimizer,
        ArtifactMetadata{ModelKind::Taae, 4001, 1, 0, 4001},
        path.string()
    );
    TaaeTransformer restored(TaaeConfig{}, 4001);
    restored->to(torch::kFloat64);
    restored->to(device);
    torch::optim::Adam restored_optimizer(
        restored->parameters(),
        torch::optim::AdamOptions(1.0e-3)
    );
    ArtifactMetadata metadata = load_artifact(
        *restored,
        restored_optimizer,
        ModelKind::Taae,
        path.string(),
        device
    );
    restored->eval();
    torch::Tensor replay =
        restored->forward(tokens).logits.detach().to(torch::kCPU);
    require(
        torch::allclose(canonical, replay, 1.0e-11, 1.0e-11),
        "TAAE artifact replay"
    );
    require(
        metadata.optimizer_step == 1 && metadata.seed == 4001,
        "TAAE artifact metadata"
    );
    torch::Tensor offspring =
        taae_optimization_transition(restored, tokens);
    require(
        offspring.sizes() == torch::IntArrayRef({3, 15}),
        "TAAE optimization bridge"
    );
    torch::Tensor offspring_cpu =
        offspring.to(torch::kCPU).contiguous();
    for (std::int64_t row = 0; row < offspring.size(0); ++row) {
        const auto* begin =
            offspring_cpu[row].const_data_ptr<std::int64_t>();
        const std::set<std::int64_t> unique(begin, begin + 15);
        require(
            unique.size() == 15,
            "TAAE bridge repair uniqueness"
        );
    }
    std::filesystem::remove(path);
    std::cout
        << "taae_transformer_libtorch_pass backend=" << backend
        << " encoder_layers=6 decoder_layers=6 heads=4"
        << " gradients=" << gradient_count
        << " artifact_replay=yes optimization_bridge=yes\n";
}

void test_alga(const std::string& backend) {
    const torch::Device device = device_for(backend);
    torch::manual_seed(4501);
    AlgaAttention model(AlgaConfig{}, 4501);
    model->to(torch::kFloat64);
    model->to(device);
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
    std::vector<torch::Tensor> transferred = transfer_bounded_queue(
        {population, fitness},
        device,
        hybrid(backend),
        2
    );
    population = std::move(transferred[0]);
    fitness = std::move(transferred[1]);
    torch::optim::SGD optimizer(
        model->parameters(),
        torch::optim::SGDOptions(1.0e-3)
    );
    optimizer.zero_grad();
    AlgaOutput output = model->forward(population);
    require(
        output.attention.sizes() == torch::IntArrayRef({8, 30, 30}),
        "ALGA attention shape"
    );
    torch::Tensor loss = alga_loss(output, fitness);
    require_finite_loss(loss, "ALGA");
    loss.backward();
    const std::size_t gradient_count = gradient_parameter_count(*model);
    require(gradient_count == 5, "ALGA named gradient graph");
    optimizer.step();
    torch::Tensor canonical =
        model->forward(population).prediction.detach().to(torch::kCPU);
    const std::filesystem::path path = artifact_path("alga", backend);
    save_artifact(
        *model,
        optimizer,
        ArtifactMetadata{ModelKind::Alga, 4501, 1, 0, 4501},
        path.string()
    );
    AlgaAttention restored(AlgaConfig{}, 4501);
    restored->to(torch::kFloat64);
    restored->to(device);
    torch::optim::SGD restored_optimizer(
        restored->parameters(),
        torch::optim::SGDOptions(1.0e-3)
    );
    static_cast<void>(load_artifact(
        *restored,
        restored_optimizer,
        ModelKind::Alga,
        path.string(),
        device
    ));
    torch::Tensor replay =
        restored->forward(population).prediction.detach().to(torch::kCPU);
    require(
        torch::allclose(canonical, replay, 1.0e-11, 1.0e-11),
        "ALGA artifact replay"
    );
    torch::Tensor next =
        alga_optimization_transition(restored, population, 400);
    require(
        next.sizes() == torch::IntArrayRef({30, 20}),
        "ALGA optimization bridge"
    );
    std::filesystem::remove(path);
    std::cout
        << "alga_attention_libtorch_pass backend=" << backend
        << " heads=8 attention=8x30x30 gradients=" << gradient_count
        << " artifact_replay=yes optimization_bridge=yes\n";
}

void test_rlpso(const std::string& backend) {
    const torch::Device device = device_for(backend);
    torch::manual_seed(4201);
    RlpsoActorCritic model(4201);
    model->to(torch::kFloat64);
    model->to(device);
    PpoBatch batch{
        torch::tensor(
            {{0.5, 0.5}, {0.51, 0.49}, {0.49, 0.51}, {0.52, 0.48}},
            torch::TensorOptions().dtype(torch::kFloat64)
        ),
        torch::tensor(
            {0, 1, 2, 3},
            torch::TensorOptions().dtype(torch::kInt64)
        ),
        torch::full(
            {4},
            -std::log(4.0),
            torch::TensorOptions().dtype(torch::kFloat64)
        ),
        torch::tensor(
            {1.0, -0.5, 0.25, -0.75},
            torch::TensorOptions().dtype(torch::kFloat64)
        ),
        torch::tensor(
            {0.8, 0.2, -0.1, 0.4},
            torch::TensorOptions().dtype(torch::kFloat64)
        ),
    };
    torch::Tensor reward = torch::tensor(
        {0.2, -0.1, 0.4, 0.3},
        torch::TensorOptions().dtype(torch::kFloat64)
    );
    torch::Tensor terminal = torch::tensor(
        {false, false, false, true},
        torch::TensorOptions().dtype(torch::kBool)
    );
    std::vector<torch::Tensor> transferred = transfer_bounded_queue(
        {
            batch.state,
            batch.action,
            batch.old_log_probability,
            batch.advantage,
            batch.returns,
            reward,
            terminal,
        },
        device,
        hybrid(backend),
        2
    );
    batch.state = std::move(transferred[0]);
    batch.action = std::move(transferred[1]);
    batch.old_log_probability = std::move(transferred[2]);
    batch.advantage = std::move(transferred[3]);
    batch.returns = std::move(transferred[4]);
    reward = std::move(transferred[5]);
    terminal = std::move(transferred[6]);
    batch.returns =
        rlpso_discounted_normalized_returns(reward, terminal);
    {
        torch::NoGradGuard no_grad;
        batch.advantage = batch.returns
            - model->forward(batch.state).value;
    }
    require(
        std::abs(
            batch.returns.mean().to(torch::kCPU).item<double>()
        ) < 1.0e-12,
        "RLPSO normalized discounted return mean"
    );
    torch::optim::Adam optimizer(
        model->parameters(),
        torch::optim::AdamOptions(1.0e-3)
            .betas(std::make_tuple(0.9, 0.999))
            .eps(1.0e-8)
    );
    optimizer.zero_grad();
    RlpsoOutput output = model->forward(batch.state);
    require(
        output.logits.sizes() == torch::IntArrayRef({4, 4}),
        "RLPSO actor shape"
    );
    require(
        output.value.sizes() == torch::IntArrayRef({4}),
        "RLPSO critic shape"
    );
    PpoLoss losses = rlpso_ppo_loss(output, batch);
    require_finite_loss(losses.total, "RLPSO");
    losses.total.backward();
    const std::size_t gradient_count = gradient_parameter_count(*model);
    require(gradient_count == 12, "RLPSO named gradient graph");
    optimizer.step();
    for (std::int64_t epoch = 1;
         epoch < kRlpsoUpdateEpochs;
         ++epoch) {
        optimizer.zero_grad();
        RlpsoOutput epoch_output = model->forward(batch.state);
        rlpso_ppo_loss(epoch_output, batch).total.backward();
        optimizer.step();
    }
    torch::Tensor canonical =
        model->forward(batch.state).logits.detach().to(torch::kCPU);
    const std::filesystem::path path = artifact_path("rlpso", backend);
    save_artifact(
        *model,
        optimizer,
        ArtifactMetadata{
            ModelKind::Rlpso,
            4201,
            static_cast<std::uint64_t>(kRlpsoUpdateEpochs),
            4,
            4201,
        },
        path.string()
    );
    RlpsoActorCritic restored(4201);
    restored->to(torch::kFloat64);
    restored->to(device);
    torch::optim::Adam restored_optimizer(
        restored->parameters(),
        torch::optim::AdamOptions(1.0e-3)
    );
    ArtifactMetadata metadata = load_artifact(
        *restored,
        restored_optimizer,
        ModelKind::Rlpso,
        path.string(),
        device
    );
    torch::Tensor replay =
        restored->forward(batch.state).logits.detach().to(torch::kCPU);
    require(
        torch::allclose(canonical, replay, 1.0e-11, 1.0e-11),
        "RLPSO artifact replay"
    );
    require(
        metadata.rollout_cursor == 4
        && metadata.optimizer_step
            == static_cast<std::uint64_t>(kRlpsoUpdateEpochs),
        "RLPSO training lifecycle metadata"
    );
    torch::Tensor personal = torch::ones(
        {4, 6},
        torch::TensorOptions()
            .dtype(torch::kFloat64)
            .device(device)
    );
    torch::Tensor global = torch::full(
        {4, 6},
        2.0,
        personal.options()
    );
    RlpsoTransition transition = rlpso_optimization_transition(
        restored,
        batch.state,
        personal,
        global,
        0.375
    );
    require(
        transition.action.sizes() == torch::IntArrayRef({4})
        && transition.next_weights.sizes() == torch::IntArrayRef({4, 2})
        && transition.candidate.sizes() == torch::IntArrayRef({4, 6}),
        "RLPSO optimization bridge"
    );
    torch::Tensor delta =
        (transition.next_weights - batch.state).abs().sum(1);
    require(
        torch::allclose(
            delta,
            torch::full_like(delta, 0.001),
            1.0e-12,
            1.0e-12
        ),
        "RLPSO exact 0.001 action semantics"
    );
    std::filesystem::remove(path);
    std::cout
        << "rlpso_ppo_libtorch_pass backend=" << backend
        << " actor=2-256-64-4 critic=2-256-64-1"
        << " gradients=" << gradient_count
        << " update_epochs=80 action_step=0.001"
        << " categorical_sampling=counter_keyed"
        << " artifact_replay=yes optimization_bridge=yes\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        torch::set_num_threads(1);
        if (arguments.method == "taae" || arguments.method == "all") {
            test_taae(arguments.backend);
        }
        if (arguments.method == "alga" || arguments.method == "all") {
            test_alga(arguments.backend);
        }
        if (arguments.method == "rlpso" || arguments.method == "all") {
            test_rlpso(arguments.backend);
        }
        synchronize(device_for(arguments.backend));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
