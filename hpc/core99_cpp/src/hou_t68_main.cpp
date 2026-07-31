/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T68 pure-C++ paper-case and CPU-HPC command line
Paper DOI: 10.1109/TSTE.2016.2614266
Public source, missing information, conflicts, completion, semantic IDs, HPC
design, controlling contract, and claim boundary: include/core99/hou_t68.hpp
Claim boundary: academic flexible reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/hou_t68.hpp"

#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string mode = "optimize";
    core99::t68::Role role = core99::t68::Role::direction_only;
    int workers = 20;
    int population_override = 0;
    int iteration_override = 0;
    int unchanged_iterations = 50;
    std::uint64_t seed = 20260731;
    double theta_override = std::numeric_limits<double>::quiet_NaN();
    std::filesystem::path output;
};

core99::t68::Role parse_case(const std::string& value) {
    if (value == "direction_only" || value == "t68_direction_only") {
        return core99::t68::Role::direction_only;
    }
    if (value == "scenario_i_spacing" || value == "t68_scenario_i_spacing") {
        return core99::t68::Role::scenario_i_spacing;
    }
    if (
        value == "scenario_ii_spacing_direction"
        || value == "t68_scenario_ii_spacing_direction"
    ) {
        return core99::t68::Role::scenario_ii_spacing_direction;
    }
    if (value == "scenario_iii_pitch" || value == "t68_scenario_iii_pitch") {
        return core99::t68::Role::scenario_iii_pitch;
    }
    if (value == "scenario_iv_codesign" || value == "t68_scenario_iv_codesign") {
        return core99::t68::Role::scenario_iv_codesign;
    }
    throw std::invalid_argument("T68 unknown case " + value);
}

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument("T68 missing value for " + key);
            }
            return std::string(argv[index]);
        };
        if (key == "--mode") result.mode = value();
        else if (key == "--case") result.role = parse_case(value());
        else if (key == "--workers") result.workers = std::stoi(value());
        else if (key == "--population") {
            result.population_override = std::stoi(value());
        } else if (key == "--iterations") {
            result.iteration_override = std::stoi(value());
        } else if (key == "--unchanged-iterations") {
            result.unchanged_iterations = std::stoi(value());
        } else if (key == "--seed") {
            result.seed = std::stoull(value());
        } else if (key == "--theta") {
            result.theta_override = std::stod(value());
        } else if (key == "--output") {
            result.output = value();
        } else {
            throw std::invalid_argument("T68 unknown option " + key);
        }
    }
    return result;
}

std::string evaluation_json(const core99::t68::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"gross_energy_gwh\":" << value.gross_energy_gwh
        << ",\"cable_loss_gwh\":" << value.cable_loss_gwh
        << ",\"net_energy_gwh\":" << value.net_energy_gwh
        << ",\"cable_cost_mdkk\":" << value.cable_cost_mdkk
        << ",\"annualized_cost_mdkk\":" << value.annualized_cost_mdkk
        << ",\"pitch_penalty_mdkk\":" << value.pitch_penalty_mdkk
        << ",\"lpc_dkk_per_mwh\":" << value.lpc_dkk_per_mwh
        << ",\"theta_deg\":" << value.theta_deg
        << ",\"minimum_spacing_m\":" << value.minimum_spacing_m
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
    output << ']';
    return output.str();
}

std::string run_json(const core99::t68::RunResult& value) {
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
        << ",\"converged\":" << (value.converged ? "true" : "false")
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex << value.scientific_hash
        << std::dec << "\",\"best_decision\":"
        << vector_json(value.best_decision)
        << ",\"best_evaluation\":"
        << evaluation_json(value.best_evaluation) << '}';
    return output.str();
}

void emit(const std::string& payload, const std::filesystem::path& output) {
    if (output.empty()) {
        std::cout << payload << '\n';
        return;
    }
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    std::ofstream stream(output);
    if (!stream) throw std::runtime_error("T68 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            const auto cases = core99::t68::paper_case_ids();
            std::ostringstream output;
            output << "{\"paper_case_roles\":[";
            for (std::size_t index = 0; index < cases.size(); ++index) {
                if (index > 0) output << ',';
                output << '\"' << cases[index] << '\"';
            }
            output << "],\"role_count\":" << cases.size()
                << ",\"formal_target_runs\":90}";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        const core99::t68::Problem problem(arguments.role);
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
                << ",\"maximum_iterations\":"
                << problem.maximum_iterations()
                << ",\"paper_repeats\":" << problem.paper_repeats()
                << ",\"wind_state_count\":" << problem.wind_states().size()
                << ",\"wind_probability_sum\":" << probability << '}';
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "evaluate") {
            auto decision = problem.reference_decision();
            if (std::isfinite(arguments.theta_override)) {
                if (arguments.role != core99::t68::Role::direction_only) {
                    throw std::invalid_argument(
                        "T68 --theta evaluate override is direction-only"
                    );
                }
                decision[0] = arguments.theta_override;
            }
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
                "T68 mode must be list-cases, inspect, evaluate, or optimize"
            );
        }
        core99::t68::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.population_override = arguments.population_override;
        config.iteration_override = arguments.iteration_override;
        config.unchanged_iterations = arguments.unchanged_iterations;
        emit(run_json(core99::t68::run(problem, config)), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
