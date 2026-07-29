/*
Plan-004 demoted LibTorch backend compatibility probe.

The three registered target models and their losses are shared unchanged by
CPU, CUDA, and hybrid execution.  The generated corpus is deterministic and
contains no physical objective calls.  This executable produces a replayable
training artifact.  Its generic MLPs are deliberately assigned non-target
semantic IDs and cannot satisfy target H5, H6, or formal quality gates.
*/

#include <torch/cuda.h>
#include <torch/torch.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Arguments {
    std::string method;
    std::string backend;
    std::string artifact_out;
    std::string artifact_in;
    std::uint64_t seed = 20260730;
    int samples = 32;
    int epochs = 2;
    int batch_size = 16;
    int workers = 1;
};

struct Corpus {
    torch::Tensor input;
    torch::Tensor target;
    torch::Tensor actions;
    torch::Tensor old_log_probability;
    torch::Tensor advantage;
    torch::Tensor returns;
};

struct ForwardResult {
    torch::Tensor logits;
    torch::Tensor value;
    torch::Tensor hidden;
};

struct TargetModelImpl : torch::nn::Module {
    explicit TargetModelImpl(const std::string& method_name)
        : method(method_name) {
        int input_width = 0;
        int hidden_width = 64;
        int latent_width = 32;
        int output_width = 0;
        if (method == "taae") {
            input_width = 16;
            output_width = 16;
        } else if (method == "alga") {
            input_width = 8;
            output_width = 4;
        } else if (method == "rlpso") {
            input_width = 2;
            hidden_width = 256;
            latent_width = 64;
            output_width = 4;
        } else {
            throw std::invalid_argument("unsupported method " + method);
        }
        encoder_1 = register_module(
            "encoder_1", torch::nn::Linear(input_width, hidden_width)
        );
        encoder_2 = register_module(
            "encoder_2", torch::nn::Linear(hidden_width, latent_width)
        );
        output = register_module(
            "output", torch::nn::Linear(latent_width, output_width)
        );
        value = register_module(
            "value", torch::nn::Linear(latent_width, 1)
        );
    }

    ForwardResult forward(const torch::Tensor& input_tensor) {
        auto hidden_1 = torch::tanh(encoder_1(input_tensor));
        auto hidden_2 = torch::tanh(encoder_2(hidden_1));
        return {
            output(hidden_2),
            value(hidden_2).squeeze(-1),
            hidden_2,
        };
    }

    std::string method;
    torch::nn::Linear encoder_1{nullptr};
    torch::nn::Linear encoder_2{nullptr};
    torch::nn::Linear output{nullptr};
    torch::nn::Linear value{nullptr};
};
TORCH_MODULE(TargetModel);

Arguments parse(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help") {
            std::cout
                << "usage: wflop_libtorch_training_hpc "
                << "--method taae|alga|rlpso "
                << "--backend cpu|gpu|hybrid "
                << "[--artifact-out FILE | --artifact-in FILE] "
                << "[--seed N] [--samples N] [--epochs N] "
                << "[--batch-size N] [--workers N]\n";
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
        } else if (option == "--artifact-out") {
            result.artifact_out = value;
        } else if (option == "--artifact-in") {
            result.artifact_in = value;
        } else if (option == "--seed") {
            result.seed = std::stoull(value);
        } else if (option == "--samples") {
            result.samples = std::stoi(value);
        } else if (option == "--epochs") {
            result.epochs = std::stoi(value);
        } else if (option == "--batch-size") {
            result.batch_size = std::stoi(value);
        } else if (option == "--workers") {
            result.workers = std::stoi(value);
        } else {
            throw std::invalid_argument("unknown option " + option);
        }
    }
    if (
        result.method.empty() || result.backend.empty()
        || (result.artifact_out.empty() == result.artifact_in.empty())
    ) {
        throw std::invalid_argument(
            "method, backend, and exactly one artifact path are required"
        );
    }
    if (
        result.samples <= 0 || result.epochs <= 0
        || result.batch_size <= 0 || result.workers <= 0
    ) {
        throw std::invalid_argument("work dimensions must be positive");
    }
    if (
        result.backend != "cpu" && result.backend != "gpu"
        && result.backend != "hybrid"
    ) {
        throw std::invalid_argument("unsupported backend " + result.backend);
    }
    return result;
}

std::string method_semantic_id(const std::string& method) {
    if (method == "taae") {
        return "backend_compatibility_only_v1_taae_shape_probe";
    }
    if (method == "alga") {
        return "backend_compatibility_only_v1_alga_shape_probe";
    }
    return "backend_compatibility_only_v1_rlpso_shape_probe";
}

std::string schema_id(const std::string& method) {
    if (method == "taae") {
        return "backend_compatibility_only_v1_occupancy16";
    }
    if (method == "alga") {
        return "backend_compatibility_only_v1_feature8";
    }
    return "backend_compatibility_only_v1_state2_action4";
}

std::string loss_id(const std::string& method) {
    if (method == "taae") {
        return "backend_compatibility_only_v1_bce_regression_metric";
    }
    if (method == "alga") {
        return "backend_compatibility_only_v1_cross_entropy_regression";
    }
    return "backend_compatibility_only_v1_ppo_shaped_loss";
}

Corpus deterministic_corpus(const std::string& method, int samples) {
    const int width = method == "taae" ? 16 : method == "alga" ? 8 : 2;
    std::vector<double> input(
        static_cast<std::size_t>(samples * width), 0.0
    );
    std::vector<double> target;
    if (method == "taae") {
        target.resize(input.size());
    } else {
        target.resize(static_cast<std::size_t>(samples));
    }
    std::vector<std::int64_t> actions(static_cast<std::size_t>(samples));
    std::vector<double> old_log_probability(
        static_cast<std::size_t>(samples), -std::log(4.0)
    );
    std::vector<double> advantage(static_cast<std::size_t>(samples));
    std::vector<double> returns(static_cast<std::size_t>(samples));

    for (int row = 0; row < samples; ++row) {
        double aggregate = 0.0;
        for (int column = 0; column < width; ++column) {
            double value = 0.0;
            if (method == "taae") {
                value = ((row * 5 + column * 3 + 1) % 7) < 3 ? 1.0 : 0.0;
                target[static_cast<std::size_t>(row * width + column)] =
                    value;
            } else {
                value = std::sin(
                    0.17 * static_cast<double>((row + 1) * (column + 2))
                );
            }
            input[static_cast<std::size_t>(row * width + column)] = value;
            aggregate += value;
        }
        if (method == "alga") {
            const std::int64_t selected =
                static_cast<std::int64_t>((row * 3 + 1) % 4);
            actions[static_cast<std::size_t>(row)] = selected;
            target[static_cast<std::size_t>(row)] =
                aggregate / static_cast<double>(width);
        } else if (method == "rlpso") {
            actions[static_cast<std::size_t>(row)] =
                static_cast<std::int64_t>((row * 3 + 2) % 4);
        }
        advantage[static_cast<std::size_t>(row)] =
            std::cos(0.13 * static_cast<double>(row + 1));
        returns[static_cast<std::size_t>(row)] =
            0.5 * aggregate + 0.25 * advantage[static_cast<std::size_t>(row)];
    }
    auto double_options = torch::TensorOptions().dtype(torch::kFloat64);
    auto long_options = torch::TensorOptions().dtype(torch::kInt64);
    Corpus result{
        torch::from_blob(input.data(), {samples, width}, double_options).clone(),
        method == "taae"
            ? torch::from_blob(
                  target.data(), {samples, width}, double_options
              ).clone()
            : torch::from_blob(
                  target.data(), {samples}, double_options
              ).clone(),
        torch::from_blob(actions.data(), {samples}, long_options).clone(),
        torch::from_blob(
            old_log_probability.data(), {samples}, double_options
        ).clone(),
        torch::from_blob(
            advantage.data(), {samples}, double_options
        ).clone(),
        torch::from_blob(returns.data(), {samples}, double_options).clone(),
    };
    return result;
}

Corpus to_device(
    Corpus corpus, const torch::Device& device, bool pinned
) {
    auto transfer = [&](torch::Tensor tensor) {
        if (pinned) {
            tensor = tensor.pin_memory();
        }
        return tensor.to(device, tensor.scalar_type(), pinned, true);
    };
    corpus.input = transfer(corpus.input);
    corpus.target = transfer(corpus.target);
    corpus.actions = transfer(corpus.actions);
    corpus.old_log_probability = transfer(corpus.old_log_probability);
    corpus.advantage = transfer(corpus.advantage);
    corpus.returns = transfer(corpus.returns);
    return corpus;
}

void synchronize(const torch::Device& device) {
    if (device.is_cuda()) {
        torch::cuda::synchronize(device.index());
    }
}

torch::Tensor loss(
    const std::string& method,
    const ForwardResult& forward,
    const Corpus& corpus
) {
    if (method == "taae") {
        auto reconstruction =
            torch::binary_cross_entropy_with_logits(
                forward.logits, corpus.target
            );
        auto occupancy = corpus.target.mean(1);
        auto regression =
            torch::mse_loss(forward.value, occupancy);
        auto hidden_delta =
            forward.hidden.slice(0, 1) - forward.hidden.slice(0, 0, -1);
        auto input_delta =
            corpus.input.slice(0, 1) - corpus.input.slice(0, 0, -1);
        auto metric = torch::mse_loss(
            hidden_delta.square().mean(1),
            input_delta.square().mean(1)
        );
        return reconstruction + 0.1 * regression + 0.01 * metric;
    }
    if (method == "alga") {
        auto selection =
            torch::nn::functional::cross_entropy(
                forward.logits, corpus.actions
            );
        auto regression =
            torch::mse_loss(forward.value, corpus.target);
        return selection + 0.25 * regression;
    }
    auto log_probability =
        torch::log_softmax(forward.logits, 1)
            .gather(1, corpus.actions.unsqueeze(1))
            .squeeze(1);
    auto ratio = torch::exp(
        log_probability - corpus.old_log_probability
    );
    auto unclipped = ratio * corpus.advantage;
    auto clipped =
        torch::clamp(ratio, 0.8, 1.2) * corpus.advantage;
    auto actor = -torch::minimum(unclipped, clipped).mean();
    auto critic = torch::mse_loss(forward.value, corpus.returns);
    auto probability = torch::softmax(forward.logits, 1);
    auto entropy =
        -(probability * torch::log_softmax(forward.logits, 1))
             .sum(1)
             .mean();
    return actor + 0.5 * critic - 0.01 * entropy;
}

std::uint64_t fnv1a64(const void* bytes, std::size_t size) {
    const auto* values = static_cast<const unsigned char*>(bytes);
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= static_cast<std::uint64_t>(values[index]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string tensor_hash(torch::Tensor tensor) {
    tensor = tensor.detach().to(torch::kCPU).contiguous();
    const std::size_t bytes =
        static_cast<std::size_t>(tensor.numel())
        * static_cast<std::size_t>(tensor.element_size());
    const std::uint64_t hash = fnv1a64(tensor.data_ptr(), bytes);
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0')
           << std::setw(16) << hash;
    return output.str();
}

std::string parameter_hash(const TargetModel& model) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& parameter : model->named_parameters(true)) {
        auto tensor =
            parameter.value().detach().to(torch::kCPU).contiguous();
        const auto* bytes =
            static_cast<const unsigned char*>(tensor.data_ptr());
        const std::size_t size =
            static_cast<std::size_t>(tensor.numel())
            * static_cast<std::size_t>(tensor.element_size());
        for (const char character : parameter.key()) {
            hash ^= static_cast<unsigned char>(character);
            hash *= 1099511628211ULL;
        }
        for (std::size_t index = 0; index < size; ++index) {
            hash ^= static_cast<std::uint64_t>(bytes[index]);
            hash *= 1099511628211ULL;
        }
    }
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0')
           << std::setw(16) << hash;
    return output.str();
}

std::uint64_t corpus_bytes(const Corpus& corpus) {
    std::uint64_t result = 0;
    for (const auto& tensor : {
             corpus.input,
             corpus.target,
             corpus.actions,
             corpus.old_log_probability,
             corpus.advantage,
             corpus.returns,
         }) {
        result += static_cast<std::uint64_t>(
            tensor.numel() * tensor.element_size()
        );
    }
    return result;
}

double seconds_since(const Clock::time_point& start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        torch::set_num_threads(arguments.workers);
        torch::manual_seed(static_cast<std::int64_t>(arguments.seed));

        const bool cuda_backend = arguments.backend != "cpu";
        if (cuda_backend && !torch::cuda::is_available()) {
            throw std::runtime_error(
                "CUDA backend requested but CUDA is unavailable"
            );
        }
        const torch::Device device =
            cuda_backend ? torch::Device(torch::kCUDA, 0)
                         : torch::Device(torch::kCPU);
        const auto total_started = Clock::now();
        const auto corpus_started = Clock::now();
        Corpus corpus =
            deterministic_corpus(arguments.method, arguments.samples);
        const double corpus_seconds = seconds_since(corpus_started);
        const std::uint64_t transfer_bytes =
            cuda_backend ? corpus_bytes(corpus) : 0;
        const auto transfer_started = Clock::now();
        corpus = to_device(
            std::move(corpus),
            device,
            arguments.backend == "hybrid"
        );
        synchronize(device);
        const double transfer_seconds = seconds_since(transfer_started);

        const auto model_setup_started = Clock::now();
        TargetModel model(arguments.method);
        model->to(torch::kFloat64);
        model->to(device);
        synchronize(device);
        const double model_setup_seconds =
            seconds_since(model_setup_started);
        double final_loss = 0.0;
        std::uint64_t optimizer_steps = 0;
        const auto training_started = Clock::now();
        if (!arguments.artifact_in.empty()) {
            torch::load(model, arguments.artifact_in);
            model->to(device);
        } else {
            torch::optim::Adam optimizer(
                model->parameters(),
                torch::optim::AdamOptions(1.0e-3)
            );
            model->train();
            for (int epoch = 0; epoch < arguments.epochs; ++epoch) {
                for (
                    int begin = 0;
                    begin < arguments.samples;
                    begin += arguments.batch_size
                ) {
                    const int end = std::min(
                        begin + arguments.batch_size, arguments.samples
                    );
                    Corpus batch{
                        corpus.input.slice(0, begin, end),
                        corpus.target.slice(0, begin, end),
                        corpus.actions.slice(0, begin, end),
                        corpus.old_log_probability.slice(0, begin, end),
                        corpus.advantage.slice(0, begin, end),
                        corpus.returns.slice(0, begin, end),
                    };
                    optimizer.zero_grad();
                    auto objective =
                        loss(arguments.method, model->forward(batch.input), batch);
                    objective.backward();
                    optimizer.step();
                    final_loss = objective.item<double>();
                    ++optimizer_steps;
                }
            }
            synchronize(device);
        }
        const double training_seconds = seconds_since(training_started);

        const auto artifact_started = Clock::now();
        if (!arguments.artifact_out.empty()) {
            torch::save(model, arguments.artifact_out);
        }
        synchronize(device);
        const double artifact_seconds = seconds_since(artifact_started);

        model->eval();
        torch::NoGradGuard no_grad;
        const auto inference_started = Clock::now();
        const int inference_rows = std::min(8, arguments.samples);
        const auto inference =
            model->forward(corpus.input.slice(0, 0, inference_rows)).logits;
        synchronize(device);
        const double inference_seconds = seconds_since(inference_started);
        const std::string inference_hash = tensor_hash(inference);
        const std::string model_hash = parameter_hash(model);
        const auto inference_cpu =
            inference.detach().to(torch::kCPU).contiguous();
        const double inference_sum = inference_cpu.sum().item<double>();
        const double inference_l2 =
            inference_cpu.square().sum().sqrt().item<double>();
        const double total_seconds = seconds_since(total_started);
        const double attributed = corpus_seconds + transfer_seconds
            + model_setup_seconds + training_seconds + artifact_seconds
            + inference_seconds;

        std::uintmax_t artifact_size = 0;
        const std::string artifact_path =
            arguments.artifact_out.empty()
                ? arguments.artifact_in
                : arguments.artifact_out;
        if (std::filesystem::exists(artifact_path)) {
            artifact_size = std::filesystem::file_size(artifact_path);
        }
        std::cout << std::setprecision(17)
            << "{"
            << "\"method\":\"" << arguments.method << "\","
            << "\"method_semantic_id\":\""
            << method_semantic_id(arguments.method) << "\","
            << "\"backend_id\":\""
            << method_semantic_id(arguments.method) << "__"
            << (arguments.backend == "cpu"
                    ? "cpu_hpc_v1"
                    : arguments.backend == "gpu"
                        ? "gpu_hpc_v1"
                        : "hybrid_cpu_gpu_hpc_v1")
            << "\","
            << "\"training_schema_id\":\""
            << schema_id(arguments.method) << "\","
            << "\"loss_contract_id\":\""
            << loss_id(arguments.method) << "\","
            << "\"requested_backend\":\"" << arguments.backend << "\","
            << "\"actual_training_device\":\""
            << (cuda_backend ? "cuda:0" : "cpu") << "\","
            << "\"corpus_generation_device\":"
            << "\"cpu_deterministic_contract_generator\","
            << "\"transfer_mode\":\""
            << (arguments.backend == "cpu"
                    ? "none"
                    : arguments.backend == "gpu"
                        ? "pageable_host_to_device"
                        : "pinned_host_to_device")
            << "\","
            << "\"artifact_mode\":\""
            << (arguments.artifact_in.empty() ? "train" : "replay")
            << "\","
            << "\"samples\":" << arguments.samples << ","
            << "\"epochs\":" << arguments.epochs << ","
            << "\"batch_size\":" << arguments.batch_size << ","
            << "\"workers\":" << arguments.workers << ","
            << "\"optimizer_steps\":" << optimizer_steps << ","
            << "\"training_physical_fes\":0,"
            << "\"final_loss\":" << final_loss << ","
            << "\"model_parameter_hash\":\"" << model_hash << "\","
            << "\"canonical_inference_hash\":\""
            << inference_hash << "\","
            << "\"canonical_inference_sum\":" << inference_sum << ","
            << "\"canonical_inference_l2\":" << inference_l2 << ","
            << "\"canonical_inference_values\":[";
        const auto* inference_values =
            inference_cpu.data_ptr<double>();
        for (std::int64_t index = 0;
             index < inference_cpu.numel();
             ++index) {
            if (index != 0) {
                std::cout << ",";
            }
            std::cout << inference_values[index];
        }
        std::cout
            << "],"
            << "\"artifact_bytes\":" << artifact_size << ","
            << "\"host_to_device_bytes\":" << transfer_bytes << ","
            << "\"stage_seconds\":{"
            << "\"corpus\":" << corpus_seconds << ","
            << "\"transfer\":" << transfer_seconds << ","
            << "\"model_setup\":" << model_setup_seconds << ","
            << "\"training\":" << training_seconds << ","
            << "\"artifact\":" << artifact_seconds << ","
            << "\"inference\":" << inference_seconds << "},"
            << "\"total_wall_seconds\":" << total_seconds << ","
            << "\"attributed_fraction\":"
            << (total_seconds > 0.0 ? attributed / total_seconds : 1.0)
            << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
