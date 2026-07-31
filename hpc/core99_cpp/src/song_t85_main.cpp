/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T85 pure-C++ CPU-HPC command-line and receipt writer
Paper/DOI: Particle Swarm Optimization of a Wind Farm Layout with Active
Control of Turbine Yaws; 10.1016/j.renene.2023.02.058
Public source, cited predecessor, missing assets, reconstruction completion,
semantic IDs, production backend, and claim boundary:
hpc/core99_cpp/include/core99/song_t85.hpp
Controlling contract: shared/contracts/core99_t85_song_2023.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/song_t85.hpp"

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
    std::string case_id = "wf1";
    std::string output;
    int workers = 20;
    int population = -1;
    std::uint64_t physical_fes_limit = 0;
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
        } else if (flag == "--physical-fes-limit") {
            result.physical_fes_limit = std::stoull(value());
        } else if (flag == "--seed") {
            result.seed = std::stoull(value());
        } else {
            throw std::invalid_argument("unknown T85 flag " + flag);
        }
    }
    return result;
}

core99::t85::CaseId parse_case(const std::string& value) {
    if (value == "wf1") return core99::t85::CaseId::wf1;
    if (value == "wf1_u6") return core99::t85::CaseId::wf1_u6;
    if (value == "wf1_v112") return core99::t85::CaseId::wf1_v112;
    if (value == "wf2") return core99::t85::CaseId::wf2;
    if (value == "wf3") return core99::t85::CaseId::wf3;
    if (value == "wf4") return core99::t85::CaseId::wf4;
    throw std::invalid_argument("unknown T85 case " + value);
}

std::string layout_json(
    const std::vector<core99::t85::TurbineDecision>& layout
) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t turbine = 0; turbine < layout.size(); ++turbine) {
        if (turbine != 0U) output << ',';
        output << "{\"x_m\":" << layout[turbine].x_m
            << ",\"y_m\":" << layout[turbine].y_m
            << ",\"yaw_deg\":[";
        for (std::size_t wind = 0;
             wind < layout[turbine].yaw_deg.size();
             ++wind) {
            if (wind != 0U) output << ',';
            output << layout[turbine].yaw_deg[wind];
        }
        output << "]}";
    }
    output << ']';
    return output.str();
}

std::string evaluation_json(const core99::t85::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"aep_gwh\":" << value.aep_gwh
        << ",\"wind_aep_contribution_gwh\":[";
    for (std::size_t wind = 0;
         wind < value.wind_aep_contribution_gwh.size();
         ++wind) {
        if (wind != 0U) output << ',';
        output << value.wind_aep_contribution_gwh[wind];
    }
    output << ']'
        << ",\"spacing_violation_m\":" << value.spacing_violation_m
        << ",\"boundary_violation_m\":" << value.boundary_violation_m
        << ",\"feasible\":" << (value.feasible ? "true" : "false")
        << '}';
    return output.str();
}

std::string run_json(const core99::t85::RunResult& value) {
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
        << ",\"physical_fes\":" << value.physical_fes
        << ",\"generations\":" << value.generations
        << ",\"final_subpopulation_size\":"
        << value.final_subpopulation_size
        << ",\"initial_best_aep_gwh\":"
        << value.initial_best_aep_gwh
        << ",\"best_aep_gwh\":" << value.best_aep_gwh
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex
        << value.scientific_hash << std::dec
        << "\",\"best_layout\":" << layout_json(value.best_layout)
        << '}';
    return output.str();
}

void emit(const std::string& content, const std::string& path) {
    if (path.empty()) {
        std::cout << content << '\n';
        return;
    }
    std::ofstream stream(path);
    if (!stream) {
        throw std::runtime_error("cannot open T85 output " + path);
    }
    stream << content << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            emit(
                "{\"paper_case_ids\":[\"wf1\",\"wf1_u6\","
                "\"wf1_v112\",\"wf2\",\"wf3\",\"wf4\"]}",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        const core99::t85::Problem problem(
            parse_case(arguments.case_id)
        );
        if (arguments.mode == "inspect") {
            std::ostringstream output;
            output << "{\"problem_id\":\"" << problem.id()
                << "\",\"turbine_count\":" << problem.turbine_count()
                << ",\"wind_state_count\":" << problem.wind_state_count()
                << ",\"decision_dimension\":"
                << problem.decision_dimension()
                << ",\"side_length_m\":" << problem.side_length_m()
                << ",\"rotor_diameter_m\":"
                << problem.rotor_diameter_m()
                << ",\"declared_population\":"
                << problem.declared_population()
                << ",\"declared_physical_fes\":"
                << problem.declared_physical_fes()
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
        if (arguments.mode != "optimize") {
            throw std::invalid_argument(
                "T85 mode must be list-cases, inspect, evaluate, or optimize"
            );
        }
        core99::t85::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.population = arguments.population < 0
            ? problem.declared_population() : arguments.population;
        config.physical_fes_limit =
            arguments.physical_fes_limit == 0
            ? problem.declared_physical_fes()
            : arguments.physical_fes_limit;
        emit(
            run_json(core99::t85::run(problem, config)),
            arguments.output
        );
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T85 failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
