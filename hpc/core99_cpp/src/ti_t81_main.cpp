/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T81 pure-C++ paper-case and CPU-HPC command line
Paper DOI: 10.1016/j.apenergy.2021.117947
Public source: no target source or native wind/bathymetry/wave arrays.
Missing information, completions, semantic IDs, HPC design, controlling
contract, and claim boundary: include/core99/ti_t81.hpp
Claim boundary: academic flexible reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/ti_t81.hpp"

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
    core99::t81::CaseRole role = core99::t81::CaseRole::mild_slope;
    int workers = 20;
    int multistarts = 15;
    int maximum_evaluations = 300;
    double relative_x_tolerance = 1.0e-7;
    std::uint64_t seed = 20260731;
    std::filesystem::path output;
};

core99::t81::CaseRole parse_case(const std::string& value) {
    if (value == "case1_mild_slope") {
        return core99::t81::CaseRole::mild_slope;
    }
    if (value == "case2_complex_terrain") {
        return core99::t81::CaseRole::complex_terrain;
    }
    throw std::invalid_argument("T81 unknown case " + value);
}

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument("T81 missing value for " + key);
            }
            return std::string(argv[index]);
        };
        if (key == "--mode") result.mode = value();
        else if (key == "--case") result.role = parse_case(value());
        else if (key == "--workers") result.workers = std::stoi(value());
        else if (key == "--multistarts") {
            result.multistarts = std::stoi(value());
        } else if (key == "--maxeval-per-start") {
            result.maximum_evaluations = std::stoi(value());
        } else if (key == "--xtol-rel") {
            result.relative_x_tolerance = std::stod(value());
        } else if (key == "--seed") {
            result.seed = std::stoull(value());
        } else if (key == "--output") {
            result.output = value();
        } else {
            throw std::invalid_argument("T81 unknown option " + key);
        }
    }
    return result;
}

std::string layout_json(const std::vector<core99::t81::Point>& layout) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index > 0) output << ',';
        output << '[' << layout[index].x_m << ',' << layout[index].y_m << ']';
    }
    output << ']';
    return output.str();
}

std::string evaluation_json(const core99::t81::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"aep_gwh\":" << value.aep_gwh
        << ",\"total_wave_load_index\":" << value.total_wave_load_index
        << ",\"minimum_spacing_m\":" << value.minimum_spacing_m
        << ",\"spacing_violation_m\":" << value.spacing_violation_m
        << ",\"boundary_violation_m\":" << value.boundary_violation_m
        << ",\"feasible\":" << (value.feasible ? "true" : "false")
        << '}';
    return output.str();
}

std::string run_json(const core99::t81::RunResult& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"case_id\":\"" << value.case_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"protocol_semantic_id\":\"" << value.protocol_semantic_id
        << "\",\"seed\":" << value.seed
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"physical_fes\":" << value.physical_fes
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex
        << value.scientific_hash << std::dec << "\",\"stages\":[";
    for (std::size_t index = 0; index < value.stages.size(); ++index) {
        if (index > 0) output << ',';
        const auto& stage = value.stages[index];
        output << "{\"stage_id\":\"" << stage.stage_id
            << "\",\"alpha0\":" << stage.alpha0
            << ",\"multistarts\":" << stage.multistarts
            << ",\"successful_starts\":" << stage.successful_starts
            << ",\"physical_fes\":" << stage.physical_fes
            << ",\"seconds\":" << stage.seconds
            << ",\"beta\":" << stage.beta
            << ",\"alpha1\":" << stage.alpha1
            << ",\"best_layout\":" << layout_json(stage.best_layout)
            << ",\"best_evaluation\":"
            << evaluation_json(stage.best_evaluation) << '}';
    }
    output << "]}";
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
    if (!stream) throw std::runtime_error("T81 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            const auto cases = core99::t81::paper_case_ids();
            emit(
                "{\"paper_case_roles\":[\"" + cases[0] + "\",\""
                    + cases[1] + "\"],\"role_count\":2,"
                    "\"baseline_roles\":2,\"coupled_roles\":8}",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        const core99::t81::Problem problem(arguments.role);
        if (arguments.mode == "inspect") {
            double probability = 0.0;
            for (const auto& state : problem.wind_states()) {
                probability += state.probability;
            }
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"case_id\":\"" << problem.id()
                << "\",\"turbine_count\":" << problem.turbine_count()
                << ",\"wind_state_count\":" << problem.wind_states().size()
                << ",\"wind_probability_sum\":" << probability
                << ",\"polygon_vertex_count\":"
                << problem.boundary_polygon().size() << '}';
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "evaluate") {
            const auto layout = problem.reference_layout();
            emit(
                "{\"case_id\":\"" + problem.id()
                    + "\",\"layout\":" + layout_json(layout)
                    + ",\"evaluation\":"
                    + evaluation_json(problem.evaluate(layout)) + "}",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument(
                "T81 mode must be list-cases, inspect, evaluate, or optimize"
            );
        }
        core99::t81::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.multistarts = arguments.multistarts;
        config.maximum_evaluations_per_start = arguments.maximum_evaluations;
        config.relative_x_tolerance = arguments.relative_x_tolerance;
        emit(run_json(core99::t81::run(problem, config)), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
