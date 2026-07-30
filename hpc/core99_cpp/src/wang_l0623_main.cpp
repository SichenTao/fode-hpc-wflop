/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0623 CLI and machine-readable H5/H6/formal receipts
Paper: Wang et al., 10.1016/j.oceaneng.2023.116644.
Open source: arXiv:2309.01387v1 source figures under CC BY 4.0.
Missing fields, reconstruction and claim:
hpc/core99_cpp/include/core99/wang_l0623.hpp
Contract: shared/contracts/core99_l0623_wang_cfd_kriging_2024.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/wang_l0623.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Arguments {
    std::string mode = "optimize";
    std::string case_id = "l0623_case1_flat_single";
    std::string output;
    std::string indices;
    int workers = 20;
    int initial_samples = -1;
    int maximum_truth_calls = -1;
    int maximum_ga_generations = -1;
    std::uint64_t seed = 2026062300;
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
        else if (flag == "--output") result.output = value();
        else if (flag == "--indices") result.indices = value();
        else if (flag == "--workers") result.workers = std::stoi(value());
        else if (flag == "--initial-samples") {
            result.initial_samples = std::stoi(value());
        } else if (flag == "--maximum-truth-calls") {
            result.maximum_truth_calls = std::stoi(value());
        } else if (flag == "--maximum-ga-generations") {
            result.maximum_ga_generations = std::stoi(value());
        } else if (flag == "--seed") {
            result.seed = std::stoull(value());
        } else {
            throw std::invalid_argument("unknown L0623 flag: " + flag);
        }
    }
    return result;
}

std::string string_vector_json(const std::vector<std::string>& values) {
    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) output << ',';
        output << '"' << values[index] << '"';
    }
    output << ']';
    return output.str();
}

template <class T, std::size_t N>
std::string array_json(const std::array<T, N>& values) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) output << ',';
        output << values[index];
    }
    output << ']';
    return output.str();
}

std::string vector_json(const std::vector<double>& values) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) output << ',';
        output << values[index];
    }
    output << ']';
    return output.str();
}

core99::l0623::Layout parse_layout(const std::string& value) {
    if (value.empty()) return core99::l0623::paper_baseline_layout();
    core99::l0623::Layout result{};
    std::istringstream input(value);
    std::string token;
    std::size_t index = 0;
    while (std::getline(input, token, ',')) {
        if (index >= result.size()) {
            throw std::invalid_argument("too many L0623 indices");
        }
        result[index++] = std::stoi(token);
    }
    if (index != result.size()) {
        throw std::invalid_argument("L0623 requires eight indices");
    }
    return result;
}

std::string evaluation_json(const core99::l0623::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"aep_gwh\":" << value.aep_gwh
        << ",\"turbine_aep_gwh\":"
        << array_json(value.turbine_aep_gwh)
        << ",\"minimum_spacing_margin_m\":"
        << value.minimum_spacing_margin_m
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
    if (!stream) throw std::runtime_error("cannot open L0623 output");
    stream << content;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            emit(
                "{\"paper_case_ids\":"
                    + string_vector_json(core99::l0623::paper_case_ids())
                    + "}\n",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        const core99::l0623::Problem problem(arguments.case_id);
        if (arguments.mode == "inspect") {
            std::ostringstream output;
            output << "{\"mode\":\"inspect\",\"case_id\":\""
                << problem.case_id()
                << "\",\"problem_semantic_id\":\""
                << problem.semantic_id()
                << "\",\"wind_direction_count\":"
                << problem.wind_direction_count()
                << ",\"gaussian_hill\":"
                << (problem.has_gaussian_hill() ? "true" : "false")
                << ",\"candidate_count\":81,\"turbine_count\":8"
                << ",\"paper_initial_samples\":"
                << problem.paper_initial_samples()
                << ",\"paper_truth_calls\":" << problem.paper_truth_calls()
                << ",\"paper_population\":" << problem.paper_population()
                << ",\"paper_maximum_ga_generations\":"
                << problem.paper_maximum_ga_generations()
                << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "evaluate") {
            const auto layout = parse_layout(arguments.indices);
            const auto evaluation = problem.evaluate_truth(layout);
            std::ostringstream output;
            output << "{\"mode\":\"evaluation\",\"case_id\":\""
                << problem.case_id() << "\",\"indices\":"
                << array_json(layout) << ",\"evaluation\":"
                << evaluation_json(evaluation) << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument("unknown L0623 mode");
        }
        core99::l0623::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.initial_samples = arguments.initial_samples;
        config.maximum_truth_calls = arguments.maximum_truth_calls;
        config.maximum_ga_generations =
            arguments.maximum_ga_generations;
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
            << "  \"seed\":" << result.seed << ",\n"
            << "  \"requested_workers\":" << result.requested_workers << ",\n"
            << "  \"observed_workers\":" << result.observed_workers << ",\n"
            << "  \"initial_samples\":" << result.initial_samples << ",\n"
            << "  \"truth_calls\":" << result.truth_calls << ",\n"
            << "  \"surrogate_fes\":" << result.surrogate_fes << ",\n"
            << "  \"selected_theta\":" << result.selected_theta << ",\n"
            << "  \"initial_best\":"
            << evaluation_json(result.initial_best) << ",\n"
            << "  \"best_evaluation\":"
            << evaluation_json(result.best_evaluation) << ",\n"
            << "  \"best_layout\":" << array_json(result.best_layout) << ",\n"
            << "  \"truth_best_history_gwh\":"
            << vector_json(result.truth_best_history_gwh) << ",\n"
            << "  \"truth_evaluator_seconds\":"
            << result.truth_evaluator_seconds << ",\n"
            << "  \"surrogate_training_seconds\":"
            << result.surrogate_training_seconds << ",\n"
            << "  \"surrogate_inference_seconds\":"
            << result.surrogate_inference_seconds << ",\n"
            << "  \"algorithm_seconds\":" << result.algorithm_seconds << ",\n"
            << "  \"end_to_end_seconds\":"
            << result.end_to_end_seconds << ",\n"
            << "  \"scientific_hash\":\"" << std::hex
            << result.scientific_hash << std::dec << "\"\n"
            << "}\n";
        emit(output.str(), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "L0623 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
