/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y09 pure-C++ CPU-HPC command line
Paper/DOI: Li et al.; 10.1016/j.renene.2025.124386
Public source/data, missing information, paper/patent conflicts, completion,
semantic IDs, production backend, controlling contract and claim boundary:
include/core99/li_y09.hpp
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/li_y09.hpp"

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
    std::string action = "optimize";
    std::string case_id = "Y09_west_multi";
    core99::y09::RunConfig config;
    std::filesystem::path output;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) throw std::invalid_argument("Y09 missing " + key);
            return std::string(argv[index]);
        };
        if (key == "--action") result.action = value();
        else if (key == "--case") result.case_id = value();
        else if (key == "--seed") result.config.seed = std::stoull(value());
        else if (key == "--workers") result.config.workers = std::stoi(value());
        else if (key == "--population") result.config.population = std::stoi(value());
        else if (key == "--maximum-generations") {
            result.config.maximum_generations = std::stoi(value());
        } else if (key == "--no-improvement-generations") {
            result.config.no_improvement_generations = std::stoi(value());
        } else if (key == "--crossover-rate") {
            result.config.crossover_rate = std::stod(value());
        } else if (key == "--mutation-rate") {
            result.config.total_mutation_rate = std::stod(value());
        } else if (key == "--disable-convergence") {
            result.config.enable_convergence = false;
        } else if (key == "--output") result.output = value();
        else throw std::invalid_argument("Y09 unknown option " + key);
    }
    return result;
}

std::string evaluation_json(const core99::y09::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"feasible\":" << (value.feasible ? "true" : "false")
        << ",\"five_mw_turbines\":" << value.five_mw_turbines
        << ",\"fifteen_mw_turbines\":" << value.fifteen_mw_turbines
        << ",\"total_power_mw\":" << value.total_power_mw
        << ",\"construction_cost_units\":" << value.construction_cost_units
        << ",\"maintenance_cost_units\":" << value.maintenance_cost_units
        << ",\"lcoe_units_per_mw\":" << value.lcoe_units_per_mw
        << ",\"fatigue_standard_deviation\":"
        << value.fatigue_standard_deviation
        << ",\"average_maintenance_cost_units\":"
        << value.average_maintenance_cost_units
        << ",\"turbines\":[";
    for (std::size_t index = 0; index < value.turbines.size(); ++index) {
        if (index) out << ',';
        const auto& turbine = value.turbines[index];
        out << "{\"grid_index\":" << turbine.grid_index
            << ",\"type_code\":" << turbine.type_code
            << ",\"x_m\":" << turbine.x_m
            << ",\"y_m\":" << turbine.y_m
            << ",\"effective_speed_mps\":" << turbine.effective_speed_mps
            << ",\"turbulence_intensity\":" << turbine.turbulence_intensity
            << ",\"power_mw\":" << turbine.power_mw
            << ",\"fatigue_coefficient\":" << turbine.fatigue_coefficient
            << ",\"maintenance_cost_units\":"
            << turbine.maintenance_cost_units << '}';
    }
    return out.str() + "]}";
}

std::string layout_json(const std::vector<int>& layout) {
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index) out << ',';
        out << layout[index];
    }
    return out.str() + ']';
}

std::string result_json(const core99::y09::RunResult& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"case_id\":\"" << value.case_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"protocol_semantic_id\":\"" << value.protocol_semantic_id
        << "\",\"seed\":" << value.seed
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"parallel_regions\":" << value.parallel_regions
        << ",\"population\":" << value.population
        << ",\"generations\":" << value.generations
        << ",\"physical_fes\":" << value.physical_fes
        << ",\"convergence_reason\":\"" << value.convergence_reason
        << "\",\"final_mutation_probabilities\":{\"zero\":"
        << value.final_mutation_probabilities.zero
        << ",\"five\":" << value.final_mutation_probabilities.five
        << ",\"fifteen\":" << value.final_mutation_probabilities.fifteen
        << "},\"best_evaluation\":" << evaluation_json(value.best_evaluation)
        << ",\"best_layout\":" << layout_json(value.best_layout)
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex << value.scientific_hash
        << std::dec << "\"}";
    return out.str();
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
    if (!stream) throw std::runtime_error("Y09 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        Arguments arguments = parse(argc, argv);
        const core99::y09::Problem problem;
        if (arguments.action == "list-cases") {
            std::ostringstream out;
            out << "{\"native_repeats\":1,\"population\":100,"
                << "\"crossover_rate\":0.08,\"mutation_rate\":0.01,"
                << "\"candidate_count\":100,\"paper_native_cases\":[";
            for (std::size_t index = 0; index < problem.native_scenarios().size(); ++index) {
                if (index) out << ',';
                const auto& scenario = problem.native_scenarios()[index];
                out << "{\"case_id\":\"" << scenario.case_id
                    << "\",\"composition\":\""
                    << core99::y09::to_string(scenario.composition)
                    << "\",\"flow_direction_degrees\":"
                    << scenario.flow_direction_degrees
                    << ",\"fatigue_threshold\":" << scenario.fatigue_threshold
                    << ",\"five_to_fifteen_cost_ratio\":"
                    << scenario.five_to_fifteen_cost_ratio << '}';
            }
            emit(out.str() + "]}", arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action != "optimize") {
            throw std::invalid_argument("Y09 action list-cases/optimize");
        }
        const auto found = std::find_if(
            problem.native_scenarios().begin(), problem.native_scenarios().end(),
            [&](const core99::y09::Scenario& scenario) {
                return scenario.case_id == arguments.case_id;
            }
        );
        if (found == problem.native_scenarios().end()) {
            throw std::invalid_argument("Y09 unknown case " + arguments.case_id);
        }
        arguments.config.scenario = *found;
        emit(result_json(core99::y09::run(problem, arguments.config)), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Y09 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
