/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0341 CLI and machine-readable H5/H6/formal receipts
Paper: Tao et al., 10.1016/j.renene.2020.06.003.
Public source: no target code/data; legally recovered predecessors are listed
in the contract.
Missing fields and reconstruction: see source-top declaration and contract.
Semantic IDs: l0341_three_farm_3d_gaussian_v1;
l0341_mdpso_predecessor_completed_v1.
Contract: shared/contracts/core99_l0341_tao_3d_mdpso_2020.json.
Claim boundary: declared academic reproduction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/tao_l0341.hpp"

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
    std::string case_id = "l0341_uniform_wfa_c";
    std::string output;
    int workers = 20;
    int generations = -1;
    int population_override = -1;
    double speed_mps = 8.0;
    double direction_degrees = 0.0;
    std::uint64_t seed = 2026034100;
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
        else if (flag == "--generations") {
            result.generations = std::stoi(value());
        } else if (flag == "--population-override") {
            result.population_override = std::stoi(value());
        } else if (flag == "--speed-mps") {
            result.speed_mps = std::stod(value());
        } else if (flag == "--direction-degrees") {
            result.direction_degrees = std::stod(value());
        } else if (flag == "--seed") {
            result.seed = std::stoull(value());
        } else {
            throw std::invalid_argument("unknown L0341 flag: " + flag);
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
        if constexpr (std::is_same_v<T, std::string>) {
            output << '"' << values[index] << '"';
        } else {
            output << values[index];
        }
    }
    output << ']';
    return output.str();
}

std::string layout_json(const std::vector<core99::l0341::Turbine>& layout) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index != 0U) output << ',';
        output << "{\"type\":" << layout[index].type
            << ",\"x_m\":" << layout[index].x_m
            << ",\"y_m\":" << layout[index].y_m << '}';
    }
    output << ']';
    return output.str();
}

std::string evaluation_json(const core99::l0341::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"expected_power_mw\":" << value.expected_power_mw
        << ",\"capacity_factor_percent\":"
        << value.capacity_factor_percent
        << ",\"efficiency_percent\":" << value.efficiency_percent
        << ",\"installed_capacity_mw\":"
        << value.installed_capacity_mw
        << ",\"minimum_spacing_margin_m\":"
        << value.minimum_spacing_margin_m
        << ",\"constraint_violation\":"
        << value.constraint_violation
        << ",\"active_turbines\":" << value.active_turbines
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
    if (!stream) throw std::runtime_error("cannot open L0341 output");
    stream << content;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            emit(
                "{\"paper_case_ids\":"
                    + vector_json(core99::l0341::paper_case_ids())
                    + "}\n",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "diagnostic") {
            const auto evaluation = core99::l0341::evaluate_diagnostic_4x4(
                arguments.speed_mps, arguments.direction_degrees
            );
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"mode\":\"diagnostic_4x4\","
                << "\"reference_speed_mps\":" << arguments.speed_mps
                << ",\"direction_degrees\":"
                << arguments.direction_degrees
                << ",\"evaluation\":" << evaluation_json(evaluation)
                << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        const core99::l0341::Problem problem(arguments.case_id);
        if (arguments.mode == "inspect") {
            std::ostringstream output;
            output << "{\"mode\":\"inspect\",\"case_id\":\""
                << problem.case_id()
                << "\",\"problem_semantic_id\":\""
                << problem.semantic_id()
                << "\",\"nonuniform\":"
                << (problem.nonuniform() ? "true" : "false")
                << ",\"wind_scenario\":\"" << problem.wind_scenario()
                << "\",\"maximum_slots\":" << problem.maximum_slots()
                << ",\"decision_dimension\":"
                << problem.decision_dimension()
                << ",\"paper_population\":" << problem.paper_population()
                << ",\"paper_generations\":"
                << problem.paper_generations()
                << ",\"width_m\":" << problem.width_m()
                << ",\"height_m\":" << problem.height_m()
                << ",\"capacity_mw\":" << problem.capacity_mw()
                << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument("unknown L0341 mode");
        }
        core99::l0341::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.generations = arguments.generations;
        config.population_override = arguments.population_override;
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
            << "  \"population\":" << result.population << ",\n"
            << "  \"generations\":" << result.generations << ",\n"
            << "  \"physical_fes\":" << result.physical_fes << ",\n"
            << "  \"initial_best\":"
            << evaluation_json(result.initial_best) << ",\n"
            << "  \"best_evaluation\":"
            << evaluation_json(result.best_evaluation) << ",\n"
            << "  \"best_layout\":" << layout_json(result.best_layout) << ",\n"
            << "  \"best_power_history_mw\":"
            << vector_json(result.best_power_history_mw) << ",\n"
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
        std::cerr << "L0341 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
