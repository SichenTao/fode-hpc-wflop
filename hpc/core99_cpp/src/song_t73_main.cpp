/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T73 pure-C++ CPU-HPC command line and JSON receipt
Paper/DOI: Song et al.; 10.1016/j.cie.2018.04.051.
Public assets, missing fields, conflicts, reconstruction and claim boundary:
include/core99/song_t73.hpp.
Semantic IDs and controlling contract:
shared/contracts/core99_t73_song_2018.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/song_t73.hpp"

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
    std::string action = "run";
    core99::t73::RunConfig config;
    std::filesystem::path output;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) throw std::invalid_argument("T73 missing " + key);
            return std::string(argv[index]);
        };
        if (key == "--action") result.action = value();
        else if (key == "--seed") result.config.seed = std::stoull(value());
        else if (key == "--workers") result.config.workers = std::stoi(value());
        else if (key == "--ga-population") {
            result.config.ga_population = std::stoi(value());
        } else if (key == "--ga-generations") {
            result.config.ga_generations = std::stoi(value());
        } else if (key == "--pattern-iterations") {
            result.config.pattern_iterations = std::stoi(value());
        } else if (key == "--maintenance-replications") {
            result.config.maintenance_replications = std::stoi(value());
        } else if (key == "--cluster-profile") {
            const std::string item = value();
            if (item == "equation_text_four") {
                result.config.cluster_profile =
                    core99::t73::ClusterProfile::equation_text_four;
            } else if (item == "figure_caption_two") {
                result.config.cluster_profile =
                    core99::t73::ClusterProfile::figure_caption_two;
            } else {
                throw std::invalid_argument("T73 unknown cluster profile");
            }
        } else if (key == "--output") result.output = value();
        else throw std::invalid_argument("T73 unknown option " + key);
    }
    return result;
}

std::string point_json(const core99::t73::Point& point) {
    std::ostringstream out;
    out << std::setprecision(17) << '[' << point.x_m << ',' << point.y_m << ']';
    return out.str();
}

std::string layout_json(const std::vector<core99::t73::Point>& layout) {
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index) out << ',';
        out << point_json(layout[index]);
    }
    return out.str() + ']';
}

std::string layout_evaluation_json(const core99::t73::LayoutEvaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"feasible\":" << (value.feasible ? "true" : "false")
        << ",\"turbine_count\":" << value.turbine_count
        << ",\"annual_energy_mwh\":" << value.annual_energy_mwh
        << ",\"energy_revenue_usd\":" << value.energy_revenue_usd
        << ",\"capacity_revenue_usd\":" << value.capacity_revenue_usd
        << ",\"annualized_capital_usd\":" << value.annualized_capital_usd
        << ",\"pre_maintenance_profit_usd\":"
        << value.pre_maintenance_profit_usd
        << ",\"minimum_spacing_m\":" << value.minimum_spacing_m << '}';
    return out.str();
}

std::string maintenance_json(const core99::t73::MaintenanceEvaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"inspection_interval_days\":" << value.inspection_interval_days
        << ",\"mean_cost_usd\":" << value.mean_cost_usd
        << ",\"inspection_cost_usd\":" << value.inspection_cost_usd
        << ",\"opportunistic_cost_usd\":" << value.opportunistic_cost_usd
        << ",\"condition_cost_usd\":" << value.condition_cost_usd
        << ",\"corrective_cost_usd\":" << value.corrective_cost_usd
        << ",\"downtime_cost_usd\":" << value.downtime_cost_usd
        << ",\"mean_downtime_days\":" << value.mean_downtime_days << '}';
    return out.str();
}

std::string role_json(const core99::t73::RoleResult& role) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"role\":\"" << role.role
        << "\",\"layout\":" << layout_evaluation_json(role.layout)
        << ",\"maintenance\":" << maintenance_json(role.maintenance)
        << ",\"integrated_profit_usd\":" << role.integrated_profit_usd << '}';
    return out.str();
}

std::string result_json(const core99::t73::RunResult& value) {
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
        << ",\"ga_population\":" << value.ga_population
        << ",\"ga_generations\":" << value.ga_generations
        << ",\"pattern_iterations\":" << value.pattern_iterations
        << ",\"maintenance_replications\":" << value.maintenance_replications
        << ",\"cluster_count\":" << value.cluster_count
        << ",\"layout_evaluations\":" << value.layout_evaluations
        << ",\"wind_scenario_turbine_evaluations\":"
        << value.wind_scenario_turbine_evaluations
        << ",\"component_life_events\":" << value.component_life_events
        << ",\"scenario_precompute_seconds\":"
        << value.scenario_precompute_seconds
        << ",\"layout_evaluator_seconds\":" << value.layout_evaluator_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"maintenance_simulation_seconds\":"
        << value.maintenance_simulation_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex << value.scientific_hash
        << std::dec << "\",\"discrete_layout\":"
        << layout_json(value.discrete_layout)
        << ",\"continuous_layout\":" << layout_json(value.continuous_layout)
        << ",\"cluster_assignment\":[";
    for (std::size_t index = 0; index < value.cluster_assignment.size(); ++index) {
        if (index) out << ',';
        out << value.cluster_assignment[index];
    }
    out << "],\"roles\":[";
    for (std::size_t index = 0; index < value.roles.size(); ++index) {
        if (index) out << ',';
        out << role_json(value.roles[index]);
    }
    return out.str() + "]}";
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
    if (!stream) throw std::runtime_error("T73 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const auto arguments = parse(argc, argv);
        if (arguments.action == "describe") {
            emit(
                "{\"case_id\":\"nj342\",\"candidate_count\":342,"
                "\"wind_samples\":40,\"paper_role_count\":12,"
                "\"inspection_intervals\":[200,250,332,350,400],"
                "\"cluster_profiles\":[\"equation_text_four\","
                "\"figure_caption_two\"]}",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        if (arguments.action != "run") {
            throw std::invalid_argument("T73 action describe/run");
        }
        const core99::t73::Problem problem;
        emit(
            result_json(core99::t73::run(problem, arguments.config)),
            arguments.output
        );
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T73 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
