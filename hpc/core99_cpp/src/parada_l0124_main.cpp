/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0124 command-line driver and machine-readable receipts
Paper/DOI/source/missing/conflict/reconstruction/semantic IDs/backend/claim:
hpc/core99_cpp/include/core99/parada_l0124.hpp
Controlling contract: shared/contracts/core99_l0124_parada_2017.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/parada_l0124.hpp"

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
    std::string problem = "l0124_case_a_grid10";
    std::string output;
    std::string coordinates;
    int workers = 20;
    int population = 600;
    int generations = 500;
    std::uint64_t seed = 20260731;
    double downstream_m = 200.0;
    double crosswind_m = 0.0;
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
        else if (flag == "--problem") arguments.problem = value();
        else if (flag == "--output") arguments.output = value();
        else if (flag == "--coordinates") arguments.coordinates = value();
        else if (flag == "--workers") arguments.workers = std::stoi(value());
        else if (flag == "--population") {
            arguments.population = std::stoi(value());
        } else if (flag == "--generations") {
            arguments.generations = std::stoi(value());
        } else if (flag == "--seed") {
            arguments.seed = std::stoull(value());
        } else if (flag == "--downstream-m") {
            arguments.downstream_m = std::stod(value());
        } else if (flag == "--crosswind-m") {
            arguments.crosswind_m = std::stod(value());
        } else {
            throw std::invalid_argument("unknown L0124 flag: " + flag);
        }
    }
    return arguments;
}

std::vector<int> parse_coordinates(const std::string& text) {
    std::vector<int> values;
    std::istringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        values.push_back(std::stoi(token));
    }
    return values;
}

std::string evaluation_json(const core99::l0124::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"objective\":" << value.objective
        << ",\"expected_power_kw\":" << value.expected_power_kw
        << ",\"efficiency\":" << value.efficiency
        << ",\"total_normalized_constraint_violation\":"
        << value.total_normalized_constraint_violation
        << ",\"feasible\":" << (value.feasible ? "true" : "false")
        << '}';
    return output.str();
}

std::string integers_json(const std::vector<int>& values) {
    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) output << ',';
        output << values[index];
    }
    output << ']';
    return output.str();
}

std::string doubles_json(const std::vector<double>& values) {
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
    if (!stream) throw std::runtime_error("cannot open L0124 output");
    stream << content;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "gaussian") {
            std::cout << std::setprecision(17)
                << "{\"mode\":\"gaussian\",\"deficit_ratio\":"
                << core99::l0124::gaussian_deficit_ratio(
                    arguments.downstream_m, arguments.crosswind_m
                ) << "}\n";
            return EXIT_SUCCESS;
        }
        const core99::l0124::Problem problem(arguments.problem);
        if (arguments.mode == "evaluate") {
            if (arguments.coordinates.empty()) {
                throw std::invalid_argument(
                    "--coordinates is required in evaluate mode"
                );
            }
            const auto evaluation = problem.evaluate(
                parse_coordinates(arguments.coordinates)
            );
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"mode\":\"evaluate\",\"problem_id\":\""
                << problem.id() << "\",\"problem_semantic_id\":\""
                << problem.semantic_id() << "\",\"no_wake_power_kw\":"
                << problem.no_wake_power_kw() << ",\"evaluation\":"
                << evaluation_json(evaluation) << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument("unknown L0124 mode");
        }
        core99::l0124::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.population = arguments.population;
        config.generations = arguments.generations;
        const auto result = core99::l0124::run(problem, config);
        std::ostringstream output;
        output << std::setprecision(17)
            << "{\n"
            << "  \"mode\":\"optimization\",\n"
            << "  \"problem_id\":\"" << result.problem_id << "\",\n"
            << "  \"problem_semantic_id\":\""
            << result.problem_semantic_id << "\",\n"
            << "  \"method_semantic_id\":\""
            << result.method_semantic_id << "\",\n"
            << "  \"seed\":" << result.seed << ",\n"
            << "  \"requested_workers\":" << result.requested_workers << ",\n"
            << "  \"observed_workers\":" << result.observed_workers << ",\n"
            << "  \"population\":" << result.population << ",\n"
            << "  \"generations\":" << result.generations << ",\n"
            << "  \"physical_fes\":" << result.physical_fes << ",\n"
            << "  \"best_evaluation\":"
            << evaluation_json(result.best_evaluation) << ",\n"
            << "  \"best_coordinates\":"
            << integers_json(result.best_coordinates) << ",\n"
            << "  \"best_objective_history\":"
            << doubles_json(result.best_objective_history) << ",\n"
            << "  \"evaluator_seconds\":" << result.evaluator_seconds << ",\n"
            << "  \"algorithm_seconds\":" << result.algorithm_seconds << ",\n"
            << "  \"end_to_end_seconds\":" << result.end_to_end_seconds << ",\n"
            << "  \"scientific_hash\":\"" << std::hex
            << result.scientific_hash << std::dec << "\"\n"
            << "}\n";
        emit(output.str(), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "L0124 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
