/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T87 command-line driver and machine-readable receipts
Paper/DOI/source/missing/conflict/reconstruction/semantic IDs/backend/claim:
hpc/core99_cpp/include/core99/hu_t87.hpp
Controlling contract: shared/contracts/core99_t87_hu_iga_pso_2024.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/hu_t87.hpp"

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
    std::string case_id = "t87_case1_jensen_aep";
    std::string data;
    std::string output;
    std::string indices;
    std::string coordinates_d;
    int workers = 20;
    int iga_population = 300;
    int iga_generations = 1000;
    int pso_population = 100;
    int pso_iterations = 200;
    std::uint64_t seed = 20260731;
};

Arguments parse(const int argc, char** argv) {
    Arguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument("missing value for " + flag);
            }
            return std::string(argv[index]);
        };
        if (flag == "--mode") arguments.mode = value();
        else if (flag == "--case") arguments.case_id = value();
        else if (flag == "--data") arguments.data = value();
        else if (flag == "--output") arguments.output = value();
        else if (flag == "--indices") arguments.indices = value();
        else if (flag == "--coordinates-d") {
            arguments.coordinates_d = value();
        } else if (flag == "--workers") {
            arguments.workers = std::stoi(value());
        } else if (flag == "--iga-population") {
            arguments.iga_population = std::stoi(value());
        } else if (flag == "--iga-generations") {
            arguments.iga_generations = std::stoi(value());
        } else if (flag == "--pso-population") {
            arguments.pso_population = std::stoi(value());
        } else if (flag == "--pso-iterations") {
            arguments.pso_iterations = std::stoi(value());
        } else if (flag == "--seed") {
            arguments.seed = std::stoull(value());
        } else {
            throw std::invalid_argument("unknown T87 flag: " + flag);
        }
    }
    if (arguments.data.empty()) {
        throw std::invalid_argument("--data is required");
    }
    return arguments;
}

template <class T>
std::vector<T> parse_csv(const std::string& text) {
    std::vector<T> values;
    std::istringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if constexpr (std::is_same_v<T, int>) {
            values.push_back(std::stoi(token));
        } else {
            values.push_back(std::stod(token));
        }
    }
    return values;
}

std::string evaluation_json(const core99::t87::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"fitness\":" << value.fitness
        << ",\"aep_mwh\":" << value.aep_mwh
        << ",\"nav_rmb_per_year\":" << value.nav_rmb_per_year
        << ",\"wake_efficiency\":" << value.wake_efficiency
        << ",\"total_normalized_constraint_violation\":"
        << value.total_normalized_constraint_violation
        << ",\"turbine_count\":" << value.turbine_count
        << ",\"feasible\":" << (value.feasible ? "true" : "false")
        << '}';
    return output.str();
}

template <class T>
std::string vector_json(const std::vector<T>& values) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) output << ',';
        output << values[index];
    }
    output << ']';
    return output.str();
}

void emit(const std::string& content, const std::string& path) {
    if (path.empty()) {
        std::cout << content;
        return;
    }
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("cannot open T87 output");
    stream << content;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        const core99::t87::Problem problem(
            arguments.case_id, arguments.data, arguments.workers
        );
        if (arguments.mode == "inspect") {
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"mode\":\"inspect\",\"case_id\":\""
                << problem.case_id() << "\",\"problem_semantic_id\":\""
                << problem.semantic_id() << "\",\"wake_model\":\""
                << core99::t87::wake_model_name(problem.wake_model())
                << "\",\"objective_model\":\""
                << core99::t87::objective_model_name(
                    problem.objective_model()
                ) << "\",\"candidate_count\":"
                << problem.candidates().size()
                << ",\"wind_state_count\":" << problem.wind_states().size()
                << ",\"turbine_curve_point_count\":"
                << problem.turbine_curve_point_count()
                << ",\"wind_probability_sum\":"
                << problem.wind_probability_sum()
                << ",\"precomputation_seconds\":"
                << problem.precomputation_seconds() << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "evaluate-grid") {
            if (arguments.indices.empty()) {
                throw std::invalid_argument(
                    "--indices is required in evaluate-grid mode"
                );
            }
            const auto evaluation = problem.evaluate_candidate_indices(
                parse_csv<int>(arguments.indices)
            );
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"mode\":\"evaluate_grid\",\"case_id\":\""
                << problem.case_id() << "\",\"problem_semantic_id\":\""
                << problem.semantic_id() << "\",\"evaluation\":"
                << evaluation_json(evaluation) << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "evaluate-continuous") {
            if (arguments.coordinates_d.empty()) {
                throw std::invalid_argument(
                    "--coordinates-d is required in evaluate-continuous mode"
                );
            }
            const auto evaluation = problem.evaluate_coordinates_d(
                parse_csv<double>(arguments.coordinates_d)
            );
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"mode\":\"evaluate_continuous\",\"case_id\":\""
                << problem.case_id() << "\",\"problem_semantic_id\":\""
                << problem.semantic_id() << "\",\"evaluation\":"
                << evaluation_json(evaluation) << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument("unknown T87 mode");
        }
        core99::t87::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.iga_population = arguments.iga_population;
        config.iga_generations = arguments.iga_generations;
        config.pso_population = arguments.pso_population;
        config.pso_iterations = arguments.pso_iterations;
        const auto result = core99::t87::run(problem, config);
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
            << "  \"iga_population\":" << result.iga_population << ",\n"
            << "  \"iga_generations\":" << result.iga_generations << ",\n"
            << "  \"pso_population\":" << result.pso_population << ",\n"
            << "  \"pso_iterations\":" << result.pso_iterations << ",\n"
            << "  \"proposed_fes\":" << result.proposed_fes << ",\n"
            << "  \"physical_unique_fes\":"
            << result.physical_unique_fes << ",\n"
            << "  \"best_grid_evaluation\":"
            << evaluation_json(result.best_grid_evaluation) << ",\n"
            << "  \"best_continuous_evaluation\":"
            << evaluation_json(result.best_continuous_evaluation) << ",\n"
            << "  \"best_grid_candidate_indices\":"
            << vector_json(result.best_grid_candidate_indices) << ",\n"
            << "  \"best_continuous_coordinates_d\":"
            << vector_json(result.best_continuous_coordinates_d) << ",\n"
            << "  \"best_fitness_history\":"
            << vector_json(result.best_fitness_history) << ",\n"
            << "  \"precomputation_seconds\":"
            << result.precomputation_seconds << ",\n"
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
        std::cerr << "T87 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
