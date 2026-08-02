/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T78 pure-C++ paper-case and CPU-HPC command line
Paper DOI: 10.1016/j.apenergy.2020.114896
Public source, missing information, conflicts, declared completion, semantic
IDs, HPC design, controlling contract and claim boundary:
include/core99/wu_t78.hpp
Claim boundary: academic flexible reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/wu_t78.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string mode = "optimize";
    core99::t78::Role role = core99::t78::Role::strict_noise_control;
    int workers = 20;
    int population_override = 0;
    int iteration_override = 0;
    std::uint64_t seed = 20260731;
    std::filesystem::path output;
};

core99::t78::Role parse_case(const std::string& value) {
    if (value == "strict_noise_control" || value == "t78_strict_noise_control") {
        return core99::t78::Role::strict_noise_control;
    }
    if (value == "economic_compensation" || value == "t78_economic_compensation") {
        return core99::t78::Role::economic_compensation;
    }
    throw std::invalid_argument("T78 unknown case " + value);
}

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument("T78 missing value for " + key);
            }
            return std::string(argv[index]);
        };
        if (key == "--mode") result.mode = value();
        else if (key == "--case") result.role = parse_case(value());
        else if (key == "--workers") result.workers = std::stoi(value());
        else if (key == "--population") result.population_override = std::stoi(value());
        else if (key == "--iterations") result.iteration_override = std::stoi(value());
        else if (key == "--seed") result.seed = std::stoull(value());
        else if (key == "--output") result.output = value();
        else throw std::invalid_argument("T78 unknown option " + key);
    }
    return result;
}

std::string evaluation_json(const core99::t78::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"annual_energy_gwh\":" << value.annual_energy_gwh
        << ",\"maximum_l10_dba\":" << value.maximum_l10_dba
        << ",\"excess_noise_dba\":" << value.excess_noise_dba
        << ",\"minimum_spacing_m\":" << value.minimum_spacing_m
        << ",\"spacing_violation_m\":" << value.spacing_violation_m
        << ",\"hard_noise_violation_dba\":"
        << value.hard_noise_violation_dba
        << ",\"noise_penalty_gwh\":" << value.noise_penalty_gwh
        << ",\"constraint_penalty_gwh\":" << value.constraint_penalty_gwh
        << ",\"objective_gwh\":" << value.objective_gwh
        << ",\"feasible\":" << (value.feasible ? "true" : "false")
        << '}';
    return output.str();
}

std::string vector_json(const std::vector<double>& values) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) output << ',';
        output << values[index];
    }
    return output.str() + ']';
}

std::string run_json(const core99::t78::RunResult& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"case_id\":\"" << value.case_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"protocol_semantic_id\":\"" << value.protocol_semantic_id
        << "\",\"seed\":" << value.seed
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"population_size\":" << value.population_size
        << ",\"generations\":" << value.generations
        << ",\"dimensions\":" << value.dimensions
        << ",\"physical_fes\":" << value.physical_fes
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex << value.scientific_hash
        << std::dec << "\",\"best_decision\":"
        << vector_json(value.best_decision)
        << ",\"best_evaluation\":" << evaluation_json(value.best_evaluation)
        << '}';
    return output.str();
}

void emit(const std::string& payload, const std::filesystem::path& path) {
    if (path.empty()) {
        std::cout << payload << '\n';
        return;
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("T78 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            emit(
                "{\"paper_case_roles\":[\"strict_noise_control\","
                "\"economic_compensation\"],\"role_count\":2,"
                "\"formal_target_runs\":20}",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        const core99::t78::Problem problem(arguments.role);
        if (arguments.mode == "inspect") {
            double probability = 0.0;
            for (const auto& state : problem.wind_states()) {
                probability += state.probability;
            }
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"case_id\":\"" << problem.id()
                << "\",\"dimensions\":" << problem.dimensions()
                << ",\"population_size\":" << problem.population_size()
                << ",\"maximum_iterations\":" << problem.maximum_iterations()
                << ",\"paper_repeats\":" << problem.paper_repeats()
                << ",\"wind_state_count\":" << problem.wind_states().size()
                << ",\"wind_probability_sum\":" << probability << '}';
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "evaluate") {
            const auto decision = problem.reference_decision();
            emit(
                "{\"case_id\":\"" + problem.id()
                    + "\",\"decision\":" + vector_json(decision)
                    + ",\"evaluation\":"
                    + evaluation_json(problem.evaluate(decision)) + "}",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument(
                "T78 mode must be list-cases, inspect, evaluate, or optimize"
            );
        }
        core99::t78::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.population_override = arguments.population_override;
        config.iteration_override = arguments.iteration_override;
        emit(run_json(core99::t78::run(problem, config)), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
