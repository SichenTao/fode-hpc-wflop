/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T80 CLI and machine-readable H5/H6/formal receipts
Paper/source/missing/reconstruction and semantic IDs:
hpc/core99_cpp/include/core99/bai_t80.hpp.
Public source: no T80 author source; lineage is declared in the header.
Claim boundary: academic reconstruction with a figure-derived NJ proxy.
Contract: shared/contracts/core99_t80_bai_aga_mcts_2022.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/bai_t80.hpp"

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
    std::string case_id = "t80_case1_s1_medium";
    std::string output;
    int workers = 20;
    int population = -1;
    int generations = -1;
    int mcts_simulations = -1;
    int repeats = 1;
    std::uint64_t seed = 2026080000ULL;
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
        else if (flag == "--workers") result.workers = std::stoi(value());
        else if (flag == "--population") result.population = std::stoi(value());
        else if (flag == "--generations") {
            result.generations = std::stoi(value());
        } else if (flag == "--mcts-simulations") {
            result.mcts_simulations = std::stoi(value());
        } else if (flag == "--repeats") {
            result.repeats = std::stoi(value());
        } else if (flag == "--seed") {
            result.seed = std::stoull(value());
        } else {
            throw std::invalid_argument("unknown T80 flag " + flag);
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
        output << values[index];
    }
    output << ']';
    return output.str();
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

std::string evaluation_json(const core99::gridwake::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"expected_power_kw\":" << value.expected_power_kw
        << ",\"ideal_power_kw\":" << value.ideal_power_kw
        << ",\"conversion_efficiency_percent\":"
        << value.conversion_efficiency_percent
        << ",\"cost_of_energy\":" << value.cost_of_energy
        << ",\"feasible\":" << (value.feasible ? "true" : "false")
        << '}';
    return output.str();
}

std::string run_json(const core99::t80::RunResult& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"case_id\":\"" << value.case_id
        << "\",\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"seed\":" << value.seed
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"population\":" << value.population
        << ",\"generations\":" << value.generations
        << ",\"mcts_simulations\":" << value.mcts_simulations
        << ",\"physical_fes\":" << value.physical_fes
        << ",\"initial_best\":" << evaluation_json(value.initial_best)
        << ",\"best_evaluation\":"
        << evaluation_json(value.best_evaluation)
        << ",\"best_layout\":" << vector_json(value.best_layout)
        << ",\"best_efficiency_history_percent\":"
        << vector_json(value.best_efficiency_history_percent)
        << ",\"population_evaluation_seconds\":"
        << value.population_evaluation_seconds
        << ",\"mcts_relocation_seconds\":"
        << value.mcts_relocation_seconds
        << ",\"genetic_operator_seconds\":"
        << value.genetic_operator_seconds
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
    if (!stream) throw std::runtime_error("cannot open T80 output");
    stream << content;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            emit(
                "{\"paper_case_ids\":"
                    + string_vector_json(core99::t80::paper_case_ids())
                    + "}\n",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        const core99::t80::Problem problem(arguments.case_id);
        if (arguments.mode == "inspect") {
            const auto& configuration =
                problem.evaluator().configuration();
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"case_id\":\"" << problem.case_id()
                << "\",\"problem_semantic_id\":\"" << problem.semantic_id()
                << "\",\"rows\":" << configuration.rows
                << ",\"columns\":" << configuration.columns
                << ",\"candidate_count\":"
                << problem.evaluator().candidate_count()
                << ",\"turbine_count\":" << configuration.turbine_count
                << ",\"cell_width_m\":" << configuration.cell_width_m
                << ",\"turbine\":\"" << configuration.turbine.name
                << "\",\"wind_state_count\":"
                << configuration.wind_states.size()
                << ",\"paper_population_completion\":"
                << problem.paper_population_completion()
                << ",\"paper_generations\":" << problem.paper_generations()
                << ",\"paper_mcts_simulations_completion\":"
                << problem.paper_mcts_simulations_completion()
                << ",\"paper_repeats\":" << problem.paper_repeats()
                << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize" && arguments.mode != "formal") {
            throw std::invalid_argument("unknown T80 mode " + arguments.mode);
        }
        if (arguments.repeats < 1) {
            throw std::invalid_argument("T80 repeats must be positive");
        }
        std::ostringstream output;
        output << "{\"mode\":\"" << arguments.mode << "\",\"runs\":[";
        for (int repeat = 0; repeat < arguments.repeats; ++repeat) {
            core99::t80::RunConfig config;
            config.seed = arguments.seed + static_cast<std::uint64_t>(repeat);
            config.workers = arguments.workers;
            config.population = arguments.population;
            config.generations = arguments.generations;
            config.mcts_simulations = arguments.mcts_simulations;
            if (repeat != 0) output << ',';
            output << run_json(problem.optimize(config));
        }
        output << "]}\n";
        emit(output.str(), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "core99_t80_hpc: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
