/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0590 CLI and machine-readable training/H5/H6/formal
receipts
Paper: Sun and Yang, 10.1016/j.apenergy.2023.121554.
Public source: no target code/data/weights; two direct model predecessor PDFs
were legally recovered as recorded in the contract.
Missing fields: private training assets, exact GA, calibration and cost curve.
Reconstruction: CLI exposes from-scratch training and every named paper case.
Semantic IDs: l0590_shiren_3d_ann_layout_height_v1;
l0590_real_ga_completed_v1; l0590_mlp_3_5_6_1_from_scratch_v1.
Contract: shared/contracts/core99_l0590_sun_ann_height_2023.json.
Claim boundary: academic declared reproduction, not author implementation or
numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/sun_l0590.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

struct Arguments {
    std::string mode = "optimize";
    std::string case_id = "l0590_e4";
    std::string weights;
    std::string output;
    int workers = 20;
    int generations = -1;
    int maximum_epochs = 1000;
    int sample_count = 32768;
    double target_mse = 1.0e-6;
    std::uint64_t seed = 2026059000;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument("missing value for " + flag);
            }
            return std::string(argv[index]);
        };
        if (flag == "--mode") result.mode = value();
        else if (flag == "--case") result.case_id = value();
        else if (flag == "--weights") result.weights = value();
        else if (flag == "--output") result.output = value();
        else if (flag == "--workers") result.workers = std::stoi(value());
        else if (flag == "--generations") {
            result.generations = std::stoi(value());
        } else if (flag == "--maximum-epochs") {
            result.maximum_epochs = std::stoi(value());
        } else if (flag == "--sample-count") {
            result.sample_count = std::stoi(value());
        } else if (flag == "--target-mse") {
            result.target_mse = std::stod(value());
        } else if (flag == "--seed") {
            result.seed = std::stoull(value());
        } else {
            throw std::invalid_argument("unknown L0590 flag: " + flag);
        }
    }
    return result;
}

template <class T>
std::string vector_json(const std::vector<T>& values) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) output << ',';
        if constexpr (std::is_same_v<T, std::string>) {
            output << '"' << values[index] << '"';
        } else {
            output << values[index];
        }
    }
    output << ']';
    return output.str();
}

std::string layout_json(const std::vector<core99::l0590::Turbine>& layout) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index != 0U) output << ',';
        output << "{\"x_m\":" << layout[index].x_m
            << ",\"y_m\":" << layout[index].y_m
            << ",\"hub_height_m\":" << layout[index].hub_height_m << '}';
    }
    output << ']';
    return output.str();
}

std::string evaluation_json(const core99::l0590::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"turbine_speed_mps\":"
        << vector_json(value.turbine_speed_mps)
        << ",\"turbine_power_kw\":"
        << vector_json(value.turbine_power_kw)
        << ",\"total_power_kw\":" << value.total_power_kw
        << ",\"total_cost_usd\":" << value.total_cost_usd
        << ",\"cost_of_power_usd_per_kw\":"
        << value.cost_of_power_usd_per_kw
        << ",\"objective\":" << value.objective
        << ",\"minimum_spacing_m\":" << value.minimum_spacing_m
        << ",\"feasible\":" << (value.feasible ? "true" : "false")
        << '}';
    return output.str();
}

void emit(const std::string& content, const std::string& path) {
    if (path.empty()) {
        std::cout << content;
        return;
    }
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("cannot open L0590 output");
    stream << content;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            emit(
                "{\"paper_case_ids\":"
                    + vector_json(core99::l0590::paper_case_ids())
                    + "}\n",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        core99::l0590::WakeSurrogate surrogate;
        if (arguments.mode == "train") {
            if (arguments.weights.empty()) {
                throw std::invalid_argument(
                    "--weights output is required in train mode"
                );
            }
            core99::l0590::TrainingConfig config;
            config.seed = arguments.seed;
            config.workers = arguments.workers;
            config.maximum_epochs = arguments.maximum_epochs;
            config.sample_count = arguments.sample_count;
            config.target_mse = arguments.target_mse;
            const auto result = surrogate.train(config);
            surrogate.save(arguments.weights);
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\n"
                << "  \"mode\":\"training\",\n"
                << "  \"training_semantic_id\":"
                   "\"l0590_mlp_3_5_6_1_from_scratch_v1\",\n"
                << "  \"seed\":" << arguments.seed << ",\n"
                << "  \"epochs\":" << result.epochs << ",\n"
                << "  \"requested_workers\":"
                << result.requested_workers << ",\n"
                << "  \"observed_workers\":" << result.observed_workers << ",\n"
                << "  \"train_count\":" << result.train_count << ",\n"
                << "  \"validation_count\":"
                << result.validation_count << ",\n"
                << "  \"test_count\":" << result.test_count << ",\n"
                << "  \"train_mse\":" << result.train_mse << ",\n"
                << "  \"validation_mse\":"
                << result.validation_mse << ",\n"
                << "  \"test_mse\":" << result.test_mse << ",\n"
                << "  \"seconds\":" << result.seconds << ",\n"
                << "  \"scientific_hash\":\"" << std::hex
                << result.scientific_hash << std::dec << "\"\n"
                << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.weights.empty()) {
            throw std::invalid_argument(
                "--weights is required outside train/list-cases modes"
            );
        }
        surrogate.load(arguments.weights);
        const core99::l0590::Problem problem(
            arguments.case_id, surrogate
        );
        if (arguments.mode == "inspect") {
            std::ostringstream output;
            output << "{\"mode\":\"inspect\",\"case_id\":\""
                << problem.case_id()
                << "\",\"problem_semantic_id\":\""
                << problem.semantic_id()
                << "\",\"optimizes_layout\":"
                << (problem.optimizes_layout() ? "true" : "false")
                << ",\"optimizes_height\":"
                << (problem.optimizes_height() ? "true" : "false")
                << ",\"minimizes_cost\":"
                << (problem.minimizes_cost() ? "true" : "false")
                << ",\"paper_generation_limit\":"
                << problem.paper_generation_limit()
                << ",\"turbine_count\":30,\"rotor_diameter_m\":77,"
                   "\"minimum_spacing_m\":385,\"field_extent_m\":2000,"
                   "\"height_lower_m\":45,\"height_upper_m\":85}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "evaluate-aligned") {
            const auto layout = core99::l0590::aligned_layout();
            const auto evaluation = problem.evaluate(layout);
            emit(
                "{\"mode\":\"evaluate-aligned\",\"case_id\":\""
                    + problem.case_id() + "\",\"layout\":"
                    + layout_json(layout) + ",\"evaluation\":"
                    + evaluation_json(evaluation) + "}\n",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument("unknown L0590 mode");
        }
        core99::l0590::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.generations = arguments.generations;
        const auto result = problem.optimize(config);
        std::ostringstream output;
        output << std::setprecision(17)
            << "{\n"
            << "  \"mode\":\"optimization\",\n"
            << "  \"case_id\":\"" << result.case_id << "\",\n"
            << "  \"problem_semantic_id\":\""
            << result.problem_semantic_id << "\",\n"
            << "  \"method_semantic_id\":\""
            << result.method_semantic_id << "\",\n"
            << "  \"training_semantic_id\":\""
            << result.training_semantic_id << "\",\n"
            << "  \"seed\":" << result.seed << ",\n"
            << "  \"requested_workers\":" << result.requested_workers << ",\n"
            << "  \"observed_workers\":" << result.observed_workers << ",\n"
            << "  \"physical_fes\":" << result.physical_fes << ",\n"
            << "  \"generations\":" << result.generations << ",\n"
            << "  \"initial_best\":"
            << evaluation_json(result.initial_best) << ",\n"
            << "  \"best_evaluation\":"
            << evaluation_json(result.best_evaluation) << ",\n"
            << "  \"best_layout\":" << layout_json(result.best_layout) << ",\n"
            << "  \"best_objective_history\":"
            << vector_json(result.best_objective_history) << ",\n"
            << "  \"evaluator_seconds\":" << result.evaluator_seconds << ",\n"
            << "  \"algorithm_seconds\":" << result.algorithm_seconds << ",\n"
            << "  \"end_to_end_seconds\":"
            << result.end_to_end_seconds << ",\n"
            << "  \"scientific_hash\":\"" << std::hex
            << result.scientific_hash << std::dec << "\"\n"
            << "}\n";
        emit(output.str(), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "L0590 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
