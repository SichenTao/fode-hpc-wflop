/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T67 pure-C++ paper-case and CPU-HPC command line
Paper DOI: 10.1016/j.renene.2016.10.038
Public source: no target MATLAB source or native 61-turbine arrays located.
Related public source: https://github.com/NatLabRockies/SAM is used only as
an independent commercial-turbine range reference.
Missing information, completion rules, semantic IDs, HPC design, controlling
contract, and claim boundary:
include/core99/abdulrahman_t67.hpp
Claim boundary: declared academic reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/abdulrahman_t67.hpp"

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
    core99::t67::LayoutKind layout =
        core99::t67::LayoutKind::turbine_in_line;
    int spacing = 3;
    double speed = 8.0;
    core99::t67::Terrain terrain = core99::t67::Terrain::onshore;
    core99::t67::Objective objective =
        core99::t67::Objective::maximum_power;
    int workers = 20;
    int population = 256;
    int generations = 3000;
    int stall_generations = 50;
    double tolerance = 1.0e-15;
    std::uint64_t seed = 20260731;
    std::filesystem::path output;
};

core99::t67::LayoutKind parse_layout(const std::string& value) {
    if (value == "til") {
        return core99::t67::LayoutKind::turbine_in_line;
    }
    if (value == "array") return core99::t67::LayoutKind::array;
    if (value == "staggered") {
        return core99::t67::LayoutKind::staggered;
    }
    throw std::invalid_argument("T67 unknown layout " + value);
}

core99::t67::Terrain parse_terrain(const std::string& value) {
    if (value == "onshore") return core99::t67::Terrain::onshore;
    if (value == "offshore") return core99::t67::Terrain::offshore;
    throw std::invalid_argument("T67 unknown terrain " + value);
}

core99::t67::Objective parse_objective(const std::string& value) {
    if (value == "max_power") {
        return core99::t67::Objective::maximum_power;
    }
    if (value == "max_cf") {
        return core99::t67::Objective::maximum_capacity_factor;
    }
    if (value == "min_tciop") {
        return core99::t67::Objective::minimum_tciop;
    }
    throw std::invalid_argument("T67 unknown objective " + value);
}

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument(
                    "T67 missing value for " + key
                );
            }
            return std::string(argv[index]);
        };
        if (key == "--mode") result.mode = value();
        else if (key == "--layout") result.layout = parse_layout(value());
        else if (key == "--spacing") result.spacing = std::stoi(value());
        else if (key == "--reference-speed") {
            result.speed = std::stod(value());
        } else if (key == "--terrain") {
            result.terrain = parse_terrain(value());
        } else if (key == "--objective") {
            result.objective = parse_objective(value());
        } else if (key == "--workers") {
            result.workers = std::stoi(value());
        } else if (key == "--population") {
            result.population = std::stoi(value());
        } else if (key == "--generations") {
            result.generations = std::stoi(value());
        } else if (key == "--stall-generations") {
            result.stall_generations = std::stoi(value());
        } else if (key == "--tolerance") {
            result.tolerance = std::stod(value());
        } else if (key == "--seed") {
            result.seed = std::stoull(value());
        } else if (key == "--output") {
            result.output = value();
        } else {
            throw std::invalid_argument("T67 unknown option " + key);
        }
    }
    return result;
}

std::string decision_json(const core99::t67::Decision& value) {
    std::ostringstream output;
    output << std::setprecision(17) << "{\"y_m\":[";
    for (std::size_t index = 0; index < value.y_m.size(); ++index) {
        if (index > 0) output << ',';
        output << value.y_m[index];
    }
    output << "],\"turbine_code\":[";
    for (std::size_t index = 0;
         index < value.turbine_code.size();
         ++index) {
        if (index > 0) output << ',';
        output << value.turbine_code[index];
    }
    output << "],\"hub_height_m\":[";
    for (std::size_t index = 0;
         index < value.hub_height_m.size();
         ++index) {
        if (index > 0) output << ',';
        output << value.hub_height_m[index];
    }
    output << "]}";
    return output.str();
}

std::string evaluation_json(const core99::t67::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"total_power_mw\":" << value.total_power_mw
        << ",\"rated_power_mw\":" << value.rated_power_mw
        << ",\"capacity_factor\":" << value.capacity_factor
        << ",\"total_cost_index\":" << value.total_cost_index
        << ",\"total_cost_index_per_output_power\":"
        << value.total_cost_index_per_output_power
        << ",\"minimum_spacing_margin_m\":"
        << value.minimum_spacing_margin_m
        << ",\"feasible\":"
        << (value.feasible ? "true" : "false") << '}';
    return output.str();
}

std::string run_json(const core99::t67::RunResult& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"case_id\":\"" << value.case_id
        << "\",\"method_semantic_id\":\""
        << value.method_semantic_id
        << "\",\"problem_semantic_id\":\""
        << value.problem_semantic_id
        << "\",\"protocol_semantic_id\":\""
        << value.protocol_semantic_id
        << "\",\"seed\":" << value.seed
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"population_size\":" << value.population_size
        << ",\"generations\":" << value.generations
        << ",\"physical_fes\":" << value.physical_fes
        << ",\"converged\":"
        << (value.converged ? "true" : "false")
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex
        << value.scientific_hash << std::dec
        << "\",\"best_decision\":" << decision_json(value.best_decision)
        << ",\"best_evaluation\":"
        << evaluation_json(value.best_evaluation) << '}';
    return output.str();
}

void emit(
    const std::string& payload,
    const std::filesystem::path& output
) {
    if (output.empty()) {
        std::cout << payload << '\n';
        return;
    }
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    std::ofstream stream(output);
    if (!stream) throw std::runtime_error("T67 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            const auto cases = core99::t67::paper_case_ids();
            std::ostringstream output;
            output << "{\"paper_case_roles\":[";
            for (std::size_t index = 0; index < cases.size(); ++index) {
                if (index > 0) output << ',';
                output << '"' << cases[index] << '"';
            }
            output << "],\"role_count\":" << cases.size()
                << ",\"unique_instance_count\":" << cases.size()
                << '}';
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "catalog") {
            const auto& catalog = core99::t67::turbine_catalog();
            std::ostringstream output;
            output << std::setprecision(17) << "{\"turbines\":[";
            for (std::size_t index = 0; index < catalog.size(); ++index) {
                if (index > 0) output << ',';
                const auto& item = catalog[index];
                output << "{\"code\":" << item.code
                    << ",\"rated_power_mw\":" << item.rated_power_mw
                    << ",\"diameter_m\":" << item.diameter_m
                    << ",\"rated_speed_mps\":" << item.rated_speed_mps
                    << ",\"cut_in_mps\":" << item.cut_in_mps
                    << ",\"cut_out_mps\":" << item.cut_out_mps
                    << '}';
            }
            output << "]}";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        const core99::t67::Problem problem(
            arguments.layout,
            arguments.spacing,
            arguments.speed,
            arguments.terrain,
            arguments.objective
        );
        if (arguments.mode == "inspect") {
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"case_id\":\"" << problem.id()
                << "\",\"problem_semantic_id\":"
                   "\"t67_til_swf_power_cf_tciop_162role_declared_v1\""
                << ",\"turbine_count\":" << problem.turbine_count()
                << ",\"length_x_m\":" << problem.length_x_m()
                << ",\"length_y_m\":" << problem.length_y_m()
                << ",\"population_size_completion\":256"
                << ",\"maximum_generations\":3000"
                << ",\"tolerance_function\":1e-15}";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "evaluate") {
            const auto decision = problem.reference_decision();
            const auto evaluation = problem.evaluate(decision);
            emit(
                "{\"case_id\":\"" + problem.id()
                    + "\",\"decision\":" + decision_json(decision)
                    + ",\"evaluation\":"
                    + evaluation_json(evaluation) + "}",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument(
                "T67 mode must be list-cases, catalog, inspect, evaluate, "
                "or optimize"
            );
        }
        core99::t67::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.population_size = arguments.population;
        config.maximum_generations = arguments.generations;
        config.stall_generations = arguments.stall_generations;
        config.tolerance_function = arguments.tolerance;
        emit(
            run_json(core99::t67::run(problem, config)),
            arguments.output
        );
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
