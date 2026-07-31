/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0368 pure-C++ CPU-HPC command line
Paper DOI: 10.1016/j.enconman.2021.114610
Public asset, missing information, conflicts, corrections, reconstruction,
semantic IDs, backend, controlling contract and claim boundary:
include/core99/liu_l0368.hpp
Claim boundary: flexible academic reconstruction, not author target code,
private Nanao arrays, exact MATLAB defaults/random trajectory or replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/liu_l0368.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Arguments {
    std::string action = "optimize";
    std::string case_id = "L0368_S1W1";
    core99::l0368::RunConfig config;
    bool paper_linf_sensitivity = false;
    std::string layout_points;
    std::filesystem::path output;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) throw std::invalid_argument("L0368 missing " + key);
            return std::string(argv[index]);
        };
        if (key == "--action") result.action = value();
        else if (key == "--case") result.case_id = value();
        else if (key == "--seed") result.config.seed = std::stoull(value());
        else if (key == "--workers") result.config.workers = std::stoi(value());
        else if (key == "--population") result.config.population = std::stoi(value());
        else if (key == "--generations") result.config.generations = std::stoi(value());
        else if (key == "--crossover-fraction") {
            result.config.crossover_fraction = std::stod(value());
        } else if (key == "--elite-count") {
            result.config.elite_count = std::stoi(value());
        } else if (key == "--spacing") {
            const std::string mode = value();
            if (mode == "euclidean") result.paper_linf_sensitivity = false;
            else if (mode == "paper-linf") result.paper_linf_sensitivity = true;
            else throw std::invalid_argument("L0368 spacing must be euclidean or paper-linf");
        } else if (key == "--layout-points") {
            result.layout_points = value();
        } else if (key == "--output") result.output = value();
        else throw std::invalid_argument("L0368 unknown option " + key);
    }
    return result;
}

std::vector<core99::l0368::Point> parse_layout(const std::string& text) {
    std::vector<core99::l0368::Point> result;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t end = text.find(';', begin);
        const std::string token = text.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin
        );
        const std::size_t comma = token.find(',');
        if (comma == std::string::npos) {
            throw std::invalid_argument("L0368 layout point must be x,y");
        }
        result.push_back({
            std::stod(token.substr(0, comma)),
            std::stod(token.substr(comma + 1)),
        });
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    if (result.empty()) throw std::invalid_argument("L0368 empty layout");
    return result;
}

core99::l0368::Scenario find_scenario(
    const std::string& id,
    const bool paper_linf_sensitivity
) {
    for (auto scenario : core99::l0368::paper_scenarios()) {
        if (scenario.case_id == id) {
            if (paper_linf_sensitivity) {
                scenario.spacing = core99::l0368::SpacingKind::paper_linf_sensitivity;
            }
            return scenario;
        }
    }
    throw std::invalid_argument("unknown L0368 case " + id);
}

std::string metadata_json(const core99::l0368::Scenario& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"case_id\":\"" << value.case_id << "\""
        << ",\"terrain\":\"" << core99::l0368::to_string(value.terrain) << "\""
        << ",\"wind\":\"" << core99::l0368::to_string(value.wind) << "\""
        << ",\"spacing\":\"" << core99::l0368::to_string(value.spacing) << "\""
        << ",\"paper_turbine_anchor\":" << value.paper_turbine_anchor
        << ",\"paper_capital_power_anchor_million_gbp_per_mw\":"
        << value.paper_capital_power_anchor_million_gbp_per_mw
        << ",\"paper_total_power_anchor_mw\":"
        << value.paper_total_power_anchor_mw
        << ",\"paper_efficiency_anchor_percent\":"
        << value.paper_efficiency_anchor_percent << '}';
    return out.str();
}

std::string evaluation_json(const core99::l0368::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"feasible\":" << (value.feasible ? "true" : "false")
        << ",\"turbine_count\":" << value.turbine_count
        << ",\"capital_power_proxy_gbp_per_mw\":"
        << value.capital_power_proxy_gbp_per_mw
        << ",\"initial_capital_cost_gbp\":" << value.initial_capital_cost_gbp
        << ",\"wind_turbine_cost_gbp\":" << value.wind_turbine_cost_gbp
        << ",\"support_structure_cost_gbp\":"
        << value.support_structure_cost_gbp
        << ",\"cable_substation_port_cost_gbp\":"
        << value.cable_substation_port_cost_gbp
        << ",\"expected_power_mw\":" << value.expected_power_mw
        << ",\"no_wake_power_mw\":" << value.no_wake_power_mw
        << ",\"efficiency_percent\":" << value.efficiency_percent
        << ",\"minimum_distance_m\":" << value.minimum_distance_m
        << ",\"mean_water_depth_m\":" << value.mean_water_depth_m << '}';
    return out.str();
}

std::string layout_json(const std::vector<core99::l0368::Point>& layout) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index) out << ',';
        out << '[' << layout[index].x_m << ',' << layout[index].y_m << ']';
    }
    return out.str() + "]";
}

std::string result_json(
    const core99::l0368::Scenario& scenario,
    const core99::l0368::RunResult& result
) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"metadata\":" << metadata_json(scenario)
        << ",\"method_semantic_id\":\"" << result.method_semantic_id << "\""
        << ",\"problem_semantic_id\":\"" << result.problem_semantic_id << "\""
        << ",\"protocol_semantic_id\":\"" << result.protocol_semantic_id << "\""
        << ",\"seed\":" << result.seed
        << ",\"requested_workers\":" << result.requested_workers
        << ",\"observed_workers\":" << result.observed_workers
        << ",\"parallel_regions\":" << result.parallel_regions
        << ",\"population\":" << result.population
        << ",\"generations\":" << result.generations
        << ",\"physical_fes\":" << result.physical_fes
        << ",\"evaluator_seconds\":" << result.evaluator_seconds
        << ",\"algorithm_seconds\":" << result.algorithm_seconds
        << ",\"end_to_end_seconds\":" << result.end_to_end_seconds
        << ",\"scientific_hash\":" << result.scientific_hash
        << ",\"best_evaluation\":" << evaluation_json(result.best_evaluation)
        << ",\"best_layout\":" << layout_json(result.best_layout) << '}';
    return out.str();
}

void emit(const std::string& text, const std::filesystem::path& output) {
    if (output.empty()) {
        std::cout << text << '\n';
        return;
    }
    std::filesystem::create_directories(output.parent_path());
    const std::filesystem::path temporary = output.string() + ".tmp";
    std::ofstream stream(temporary);
    if (!stream) throw std::runtime_error("cannot open L0368 output");
    stream << text << '\n';
    stream.close();
    std::filesystem::rename(temporary, output);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.action == "list-cases") {
            std::ostringstream out;
            out << '[';
            const auto scenarios = core99::l0368::paper_scenarios();
            for (std::size_t index = 0; index < scenarios.size(); ++index) {
                if (index) out << ',';
                out << metadata_json(scenarios[index]);
            }
            emit(out.str() + "]", arguments.output);
            return 0;
        }
        const auto scenario = find_scenario(
            arguments.case_id, arguments.paper_linf_sensitivity
        );
        if (arguments.action == "metadata") {
            emit(metadata_json(scenario), arguments.output);
        } else if (arguments.action == "evaluate-layout") {
            const core99::l0368::Problem problem(scenario);
            const auto layout = parse_layout(arguments.layout_points);
            emit(
                "{\"metadata\":" + metadata_json(scenario)
                    + ",\"evaluation\":" + evaluation_json(problem.evaluate(layout))
                    + ",\"layout\":" + layout_json(layout) + "}",
                arguments.output
            );
        } else if (arguments.action == "optimize") {
            const core99::l0368::Problem problem(scenario);
            emit(result_json(
                scenario, core99::l0368::run(problem, arguments.config)
            ), arguments.output);
        } else {
            throw std::invalid_argument("unknown L0368 action " + arguments.action);
        }
    } catch (const std::exception& error) {
        std::cerr << "L0368 error: " << error.what() << '\n';
        return 2;
    }
    return 0;
}
