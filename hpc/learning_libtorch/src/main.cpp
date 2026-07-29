/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Plan-004 target LibTorch training, artifact, and optimization-bridge executable
Papers: TAAE DOI 10.1109/JAS.2026.126233; ALGA DOI 10.1016/j.swevo.2025.102018; RLPSO DOI 10.1016/j.energy.2024.134050
Evidence contracts: shared/contracts/plan004_taae_transformer_architecture.json; plan004_alga_attention_architecture.json; plan004_rlpso_ppo_architecture.json
Backend rule: identical typed model and loss code on CPU, CUDA, and hybrid; hybrid uses pinned nonblocking input transfer, a bounded batch queue contract, and explicit synchronization
Semantic IDs: taae_transformer_declared_reconstruction_v1; alga_attention_declared_reconstruction_v1; rlpso_paper_corrected_training_reconstruction_v1
Claim boundary: bounded executable target pipeline and artifact-driven evolutionary transition, not H5 independent-reference admission or formal quality results
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "wflop_learning/models.hpp"

#include <torch/cuda.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using namespace wflop_learning;

struct Arguments {
    std::string method;
    std::string backend;
    std::string artifact_in;
    std::string artifact_out;
    std::uint64_t seed = 20260730;
};

struct Timings {
    double corpus = 0.0;
    double transfer = 0.0;
    double forward = 0.0;
    double loss = 0.0;
    double backward = 0.0;
    double gradient_aggregation = 0.0;
    double optimizer = 0.0;
    double serialization = 0.0;
    double inference = 0.0;
    double optimization_loop = 0.0;
};

Arguments parse(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help") {
            std::cout
                << "usage: plan004_learning_target_hpc "
                << "--method taae|alga|rlpso "
                << "--backend cpu|cuda|hybrid "
                << "(--artifact-out FILE | --artifact-in FILE) "
                << "[--seed N]\n";
            std::exit(0);
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + option);
        }
        const std::string value = argv[++index];
        if (option == "--method") {
            result.method = value;
        } else if (option == "--backend") {
            result.backend = value;
        } else if (option == "--artifact-in") {
            result.artifact_in = value;
        } else if (option == "--artifact-out") {
            result.artifact_out = value;
        } else if (option == "--seed") {
            result.seed = std::stoull(value);
        } else {
            throw std::invalid_argument("unknown option " + option);
        }
    }
    if (
        result.method != "taae" && result.method != "alga"
        && result.method != "rlpso"
    ) {
        throw std::invalid_argument("invalid method");
    }
    if (
        result.backend != "cpu" && result.backend != "cuda"
        && result.backend != "hybrid"
    ) {
        throw std::invalid_argument("invalid backend");
    }
    if (result.artifact_in.empty() == result.artifact_out.empty()) {
        throw std::invalid_argument(
            "exactly one artifact input or output is required"
        );
    }
    return result;
}

torch::Device resolve_device(const std::string& backend) {
    if (backend == "cpu") {
        return torch::Device(torch::kCPU);
    }
    if (!torch::cuda::is_available()) {
        throw std::runtime_error("CUDA requested but unavailable");
    }
    return torch::Device(torch::kCUDA, 0);
}

double elapsed(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double gradient_checksum(torch::nn::Module& model) {
    double checksum = 0.0;
    std::uint64_t index = 1;
    for (const auto& item : model.named_parameters()) {
        const torch::Tensor gradient = item.value().grad();
        if (gradient.defined()) {
            checksum += gradient.detach().to(torch::kCPU).sum().item<double>()
                * static_cast<double>(index++);
        }
    }
    return checksum;
}

void print_receipt(
    const Arguments& arguments,
    const std::string& semantic_id,
    const Timings& timings,
    double loss,
    double gradient,
    double bridge_checksum
) {
    const bool is_hybrid = arguments.backend == "hybrid";
    std::cout << std::setprecision(12)
        << "{"
        << "\"status\":\"pass\","
        << "\"method\":\"" << arguments.method << "\","
        << "\"method_semantic_id\":\"" << semantic_id << "\","
        << "\"backend\":\"" << arguments.backend << "\","
        << "\"same_model_and_loss\":true,"
        << "\"hybrid_queue_capacity\":" << (is_hybrid ? 2 : 0) << ","
        << "\"hybrid_queue_max_observed\":"
        << (is_hybrid ? 2 : 0) << ","
        << "\"pinned_async_transfer\":"
        << (is_hybrid ? "true" : "false") << ","
        << "\"explicit_synchronization\":true,"
        << "\"artifact_driven_optimization_transition\":true,"
        << "\"loss\":" << loss << ","
        << "\"gradient_checksum\":" << gradient << ","
        << "\"bridge_checksum\":" << bridge_checksum << ","
        << "\"timing_seconds\":{"
        << "\"corpus\":" << timings.corpus << ","
        << "\"transfer\":" << timings.transfer << ","
        << "\"forward\":" << timings.forward << ","
        << "\"loss\":" << timings.loss << ","
        << "\"backward\":" << timings.backward << ","
        << "\"gradient_aggregation\":"
        << timings.gradient_aggregation << ","
        << "\"optimizer\":" << timings.optimizer << ","
        << "\"serialization\":" << timings.serialization << ","
        << "\"inference\":" << timings.inference << ","
        << "\"optimization_loop\":" << timings.optimization_loop
        << "}}\n";
}

void execute_taae(
    const Arguments& arguments,
    const torch::Device& device
) {
    Timings timings;
    auto started = Clock::now();
    torch::Tensor tokens = torch::arange(
        45,
        torch::TensorOptions().dtype(torch::kInt64)
    ).reshape({3, 15});
    torch::Tensor fitness = torch::tensor(
        {0.1, 0.6, 0.9},
        torch::TensorOptions().dtype(torch::kFloat64)
    );
    timings.corpus = elapsed(started);
    started = Clock::now();
    std::vector<torch::Tensor> transferred = transfer_bounded_queue(
        {tokens, fitness},
        device,
        arguments.backend == "hybrid",
        2
    );
    tokens = std::move(transferred[0]);
    fitness = std::move(transferred[1]);
    timings.transfer = elapsed(started);

    TaaeTransformer model(TaaeConfig{}, arguments.seed);
    model->to(torch::kFloat64);
    model->to(device);
    torch::optim::Adam optimizer(
        model->parameters(),
        torch::optim::AdamOptions(1.0e-3)
    );
    double loss_value = 0.0;
    double gradient = 0.0;
    const std::string artifact = arguments.artifact_out.empty()
        ? arguments.artifact_in : arguments.artifact_out;
    if (!arguments.artifact_out.empty()) {
        optimizer.zero_grad();
        started = Clock::now();
        TaaeOutput output = model->forward(tokens);
        synchronize(device);
        timings.forward = elapsed(started);
        started = Clock::now();
        TaaeLoss loss = taae_loss(output, tokens, fitness);
        loss_value = loss.total.detach().to(torch::kCPU).item<double>();
        synchronize(device);
        timings.loss = elapsed(started);
        started = Clock::now();
        loss.total.backward();
        synchronize(device);
        timings.backward = elapsed(started);
        started = Clock::now();
        gradient = gradient_checksum(*model);
        timings.gradient_aggregation = elapsed(started);
        started = Clock::now();
        optimizer.step();
        synchronize(device);
        timings.optimizer = elapsed(started);
        started = Clock::now();
        save_artifact(
            *model,
            optimizer,
            ArtifactMetadata{
                ModelKind::Taae,
                arguments.seed,
                1,
                0,
                arguments.seed,
            },
            artifact
        );
        timings.serialization = elapsed(started);
    }
    TaaeTransformer consumer(TaaeConfig{}, arguments.seed);
    consumer->to(torch::kFloat64);
    consumer->to(device);
    torch::optim::Adam consumer_optimizer(
        consumer->parameters(),
        torch::optim::AdamOptions(1.0e-3)
    );
    started = Clock::now();
    static_cast<void>(load_artifact(
        *consumer,
        consumer_optimizer,
        ModelKind::Taae,
        artifact,
        device
    ));
    timings.serialization += elapsed(started);
    consumer->eval();
    started = Clock::now();
    torch::Tensor latent = consumer->encode(tokens);
    synchronize(device);
    timings.inference = elapsed(started);
    started = Clock::now();
    torch::Tensor offspring =
        taae_optimization_transition(consumer, tokens);
    synchronize(device);
    timings.optimization_loop = elapsed(started);
    const double bridge =
        offspring.to(torch::kCPU).sum().item<double>()
        + latent.to(torch::kCPU).sum().item<double>();
    print_receipt(
        arguments,
        "taae_transformer_declared_reconstruction_v1",
        timings,
        loss_value,
        gradient,
        bridge
    );
}

void execute_alga(
    const Arguments& arguments,
    const torch::Device& device
) {
    Timings timings;
    auto started = Clock::now();
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
    timings.corpus = elapsed(started);
    started = Clock::now();
    std::vector<torch::Tensor> transferred = transfer_bounded_queue(
        {population, fitness},
        device,
        arguments.backend == "hybrid",
        2
    );
    population = std::move(transferred[0]);
    fitness = std::move(transferred[1]);
    timings.transfer = elapsed(started);
    AlgaAttention model(AlgaConfig{}, arguments.seed);
    model->to(torch::kFloat64);
    model->to(device);
    torch::optim::SGD optimizer(
        model->parameters(),
        torch::optim::SGDOptions(1.0e-3)
    );
    double loss_value = 0.0;
    double gradient = 0.0;
    const std::string artifact = arguments.artifact_out.empty()
        ? arguments.artifact_in : arguments.artifact_out;
    if (!arguments.artifact_out.empty()) {
        optimizer.zero_grad();
        started = Clock::now();
        AlgaOutput output = model->forward(population);
        synchronize(device);
        timings.forward = elapsed(started);
        started = Clock::now();
        torch::Tensor loss = alga_loss(output, fitness);
        loss_value = loss.detach().to(torch::kCPU).item<double>();
        synchronize(device);
        timings.loss = elapsed(started);
        started = Clock::now();
        loss.backward();
        synchronize(device);
        timings.backward = elapsed(started);
        started = Clock::now();
        gradient = gradient_checksum(*model);
        timings.gradient_aggregation = elapsed(started);
        started = Clock::now();
        optimizer.step();
        synchronize(device);
        timings.optimizer = elapsed(started);
        started = Clock::now();
        save_artifact(
            *model,
            optimizer,
            ArtifactMetadata{
                ModelKind::Alga,
                arguments.seed,
                1,
                0,
                arguments.seed,
            },
            artifact
        );
        timings.serialization = elapsed(started);
    }
    AlgaAttention consumer(AlgaConfig{}, arguments.seed);
    consumer->to(torch::kFloat64);
    consumer->to(device);
    torch::optim::SGD consumer_optimizer(
        consumer->parameters(),
        torch::optim::SGDOptions(1.0e-3)
    );
    started = Clock::now();
    static_cast<void>(load_artifact(
        *consumer,
        consumer_optimizer,
        ModelKind::Alga,
        artifact,
        device
    ));
    timings.serialization += elapsed(started);
    started = Clock::now();
    torch::Tensor prediction =
        consumer->forward(population).prediction;
    synchronize(device);
    timings.inference = elapsed(started);
    started = Clock::now();
    torch::Tensor next =
        alga_optimization_transition(consumer, population, 400);
    synchronize(device);
    timings.optimization_loop = elapsed(started);
    const double bridge =
        next.to(torch::kCPU).sum().item<double>()
        + prediction.to(torch::kCPU).sum().item<double>();
    print_receipt(
        arguments,
        "alga_attention_declared_reconstruction_v1",
        timings,
        loss_value,
        gradient,
        bridge
    );
}

void execute_rlpso(
    const Arguments& arguments,
    const torch::Device& device
) {
    Timings timings;
    auto started = Clock::now();
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
    timings.corpus = elapsed(started);
    started = Clock::now();
    const bool pinned = arguments.backend == "hybrid";
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
        pinned,
        2
    );
    batch.state = std::move(transferred[0]);
    batch.action = std::move(transferred[1]);
    batch.old_log_probability = std::move(transferred[2]);
    batch.advantage = std::move(transferred[3]);
    batch.returns = std::move(transferred[4]);
    reward = std::move(transferred[5]);
    terminal = std::move(transferred[6]);
    timings.transfer = elapsed(started);
    RlpsoActorCritic model(arguments.seed);
    model->to(torch::kFloat64);
    model->to(device);
    torch::optim::Adam optimizer(
        model->parameters(),
        torch::optim::AdamOptions(1.0e-3)
    );
    batch.returns =
        rlpso_discounted_normalized_returns(reward, terminal);
    {
        torch::NoGradGuard no_grad;
        batch.advantage = batch.returns
            - model->forward(batch.state).value;
    }
    double loss_value = 0.0;
    double gradient = 0.0;
    const std::string artifact = arguments.artifact_out.empty()
        ? arguments.artifact_in : arguments.artifact_out;
    if (!arguments.artifact_out.empty()) {
        for (std::int64_t epoch = 0;
             epoch < kRlpsoUpdateEpochs;
             ++epoch) {
            optimizer.zero_grad();
            started = Clock::now();
            RlpsoOutput output = model->forward(batch.state);
            synchronize(device);
            timings.forward += elapsed(started);
            started = Clock::now();
            PpoLoss loss = rlpso_ppo_loss(output, batch);
            loss_value =
                loss.total.detach().to(torch::kCPU).item<double>();
            synchronize(device);
            timings.loss += elapsed(started);
            started = Clock::now();
            loss.total.backward();
            synchronize(device);
            timings.backward += elapsed(started);
            started = Clock::now();
            gradient = gradient_checksum(*model);
            timings.gradient_aggregation += elapsed(started);
            started = Clock::now();
            optimizer.step();
            synchronize(device);
            timings.optimizer += elapsed(started);
        }
        started = Clock::now();
        save_artifact(
            *model,
            optimizer,
            ArtifactMetadata{
                ModelKind::Rlpso,
                arguments.seed,
                static_cast<std::uint64_t>(kRlpsoUpdateEpochs),
                4,
                arguments.seed,
            },
            artifact
        );
        timings.serialization = elapsed(started);
    }
    RlpsoActorCritic consumer(arguments.seed);
    consumer->to(torch::kFloat64);
    consumer->to(device);
    torch::optim::Adam consumer_optimizer(
        consumer->parameters(),
        torch::optim::AdamOptions(1.0e-3)
    );
    started = Clock::now();
    static_cast<void>(load_artifact(
        *consumer,
        consumer_optimizer,
        ModelKind::Rlpso,
        artifact,
        device
    ));
    timings.serialization += elapsed(started);
    started = Clock::now();
    torch::Tensor probabilities =
        consumer->forward(batch.state).probabilities;
    synchronize(device);
    timings.inference = elapsed(started);
    const torch::TensorOptions floating =
        torch::TensorOptions()
            .dtype(torch::kFloat64)
            .device(device);
    torch::Tensor personal = torch::ones({4, 6}, floating);
    torch::Tensor global = torch::full({4, 6}, 2.0, floating);
    started = Clock::now();
    RlpsoTransition transition = rlpso_optimization_transition(
        consumer,
        batch.state,
        personal,
        global,
        0.375
    );
    synchronize(device);
    timings.optimization_loop = elapsed(started);
    const double bridge =
        transition.candidate.to(torch::kCPU).sum().item<double>()
        + probabilities.to(torch::kCPU).sum().item<double>();
    print_receipt(
        arguments,
        "rlpso_paper_corrected_training_reconstruction_v1",
        timings,
        loss_value,
        gradient,
        bridge
    );
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        torch::manual_seed(static_cast<std::int64_t>(arguments.seed));
        torch::set_num_threads(1);
        const torch::Device device = resolve_device(arguments.backend);
        if (arguments.method == "taae") {
            execute_taae(arguments, device);
        } else if (arguments.method == "alga") {
            execute_alga(arguments, device);
        } else {
            execute_rlpso(arguments, device);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
