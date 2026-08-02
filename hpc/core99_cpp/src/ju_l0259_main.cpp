/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0259 CLI and machine-readable H5/H6/formal receipts
Paper/DOI: public source, conflicts, missing facts, reconstruction
completion, semantic IDs, production backend and claim boundary:
hpc/core99_cpp/include/core99/ju_l0259.hpp.
Public source: pinned MIT author repository declared in the header.
Controlling contract: shared/contracts/core99_l0259_sugga_2019.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/ju_l0259.hpp"

#include <algorithm>
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
    std::string case_id = "l0259_d3_l5_n25";
    std::string variant = "paper_probability";
    std::string output;
    std::string indices;
    int workers = 20;
    int monte_carlo_layouts = -1;
    int population = -1;
    int generations = -1;
    int repeats = 1;
    std::uint64_t seed = 2026075900ULL;
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
        else if (flag == "--variant") result.variant = value();
        else if (flag == "--output") result.output = value();
        else if (flag == "--indices") result.indices = value();
        else if (flag == "--workers") result.workers = std::stoi(value());
        else if (flag == "--monte-carlo-layouts") {
            result.monte_carlo_layouts = std::stoi(value());
        } else if (flag == "--population") {
            result.population = std::stoi(value());
        } else if (flag == "--generations") {
            result.generations = std::stoi(value());
        } else if (flag == "--repeats") {
            result.repeats = std::stoi(value());
        } else if (flag == "--seed") {
            result.seed = std::stoull(value());
        } else {
            throw std::invalid_argument("unknown L0259 flag " + flag);
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

core99::l0259::Layout parse_layout(
    const core99::l0259::Problem& problem,
    const std::string& value
) {
    if (value.empty()) {
        return core99::l0259::regular_reference_layout(problem);
    }
    core99::l0259::Layout result;
    std::istringstream input(value);
    std::string token;
    while (std::getline(input, token, ',')) {
        result.push_back(std::stoi(token));
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string evaluation_json(const core99::l0259::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"expected_power_kw\":" << value.expected_power_kw
        << ",\"efficiency_percent\":" << value.efficiency_percent
        << ",\"turbine_power_kw\":"
        << vector_json(value.turbine_power_kw)
        << ",\"feasible\":" << (value.feasible ? "true" : "false")
        << '}';
    return output.str();
}

std::string surrogate_json(
    const core99::l0259::SurrogateSnapshot& value
) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"training_targets_kw\":"
        << vector_json(value.training_targets_kw)
        << ",\"predictions_kw\":" << vector_json(value.predictions_kw)
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"monte_carlo_truth_seconds\":"
        << value.monte_carlo_truth_seconds
        << ",\"training_seconds\":" << value.training_seconds << '}';
    return output.str();
}

std::string run_json(const core99::l0259::RunResult& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"case_id\":\"" << value.case_id
        << "\",\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"seed\":" << value.seed
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"monte_carlo_layouts\":" << value.monte_carlo_layouts
        << ",\"population\":" << value.population
        << ",\"generations\":" << value.generations
        << ",\"surrogate_reused\":"
        << (value.surrogate_reused ? "true" : "false")
        << ",\"physical_fes\":" << value.physical_fes
        << ",\"initial_best\":" << evaluation_json(value.initial_best)
        << ",\"best_evaluation\":"
        << evaluation_json(value.best_evaluation)
        << ",\"best_layout\":" << vector_json(value.best_layout)
        << ",\"best_efficiency_history_percent\":"
        << vector_json(value.best_efficiency_history_percent)
        << ",\"monte_carlo_truth_seconds\":"
        << value.monte_carlo_truth_seconds
        << ",\"surrogate_training_seconds\":"
        << value.surrogate_training_seconds
        << ",\"population_truth_seconds\":"
        << value.population_truth_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex << value.scientific_hash
        << std::dec << "\"}";
    return output.str();
}

void emit(const std::string& content, const std::string& path) {
    if (path.empty()) {
        std::cout << content;
        return;
    }
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("cannot open L0259 output");
    stream << content;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            emit(
                "{\"paper_case_ids\":"
                    + string_vector_json(
                        core99::l0259::paper_case_ids()
                    )
                    + "}\n",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        const core99::l0259::Problem problem(
            arguments.case_id,
            arguments.variant
        );
        if (arguments.mode == "inspect") {
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"case_id\":\"" << problem.case_id()
                << "\",\"problem_semantic_id\":\"" << problem.semantic_id()
                << "\",\"variant\":\"" << arguments.variant
                << "\",\"wind_profile\":" << problem.wind_profile()
                << ",\"landscape\":" << problem.landscape()
                << ",\"cell_width_m\":" << problem.cell_width_m()
                << ",\"hub_height_m\":" << problem.hub_height_m()
                << ",\"wind_state_count\":" << problem.wind_state_count()
                << ",\"candidate_count\":144,\"turbine_count\":"
                << problem.turbine_count()
                << ",\"available_count\":" << problem.available_count()
                << ",\"paper_population\":" << problem.paper_population()
                << ",\"paper_generations\":" << problem.paper_generations()
                << ",\"paper_monte_carlo_layouts\":"
                << problem.paper_monte_carlo_layouts()
                << ",\"paper_repeats\":" << problem.paper_repeats()
                << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "evaluate") {
            emit(
                evaluation_json(
                    problem.evaluate(
                        parse_layout(problem, arguments.indices)
                    )
                ) + "\n",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "surrogate") {
            const int monte_carlo_layouts =
                arguments.monte_carlo_layouts < 0
                ? problem.paper_monte_carlo_layouts()
                : arguments.monte_carlo_layouts;
            emit(
                surrogate_json(problem.train_surrogate(
                    monte_carlo_layouts,
                    arguments.seed,
                    arguments.workers
                )) + "\n",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize" && arguments.mode != "formal") {
            throw std::invalid_argument(
                "unknown L0259 mode " + arguments.mode
            );
        }
        if (arguments.repeats < 1) {
            throw std::invalid_argument(
                "L0259 repeats must be positive"
            );
        }
        std::ostringstream output;
        output << "{\"mode\":\"" << arguments.mode << "\",\"runs\":[";
        for (int repeat = 0; repeat < arguments.repeats; ++repeat) {
            core99::l0259::RunConfig config;
            config.seed =
                arguments.seed + static_cast<std::uint64_t>(repeat);
            config.workers = arguments.workers;
            config.monte_carlo_layouts = arguments.monte_carlo_layouts;
            config.population = arguments.population;
            config.generations = arguments.generations;
            config.variant = arguments.variant;
            config.reuse_surrogate = arguments.repeats > 1;
            if (repeat != 0) output << ',';
            output << run_json(problem.optimize(config));
        }
        output << "]}\n";
        emit(output.str(), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "core99_l0259_hpc: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
