/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T82 pure-C++ CPU-HPC command-line and receipt writer
Paper/DOI: Wind Farm Layout Optimization to Minimize the Wake-Induced
Turbulence Effect on Wind Turbines; 10.1016/j.apenergy.2022.119599
Public source, missing assets, conflict handling, reconstruction completion,
semantic IDs, production backend, and claim boundary:
hpc/core99_cpp/include/core99/cao_t82.hpp
Controlling contract: shared/contracts/core99_t82_cao_2022.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/cao_t82.hpp"

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
    std::string case_id = "ideal_i";
    std::string output;
    int workers = 20;
    int population = -1;
    int generations = -1;
    std::uint64_t seed = 20260731;
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
        } else {
            throw std::invalid_argument("unknown T82 flag " + flag);
        }
    }
    return result;
}

core99::t82::CaseId parse_case(const std::string& value) {
    if (value == "ideal_i") {
        return core99::t82::CaseId::ideal_single;
    }
    if (value == "ideal_ii") {
        return core99::t82::CaseId::ideal_multi;
    }
    if (value == "zhuanghe") {
        return core99::t82::CaseId::zhuanghe;
    }
    throw std::invalid_argument("unknown T82 case " + value);
}

std::string turbine_json(const core99::t82::Turbine& turbine) {
    std::ostringstream output;
    output << std::setprecision(17)
        << '[' << turbine.x_m << ',' << turbine.y_m
        << ',' << turbine.type << ']';
    return output.str();
}

std::string layout_json(
    const std::vector<core99::t82::Turbine>& layout
) {
    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index != 0U) output << ',';
        output << turbine_json(layout[index]);
    }
    output << ']';
    return output.str();
}

std::string evaluation_json(const core99::t82::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"expected_power_kw\":" << value.expected_power_kw
        << ",\"maximum_comprehensive_turbulence\":"
        << value.maximum_comprehensive_turbulence
        << ",\"spacing_violation_m\":" << value.spacing_violation_m
        << ",\"boundary_violation_m\":" << value.boundary_violation_m
        << ",\"feasible\":" << (value.feasible ? "true" : "false")
        << '}';
    return output.str();
}

std::string front_json(
    const std::vector<core99::t82::FrontPoint>& front
) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < front.size(); ++index) {
        if (index != 0U) output << ',';
        output << "{\"expected_power_kw\":"
            << front[index].expected_power_kw
            << ",\"maximum_comprehensive_turbulence\":"
            << front[index].maximum_comprehensive_turbulence
            << ",\"layout\":" << layout_json(front[index].layout)
            << '}';
    }
    output << ']';
    return output.str();
}

std::string run_json(const core99::t82::RunResult& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"problem_id\":\"" << value.problem_id
        << "\",\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
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
        << "\",\"front\":" << front_json(value.front) << '}';
    return output.str();
}

void emit(const std::string& content, const std::string& path) {
    if (path.empty()) {
        std::cout << content << '\n';
        return;
    }
    std::ofstream stream(path);
    if (!stream) {
        throw std::runtime_error("cannot open T82 output " + path);
    }
    stream << content << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            emit(
                "{\"paper_case_ids\":[\"ideal_i\",\"ideal_ii\","
                "\"zhuanghe\"]}",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        const core99::t82::Problem problem(
            parse_case(arguments.case_id)
        );
        if (arguments.mode == "inspect") {
            std::ostringstream output;
            output << "{\"problem_id\":\"" << problem.id()
                << "\",\"turbine_count\":" << problem.turbine_count()
                << ",\"wind_state_count\":" << problem.wind_state_count()
                << ",\"paper_population\":"
                << problem.paper_population()
                << ",\"paper_generations\":"
                << problem.paper_generations()
                << ",\"paper_repeats\":" << problem.paper_repeats()
                << '}';
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
        if (arguments.mode != "optimize") {
            throw std::invalid_argument(
                "T82 mode must be list-cases, inspect, evaluate, or optimize"
            );
        }
        core99::t82::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.population = arguments.population < 0
            ? problem.paper_population() : arguments.population;
        config.generations = arguments.generations < 0
            ? problem.paper_generations() : arguments.generations;
        emit(
            run_json(core99::t82::run(problem, config)),
            arguments.output
        );
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T82 failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
