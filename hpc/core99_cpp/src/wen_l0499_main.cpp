/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0499 CLI and machine-readable H5/H6/formal receipts
Paper/DOI/source/missing/conflict/reconstruction/semantic IDs/backend/claim:
hpc/core99_cpp/include/core99/wen_l0499.hpp
Controlling contract:
shared/contracts/core99_l0499_wen_uncertain_cvar_2022.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/wen_l0499.hpp"

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
    std::string case_id = "l0499_case_a_so";
    std::string data;
    std::string output;
    std::string indices;
    int workers = 20;
    std::uint64_t max_physical_fes = 20032;
    std::uint64_t seed = 2026049900;
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
        else if (flag == "--workers") arguments.workers = std::stoi(value());
        else if (flag == "--max-physical-fes") {
            arguments.max_physical_fes = std::stoull(value());
        } else if (flag == "--seed") {
            arguments.seed = std::stoull(value());
        } else {
            throw std::invalid_argument("unknown L0499 flag: " + flag);
        }
    }
    if (arguments.data.empty()) {
        throw std::invalid_argument("--data is required");
    }
    return arguments;
}

std::vector<int> parse_indices(const std::string& text) {
    std::vector<int> result;
    std::istringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        result.push_back(std::stoi(token));
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

std::string evaluation_json(const core99::l0499::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"sector_power_kw\":" << vector_json(value.sector_power_kw)
        << ",\"expected_aep_mwh\":" << value.expected_aep_mwh
        << ",\"aep_standard_deviation_mwh\":"
        << value.aep_standard_deviation_mwh
        << ",\"cvar_mwh\":" << value.cvar_mwh
        << ",\"minimum_sector_power_kw\":"
        << value.minimum_sector_power_kw
        << ",\"objective\":" << value.objective
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
    if (!stream) throw std::runtime_error("cannot open L0499 output");
    stream << content;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            emit(
                "{\"paper_case_ids\":"
                    + vector_json(core99::l0499::paper_case_ids())
                    + "}\n",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        const core99::l0499::Problem problem(
            arguments.case_id, arguments.data, arguments.workers
        );
        if (arguments.mode == "inspect") {
            double probability_sum = 0.0;
            for (const double value : problem.wind_mean()) {
                probability_sum += value;
            }
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"mode\":\"inspect\",\"case_id\":\""
                << problem.case_id() << "\",\"problem_semantic_id\":\""
                << problem.semantic_id() << "\",\"objective_variant\":\""
                << problem.objective_variant() << "\",\"station_index\":"
                << problem.station_index()
                << ",\"candidate_count\":" << problem.candidates().size()
                << ",\"turbine_count\":" << problem.turbine_count()
                << ",\"sector_count\":" << problem.sector_count()
                << ",\"wind_mean_sum\":" << probability_sum
                << ",\"minimum_spacing_m\":"
                << problem.minimum_spacing_m()
                << ",\"precomputation_seconds\":"
                << problem.precomputation_seconds()
                << ",\"observed_precomputation_workers\":"
                << problem.observed_precomputation_workers()
                << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "evaluate") {
            if (arguments.indices.empty()) {
                throw std::invalid_argument(
                    "--indices is required in evaluate mode"
                );
            }
            const auto evaluation = problem.evaluate(
                parse_indices(arguments.indices)
            );
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"mode\":\"evaluate\",\"case_id\":\""
                << problem.case_id() << "\",\"problem_semantic_id\":\""
                << problem.semantic_id() << "\",\"evaluation\":"
                << evaluation_json(evaluation) << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument("unknown L0499 mode");
        }
        core99::l0499::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.max_physical_fes = arguments.max_physical_fes;
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
            << "  \"physical_fes\":" << result.physical_fes << ",\n"
            << "  \"generations\":" << result.generations << ",\n"
            << "  \"initial_best\":"
            << evaluation_json(result.initial_best) << ",\n"
            << "  \"best_evaluation\":"
            << evaluation_json(result.best_evaluation) << ",\n"
            << "  \"best_candidate_indices\":"
            << vector_json(result.best_candidate_indices) << ",\n"
            << "  \"best_objective_history\":"
            << vector_json(result.best_objective_history) << ",\n"
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
        std::cerr << "L0499 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
