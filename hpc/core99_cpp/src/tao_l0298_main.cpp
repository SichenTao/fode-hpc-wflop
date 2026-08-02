/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0298 pure-C++ CPU-HPC command line and JSON receipt
Paper/DOI: 10.1109/TSG.2020.3022378.
Public assets, missing information, conflicts, reconstruction, semantic IDs,
backend, contract and claim boundary: include/core99/tao_l0298.hpp.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/tao_l0298.hpp"

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
    core99::l0298::ProfileId profile =
        core99::l0298::ProfileId::model_comparison;
    core99::l0298::RunConfig config;
    std::filesystem::path output;
};

core99::l0298::ProfileId parse_profile(const std::string& value) {
    for (const auto profile : core99::l0298::paper_profiles()) {
        if (core99::l0298::to_string(profile) == value) return profile;
    }
    throw std::invalid_argument("L0298 unknown profile " + value);
}

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) throw std::invalid_argument("L0298 missing " + key);
            return std::string(argv[index]);
        };
        if (key == "--action") result.action = value();
        else if (key == "--profile") result.profile = parse_profile(value());
        else if (key == "--seed") result.config.seed = std::stoull(value());
        else if (key == "--workers") result.config.workers = std::stoi(value());
        else if (key == "--outer-population") {
            result.config.outer_population = std::stoi(value());
        } else if (key == "--outer-iterations") {
            result.config.outer_iterations = std::stoi(value());
        } else if (key == "--inner-population") {
            result.config.inner_population = std::stoi(value());
        } else if (key == "--inner-iterations") {
            result.config.inner_iterations = std::stoi(value());
        } else if (key == "--output") result.output = value();
        else throw std::invalid_argument("L0298 unknown option " + key);
    }
    return result;
}

std::string evaluation_json(const core99::l0298::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"feasible\":" << (value.feasible ? "true" : "false")
        << ",\"turbine_count\":" << value.turbine_count
        << ",\"installed_capacity_mw\":" << value.installed_capacity_mw
        << ",\"profit_rate_percent\":" << value.profit_rate_percent
        << ",\"capacity_factor_percent\":" << value.capacity_factor_percent
        << ",\"variability_percent\":" << value.variability_percent
        << ",\"cable_daily_cost_eur\":" << value.cable_daily_cost_eur
        << ",\"cable_length_m\":" << value.cable_length_m
        << ",\"grid_benefit_eur\":" << value.grid_benefit_eur
        << ",\"wind_daily_energy_mwh\":" << value.wind_daily_energy_mwh
        << ",\"constraint_violation\":" << value.constraint_violation << '}';
    return output.str();
}

std::string result_json(const core99::l0298::RunResult& result) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"profile_id\":\"" << result.profile_id << "\""
        << ",\"method_semantic_id\":\"" << result.method_semantic_id << "\""
        << ",\"problem_semantic_id\":\"" << result.problem_semantic_id << "\""
        << ",\"protocol_semantic_id\":\"" << result.protocol_semantic_id << "\""
        << ",\"seed\":" << result.seed
        << ",\"requested_workers\":" << result.requested_workers
        << ",\"observed_workers\":" << result.observed_workers
        << ",\"parallel_regions\":" << result.parallel_regions
        << ",\"outer_population\":" << result.outer_population
        << ",\"outer_iterations\":" << result.outer_iterations
        << ",\"inner_population\":" << result.inner_population
        << ",\"inner_iterations\":" << result.inner_iterations
        << ",\"complete_outer_evaluations\":"
        << result.complete_outer_evaluations
        << ",\"cable_particle_evaluations\":"
        << result.cable_particle_evaluations
        << ",\"hourly_wake_evaluations\":"
        << result.hourly_wake_evaluations
        << ",\"wake_and_coupled_evaluator_seconds\":"
        << result.wake_and_coupled_evaluator_seconds
        << ",\"evolutionary_orchestration_seconds\":"
        << result.evolutionary_orchestration_seconds
        << ",\"end_to_end_seconds\":" << result.end_to_end_seconds
        << ",\"scientific_hash\":" << result.scientific_hash
        << ",\"roles\":[";
    for (std::size_t index = 0; index < result.roles.size(); ++index) {
        if (index) output << ',';
        const auto& role = result.roles[index];
        output << "{\"role\":\"" << role.role << "\""
            << ",\"model\":\"" << role.model << "\""
            << ",\"evaluation\":" << evaluation_json(role.evaluation)
            << ",\"active_cells\":[";
        for (std::size_t cell = 0; cell < role.active_cells.size(); ++cell) {
            if (cell) output << ',';
            output << role.active_cells[cell];
        }
        output << "],\"cable_edges\":[";
        for (std::size_t edge = 0; edge < role.cable_edges.size(); ++edge) {
            if (edge) output << ',';
            const auto& item = role.cable_edges[edge];
            output << "{\"from_turbine\":" << item.from_turbine
                << ",\"to_node\":" << item.to_node
                << ",\"cable_type\":" << item.cable_type
                << ",\"flow_mw\":" << item.flow_mw
                << ",\"length_m\":" << item.length_m << '}';
        }
        output << "]}";
    }
    return output.str() + "]}";
}

void emit(const std::string& text, const std::filesystem::path& output) {
    if (output.empty()) {
        std::cout << text << '\n';
        return;
    }
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    const auto temporary = std::filesystem::path(output.string() + ".tmp");
    std::ofstream stream(temporary);
    if (!stream) throw std::runtime_error("cannot open L0298 output");
    stream << text << '\n';
    stream.close();
    std::filesystem::rename(temporary, output);
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.action == "list-profiles") {
            std::ostringstream output;
            output << '[';
            const auto profiles = core99::l0298::paper_profiles();
            for (std::size_t index = 0; index < profiles.size(); ++index) {
                if (index) output << ',';
                const core99::l0298::Problem problem(profiles[index]);
                output << "{\"profile\":\""
                    << core99::l0298::to_string(profiles[index]) << "\""
                    << ",\"paper_role_count\":"
                    << problem.expected_role_count() << '}';
            }
            emit(output.str() + ']', arguments.output);
            return 0;
        }
        if (arguments.action != "optimize") {
            throw std::invalid_argument("L0298 action must be optimize or list-profiles");
        }
        const core99::l0298::Problem problem(arguments.profile);
        emit(result_json(core99::l0298::run(problem, arguments.config)),
             arguments.output);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
