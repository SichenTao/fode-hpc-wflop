/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T76 pure-C++ paper-case and CPU-HPC command line
Paper DOI: 10.1016/j.energy.2018.11.073
Public source: no target MATLAB source or native problem arrays located.
Cited MPGA lineage: 10.1016/j.jweia.2015.01.018 and repository T62.
Missing information, completions, semantic IDs, HPC design, controlling
contract, and claim boundary: include/core99/sun_t76.hpp
Claim boundary: declared academic reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/sun_t76.hpp"

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
    core99::t76::CaseRole role =
        core99::t76::CaseRole::case2_directional_mpga;
    int workers = 20;
    int demes = 10;
    int individuals = 20;
    int unchanged = 500;
    int generations = 5000;
    int migration = 20;
    std::uint64_t seed = 20260731;
    std::filesystem::path output;
};

core99::t76::CaseRole parse_case(const std::string& value) {
    using Role = core99::t76::CaseRole;
    if (value == "case1_omnidirectional_aligned") {
        return Role::case1_omnidirectional_aligned;
    }
    if (value == "case1_directional_aligned") {
        return Role::case1_directional_aligned;
    }
    if (value == "case2_omnidirectional_mpga") {
        return Role::case2_omnidirectional_mpga;
    }
    if (value == "case2_directional_mpga") {
        return Role::case2_directional_mpga;
    }
    if (value == "case3_directional_multitype_mpga") {
        return Role::case3_directional_multitype_mpga;
    }
    if (value == "case4_sha_chau_multitype_mpga") {
        return Role::case4_sha_chau_multitype_mpga;
    }
    throw std::invalid_argument("T76 unknown case " + value);
}

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument("T76 missing value for " + key);
            }
            return std::string(argv[index]);
        };
        if (key == "--mode") result.mode = value();
        else if (key == "--case") result.role = parse_case(value());
        else if (key == "--workers") result.workers = std::stoi(value());
        else if (key == "--demes") result.demes = std::stoi(value());
        else if (key == "--individuals") {
            result.individuals = std::stoi(value());
        } else if (key == "--unchanged-generations") {
            result.unchanged = std::stoi(value());
        } else if (key == "--max-generations") {
            result.generations = std::stoi(value());
        } else if (key == "--migration-period") {
            result.migration = std::stoi(value());
        } else if (key == "--seed") {
            result.seed = std::stoull(value());
        } else if (key == "--output") {
            result.output = value();
        } else {
            throw std::invalid_argument("T76 unknown option " + key);
        }
    }
    return result;
}

std::vector<core99::t76::Point> reference_layout(
    const core99::t76::Problem& problem
) {
    if (!problem.optimized()) return problem.aligned_layout();
    const int columns = problem.turbine_count() == 48 ? 8 : 9;
    const int rows = (problem.turbine_count() + columns - 1) / columns;
    std::vector<core99::t76::Point> result;
    result.reserve(static_cast<std::size_t>(problem.turbine_count()));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            if (static_cast<int>(result.size()) == problem.turbine_count()) {
                return result;
            }
            result.push_back({
                problem.farm_side_m() * static_cast<double>(column)
                    / static_cast<double>(columns - 1),
                problem.farm_side_m() * static_cast<double>(row)
                    / static_cast<double>(rows - 1),
            });
        }
    }
    return result;
}

std::string layout_json(const std::vector<core99::t76::Point>& layout) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index > 0) output << ',';
        output << '[' << layout[index].x_m << ',' << layout[index].y_m << ']';
    }
    output << ']';
    return output.str();
}

std::string evaluation_json(const core99::t76::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"expected_power_mw\":" << value.expected_power_mw
        << ",\"theoretical_no_wake_power_mw\":"
        << value.theoretical_no_wake_power_mw
        << ",\"utilization_rate\":" << value.utilization_rate
        << ",\"minimum_turbine_power_kw\":"
        << value.minimum_turbine_power_kw
        << ",\"maximum_turbine_power_kw\":"
        << value.maximum_turbine_power_kw
        << ",\"inactive_turbine_states\":"
        << value.inactive_turbine_states
        << ",\"boundary_violation_m\":" << value.boundary_violation_m
        << ",\"feasible\":" << (value.feasible ? "true" : "false")
        << '}';
    return output.str();
}

std::string run_json(const core99::t76::RunResult& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"case_id\":\"" << value.case_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"protocol_semantic_id\":\"" << value.protocol_semantic_id
        << "\",\"seed\":" << value.seed
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"demes\":" << value.demes
        << ",\"individuals_per_deme\":" << value.individuals_per_deme
        << ",\"generations\":" << value.generations
        << ",\"unchanged_generations\":" << value.unchanged_generations
        << ",\"physical_fes\":" << value.physical_fes
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex
        << value.scientific_hash << std::dec
        << "\",\"best_layout\":" << layout_json(value.best_layout)
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
    if (!stream) throw std::runtime_error("T76 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            const auto cases = core99::t76::paper_case_ids();
            std::ostringstream output;
            output << "{\"paper_case_roles\":[";
            for (std::size_t index = 0; index < cases.size(); ++index) {
                if (index > 0) output << ',';
                output << '"' << cases[index] << '"';
            }
            output << "],\"role_count\":" << cases.size()
                << ",\"optimized_role_count\":4} ";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "catalog") {
            std::ostringstream output;
            output << std::setprecision(17) << "{\"turbines\":[";
            const auto& catalog = core99::t76::turbine_catalog();
            for (std::size_t index = 0; index < catalog.size(); ++index) {
                if (index > 0) output << ',';
                const auto& item = catalog[index];
                output << "{\"name\":\"" << item.name
                    << "\",\"rated_power_kw\":" << item.rated_power_kw
                    << ",\"diameter_m\":" << item.diameter_m
                    << ",\"cut_in_mps\":" << item.cut_in_mps
                    << ",\"rated_speed_mps\":" << item.rated_speed_mps
                    << ",\"cut_out_mps\":" << item.cut_out_mps
                    << ",\"hub_height_m\":" << item.hub_height_m
                    << ",\"anchor_speed_mps\":" << item.anchor_speed_mps
                    << ",\"anchor_power_kw\":" << item.anchor_power_kw
                    << '}';
            }
            output << "]}";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        const core99::t76::Problem problem(arguments.role);
        if (arguments.mode == "inspect") {
            double probability = 0.0;
            for (const auto& state : problem.wind_states()) {
                probability += state.probability;
            }
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"case_id\":\"" << problem.id()
                << "\",\"optimized\":"
                << (problem.optimized() ? "true" : "false")
                << ",\"turbine_count\":" << problem.turbine_count()
                << ",\"wind_state_count\":" << problem.wind_states().size()
                << ",\"wind_probability_sum\":" << probability
                << ",\"reference_height_m\":"
                << problem.reference_height_m()
                << ",\"shear_exponent\":" << problem.shear_exponent()
                << ",\"directional_crosswind_ratio\":"
                << problem.directional_crosswind_ratio() << '}';
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "evaluate") {
            const auto layout = reference_layout(problem);
            const auto evaluation = problem.evaluate(layout);
            emit(
                "{\"case_id\":\"" + problem.id()
                    + "\",\"layout\":" + layout_json(layout)
                    + ",\"evaluation\":"
                    + evaluation_json(evaluation) + "}",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument(
                "T76 mode must be list-cases, catalog, inspect, evaluate, "
                "or optimize"
            );
        }
        core99::t76::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.demes = arguments.demes;
        config.individuals_per_deme = arguments.individuals;
        config.unchanged_generations = arguments.unchanged;
        config.maximum_generations = arguments.generations;
        config.migration_period = arguments.migration;
        emit(run_json(core99::t76::run(problem, config)), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
