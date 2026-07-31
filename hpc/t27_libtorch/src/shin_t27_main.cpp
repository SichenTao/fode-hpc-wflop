/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T27 C++/LibTorch CPU/CUDA command-line driver
Paper DOI: 10.1145/3711896.3737181
Public source: https://github.com/dbsxodud-11/layopt at 19ff389.
Missing facts and reconstruction decisions: include/core99/shin_t27.hpp.
Semantic IDs: shin2025_conditional_edm_gat_paper_profile_v1 and
shin2025_iterative_dataset_protocol_repaired_v1.
Contract: shared/contracts/core99_t27_shin_diffusion_2025.json.
Claim boundary: academic paper-first reconstruction, not author software.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/shin_t27.hpp"

#include <ATen/Parallel.h>
#include <torch/cuda.h>

#include <fstream>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string mode = "optimize";
    std::string backend = "auto";
    std::string output;
    int turbines = 30;
    double side_length_m = 3000.0;
    int initial_layouts = 5000;
    int rounds = 10;
    int generated = 1000;
    int training_steps = 10000;
    int batch_size = 256;
    int repair_steps = 1000;
    int sample_steps = 128;
    int hidden_width = 1024;
    int workers = 20;
    std::uint64_t seed = 0;
    double wind_speed = 8.0;
    double wind_direction = 60.0;
    std::string activation = "paper";
    std::string model_type = "gnn";
    std::string transfer_counts;
    double guidance = 2.0;
    bool diverse_training = false;
    double wind_speed_min = 6.0;
    double wind_speed_max = 10.0;
    double wind_direction_min = 0.0;
    double wind_direction_max = 120.0;
};

Arguments parse(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help") {
            std::cout
                << "core99_t27_hpc --mode optimize|evaluator-fixture "
                << "--backend auto|cpu|cuda [paper protocol overrides]\n";
            std::exit(0);
        }
        if (++index >= argc) {
            throw std::invalid_argument("missing value for " + option);
        }
        const std::string value = argv[index];
        if (option == "--mode") result.mode = value;
        else if (option == "--backend") result.backend = value;
        else if (option == "--output") result.output = value;
        else if (option == "--turbines") result.turbines = std::stoi(value);
        else if (option == "--side-length-m") {
            result.side_length_m = std::stod(value);
        } else if (option == "--initial-layouts") {
            result.initial_layouts = std::stoi(value);
        } else if (option == "--rounds") {
            result.rounds = std::stoi(value);
        } else if (option == "--generated") {
            result.generated = std::stoi(value);
        } else if (option == "--training-steps") {
            result.training_steps = std::stoi(value);
        } else if (option == "--batch-size") {
            result.batch_size = std::stoi(value);
        } else if (option == "--repair-steps") {
            result.repair_steps = std::stoi(value);
        } else if (option == "--sample-steps") {
            result.sample_steps = std::stoi(value);
        } else if (option == "--hidden-width") {
            result.hidden_width = std::stoi(value);
        } else if (option == "--workers") {
            result.workers = std::stoi(value);
        } else if (option == "--seed") {
            result.seed = std::stoull(value);
        } else if (option == "--wind-speed") {
            result.wind_speed = std::stod(value);
        } else if (option == "--wind-direction") {
            result.wind_direction = std::stod(value);
        } else if (option == "--activation") {
            result.activation = value;
        } else if (option == "--model-type") {
            result.model_type = value;
        } else if (option == "--transfer-counts") {
            result.transfer_counts = value;
        } else if (option == "--guidance") {
            result.guidance = std::stod(value);
        } else if (option == "--diverse-training") {
            if (value != "true" && value != "false") {
                throw std::invalid_argument(
                    "--diverse-training requires true or false"
                );
            }
            result.diverse_training = value == "true";
        } else if (option == "--wind-speed-min") {
            result.wind_speed_min = std::stod(value);
        } else if (option == "--wind-speed-max") {
            result.wind_speed_max = std::stod(value);
        } else if (option == "--wind-direction-min") {
            result.wind_direction_min = std::stod(value);
        } else if (option == "--wind-direction-max") {
            result.wind_direction_max = std::stod(value);
        } else {
            throw std::invalid_argument("unknown T27 option " + option);
        }
    }
    return result;
}

torch::Device device(const std::string& backend) {
    if (backend == "cpu") return torch::Device(torch::kCPU);
    if (backend == "cuda") {
        if (!torch::cuda::is_available()) {
            throw std::runtime_error("CUDA requested but unavailable");
        }
        return torch::Device(torch::kCUDA);
    }
    if (backend == "auto") {
        return torch::cuda::is_available()
            ? torch::Device(torch::kCUDA)
            : torch::Device(torch::kCPU);
    }
    throw std::invalid_argument("invalid T27 backend");
}

void write_payload(
    const std::string& payload,
    const std::string& output
) {
    if (output.empty()) {
        std::cout << payload << "\n";
        return;
    }
    std::ofstream stream(output);
    if (!stream) throw std::runtime_error("cannot open T27 output");
    stream << payload << "\n";
}

std::string evaluator_fixture(int workers) {
    struct Case {
        const char* name;
        std::vector<double> x;
        std::vector<double> y;
        double speed;
        double direction;
    };
    const std::vector<Case> cases{
        {"single",{0.0},{0.0},8.0,60.0},
        {"aligned3",{0.0,630.0,1260.0},{0.0,0.0,0.0},8.0,270.0},
        {"cross5",{0.0,750.0,1500.0,750.0,750.0},
         {750.0,750.0,750.0,0.0,1500.0},8.0,60.0},
        {"stagger6",{0.0,600.0,1200.0,300.0,900.0,1500.0},
         {0.0,0.0,0.0,600.0,600.0,600.0},10.0,120.0},
    };
    core99::t27::Floris411Gch evaluator(workers);
    std::ostringstream out;
    out << "{\"schema_version\":1,\"corpus_id\":\"T27\","
        << "\"evaluator_semantic_id\":"
        << "\"shin2025_floris411_gch_rectangular_v1\","
        << "\"cases\":[";
    for (std::size_t index = 0; index < cases.size(); ++index) {
        if (index) out << ",";
        core99::t27::Layout layout;
        layout.x_unit = cases[index].x;
        layout.y_unit = cases[index].y;
        for (double& value : layout.x_unit) value /= 3000.0;
        for (double& value : layout.y_unit) value /= 3000.0;
        const auto result = evaluator.evaluate(
            layout,
            3000.0,
            {cases[index].speed, cases[index].direction}
        );
        out << std::setprecision(17)
            << "{\"name\":\"" << cases[index].name
            << "\",\"farm_power_w\":" << result.farm_power_w
            << ",\"aep_mwh\":" << result.annual_energy_mwh
            << ",\"turbine_power_w\":[";
        for (std::size_t turbine = 0;
             turbine < result.turbine_power_w.size();
             ++turbine) {
            if (turbine) out << ",";
            out << result.turbine_power_w[turbine];
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

std::string evaluator_throughput(const Arguments& arguments) {
    std::vector<core99::t27::Layout> layouts(
        static_cast<std::size_t>(arguments.initial_layouts)
    );
    for (int batch = 0; batch < arguments.initial_layouts; ++batch) {
        auto& layout = layouts[static_cast<std::size_t>(batch)];
        layout.x_unit.resize(static_cast<std::size_t>(arguments.turbines));
        layout.y_unit.resize(static_cast<std::size_t>(arguments.turbines));
        for (int turbine = 0; turbine < arguments.turbines; ++turbine) {
            layout.x_unit[static_cast<std::size_t>(turbine)] = std::fmod(
                (batch * 17.0 + turbine * 37.0 + 13.0)
                    * 0.6180339887498948,
                1.0
            );
            layout.y_unit[static_cast<std::size_t>(turbine)] = std::fmod(
                (batch * 29.0 + turbine * 19.0 + 7.0)
                    * 0.4142135623730950,
                1.0
            );
        }
    }
    core99::t27::Floris411Gch evaluator(arguments.workers);
    const auto begin = std::chrono::steady_clock::now();
    const auto values = evaluator.evaluate_batch(
        layouts,
        arguments.side_length_m,
        {arguments.wind_speed, arguments.wind_direction}
    );
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - begin
    ).count();
    double checksum = 0.0;
    for (const auto& value : values) checksum += value.annual_energy_mwh;
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"schema_version\":1,\"corpus_id\":\"T27\","
        << "\"mode\":\"evaluator-throughput\","
        << "\"layouts\":" << layouts.size()
        << ",\"turbines\":" << arguments.turbines
        << ",\"workers\":" << arguments.workers
        << ",\"seconds\":" << seconds
        << ",\"layout_evaluations_per_second\":"
        << layouts.size() / seconds
        << ",\"aep_checksum_mwh\":" << checksum << "}";
    return out.str();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "evaluator-fixture") {
            write_payload(
                evaluator_fixture(arguments.workers),
                arguments.output
            );
            return 0;
        }
        if (arguments.mode == "evaluator-throughput") {
            write_payload(
                evaluator_throughput(arguments),
                arguments.output
            );
            return 0;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument("unsupported T27 mode");
        }
        core99::t27::ProtocolConfig protocol;
        protocol.turbine_count = arguments.turbines;
        protocol.side_length_m = arguments.side_length_m;
        protocol.initial_layouts = arguments.initial_layouts;
        protocol.rounds = arguments.rounds;
        protocol.generated_per_round = arguments.generated;
        protocol.training_steps_per_round = arguments.training_steps;
        protocol.batch_size = arguments.batch_size;
        protocol.repair_steps = arguments.repair_steps;
        protocol.workers = arguments.workers;
        protocol.seed = arguments.seed;
        protocol.wind = {
            arguments.wind_speed,
            arguments.wind_direction,
        };
        protocol.diverse_wind_training = arguments.diverse_training;
        protocol.training_wind_speed_min = arguments.wind_speed_min;
        protocol.training_wind_speed_max = arguments.wind_speed_max;
        protocol.training_wind_direction_min =
            arguments.wind_direction_min;
        protocol.training_wind_direction_max =
            arguments.wind_direction_max;
        if (!arguments.transfer_counts.empty()) {
            std::size_t start = 0;
            while (start < arguments.transfer_counts.size()) {
                const auto end =
                    arguments.transfer_counts.find(',', start);
                protocol.transfer_turbine_counts.push_back(std::stoi(
                    arguments.transfer_counts.substr(start, end - start)
                ));
                if (end == std::string::npos) break;
                start = end + 1;
            }
        }
        core99::t27::ModelConfig model;
        model.hidden_width = arguments.hidden_width;
        model.turbine_count = arguments.turbines;
        if (arguments.activation == "source") {
            model.activation =
                core99::t27::ActivationProfile::source_gelu;
        } else if (arguments.activation != "paper") {
            throw std::invalid_argument("invalid activation profile");
        }
        if (arguments.model_type == "mlp") {
            model.architecture =
                core99::t27::ArchitectureProfile::mlp;
        } else if (arguments.model_type != "gnn") {
            throw std::invalid_argument("invalid T27 model type");
        }
        core99::t27::EdmConfig edm;
        edm.sample_steps = arguments.sample_steps;
        edm.guidance = arguments.guidance;
        const auto result = core99::t27::run(
            protocol,
            device(arguments.backend),
            model,
            edm
        );
        std::ostringstream out;
        out << std::setprecision(17)
            << "{\"schema_version\":1,\"corpus_id\":\"T27\","
            << "\"method_semantic_id\":\""
            << (
                model.architecture
                    == core99::t27::ArchitectureProfile::gnn
                    ? "shin2025_conditional_edm_gat_paper_profile_v1"
                    : "shin2025_conditional_edm_mlp_ablation_v1"
            )
            << "\","
            << "\"problem_semantic_id\":"
            << "\"shin2025_floris411_gch_rectangular_v1\","
            << "\"protocol_semantic_id\":"
            << "\"shin2025_iterative_dataset_protocol_repaired_v1\","
            << "\"backend\":\"" << result.backend
            << "\",\"activation_profile\":\""
            << core99::t27::activation_profile_name(model.activation)
            << "\",\"architecture_profile\":\""
            << core99::t27::architecture_profile_name(model.architecture)
            << "\",\"seed\":" << arguments.seed
            << ",\"turbines\":" << arguments.turbines
            << ",\"side_length_m\":" << arguments.side_length_m
            << ",\"diverse_wind_training\":"
            << (arguments.diverse_training ? "true" : "false")
            << ",\"observed_cpu_threads\":"
            << result.observed_cpu_threads
            << ",\"completed_rounds\":" << result.completed_rounds
            << ",\"optimizer_steps\":" << result.optimizer_steps
            << ",\"physical_layout_evaluations\":"
            << result.physical_layout_evaluations
            << ",\"initial_best_aep_mwh\":"
            << result.initial_best_aep_mwh
            << ",\"best_aep_mwh\":" << result.best_aep_mwh
            << ",\"transfer\":[";
        for (std::size_t index = 0;
             index < result.transfer.size();
             ++index) {
            if (index) out << ",";
            out << "{\"turbines\":"
                << result.transfer[index].turbine_count
                << ",\"best_aep_mwh\":"
                << result.transfer[index].best_aep_mwh
                << ",\"physical_layout_evaluations\":"
                << result.transfer[index].physical_layout_evaluations
                << "}";
        }
        out
            << "]"
            << ",\"timing_seconds\":{\"data_generation\":"
            << result.data_generation_seconds
            << ",\"training\":" << result.training_seconds
            << ",\"sampling_repair\":"
            << result.sampling_repair_seconds
            << ",\"evaluation\":" << result.evaluation_seconds
            << ",\"end_to_end\":" << result.end_to_end_seconds
            << "}}";
        write_payload(out.str(), arguments.output);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "core99_t27_hpc: " << error.what() << "\n";
        return 2;
    }
}
