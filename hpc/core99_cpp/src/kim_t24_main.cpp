/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T24 pure-C++ CPU-HPC command line and receipt writer
Paper/DOI: Optimization of a Wind Farm Layout to Mitigate the Wind Power
Intermittency; 10.1016/j.apenergy.2024.123383
Public source, missing assets, paper-internal data conflict, reconstruction
completion, semantic IDs, production backend, and claim boundary:
hpc/core99_cpp/include/core99/kim_t24.hpp
Controlling contract: shared/contracts/core99_t24_kim_2024.json
Claim boundary: academic flexible declared reconstruction, not author code,
original Marado wind data, private arrays, random states, or numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/kim_t24.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Arguments {
    std::string mode = "optimize";
    std::string case_id = "uniform_p0";
    std::string output;
    int workers = 20;
    int population = -1;
    int generations = -1;
    std::uint64_t seed = 20260731;
    double model_y_over_d = 5.0;
    double model_speed_mps = 9.0;
    double model_direction_deg = 0.0;
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
        else if (flag == "--population") {
            result.population = std::stoi(value());
        } else if (flag == "--generations") {
            result.generations = std::stoi(value());
        } else if (flag == "--seed") {
            result.seed = std::stoull(value());
        } else if (flag == "--model-y-over-d") {
            result.model_y_over_d = std::stod(value());
        } else if (flag == "--model-speed-mps") {
            result.model_speed_mps = std::stod(value());
        } else if (flag == "--model-direction-deg") {
            result.model_direction_deg = std::stod(value());
        } else {
            throw std::invalid_argument("unknown T24 flag " + flag);
        }
    }
    return result;
}

core99::t24::CaseId parse_case(const std::string& value) {
    using core99::t24::CaseId;
    if (value == "uniform_p0") return CaseId::uniform_p0;
    if (value == "uniform_p007") return CaseId::uniform_p007;
    if (value == "uniform_p015") return CaseId::uniform_p015;
    if (value == "real_p0") return CaseId::real_p0;
    if (value == "real_p007") return CaseId::real_p007;
    if (value == "real_p015") return CaseId::real_p015;
    throw std::invalid_argument("unknown T24 case " + value);
}

std::string layout_json(
    const std::vector<core99::t24::Turbine>& layout
) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index != 0U) output << ',';
        output << "{\"x_m\":" << layout[index].x_m
            << ",\"y_m\":" << layout[index].y_m << '}';
    }
    output << ']';
    return output.str();
}

std::string evaluation_json(const core99::t24::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"mean_power_mw\":" << value.mean_power_mw
        << ",\"intermittency_mw\":" << value.intermittency_mw
        << ",\"spacing_violation_m\":" << value.spacing_violation_m
        << ",\"boundary_violation_m\":" << value.boundary_violation_m
        << ",\"feasible\":" << (value.feasible ? "true" : "false")
        << '}';
    return output.str();
}

std::string run_json(const core99::t24::RunResult& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"problem_id\":\"" << value.problem_id
        << "\",\"problem_semantic_id\":\""
        << value.problem_semantic_id
        << "\",\"method_semantic_id\":\""
        << value.method_semantic_id
        << "\",\"seed\":" << value.seed
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"population\":" << value.population
        << ",\"generations\":" << value.generations
        << ",\"physical_fes\":" << value.physical_fes
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex
        << value.scientific_hash << std::dec
        << "\",\"front\":[";
    for (std::size_t index = 0; index < value.front.size(); ++index) {
        if (index != 0U) output << ',';
        const auto& point = value.front[index];
        output << "{\"mean_power_mw\":" << point.mean_power_mw
            << ",\"intermittency_mw\":" << point.intermittency_mw
            << ",\"layout\":" << layout_json(point.layout) << '}';
    }
    output << "]}";
    return output.str();
}

void emit(const std::string& content, const std::string& path) {
    if (path.empty()) {
        std::cout << content << '\n';
        return;
    }
    std::ofstream stream(path);
    if (!stream) {
        throw std::runtime_error("cannot open T24 output " + path);
    }
    stream << content << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            emit(
                "{\"paper_case_ids\":[\"uniform_p0\","
                "\"uniform_p007\",\"uniform_p015\",\"real_p0\","
                "\"real_p007\",\"real_p015\"]}",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        const core99::t24::Problem problem(
            parse_case(arguments.case_id)
        );
        if (arguments.mode == "inspect") {
            std::ostringstream output;
            output << "{\"problem_id\":\"" << problem.id()
                << "\",\"turbine_count\":" << problem.turbine_count()
                << ",\"wind_state_count\":" << problem.wind_state_count()
                << ",\"threshold_fraction\":"
                << problem.threshold_fraction()
                << ",\"real_wind\":"
                << (problem.real_wind() ? "true" : "false")
                << ",\"paper_population\":"
                << problem.paper_population()
                << ",\"paper_reference_intervals\":"
                << problem.paper_reference_intervals()
                << ",\"paper_minimum_generations\":"
                << problem.paper_minimum_generations()
                << ",\"declared_maximum_generations\":"
                << problem.declared_maximum_generations()
                << ",\"declared_repeats\":"
                << problem.declared_repeats() << '}';
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "evaluate") {
            const auto layout = problem.reference_layout();
            std::ostringstream output;
            output << "{\"problem_id\":\"" << problem.id()
                << "\",\"evaluation\":"
                << evaluation_json(problem.evaluate(layout))
                << ",\"layout\":" << layout_json(layout) << '}';
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "model") {
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"upstream_y_over_d\":"
                << arguments.model_y_over_d
                << ",\"speed_mps\":" << arguments.model_speed_mps
                << ",\"direction_deg\":"
                << arguments.model_direction_deg
                << ",\"power_mw\":"
                << problem.model_problem_power_mw(
                    arguments.model_y_over_d,
                    arguments.model_speed_mps,
                    arguments.model_direction_deg
                ) << '}';
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument(
                "T24 mode must be list-cases, inspect, evaluate, model, "
                "or optimize"
            );
        }
        core99::t24::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.population = arguments.population < 0
            ? problem.paper_population() : arguments.population;
        config.generations = arguments.generations < 0
            ? problem.paper_minimum_generations()
            : arguments.generations;
        emit(
            run_json(core99::t24::run(problem, config)),
            arguments.output
        );
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T24 failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
