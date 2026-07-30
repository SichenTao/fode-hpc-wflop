/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T16 command-line driver and machine-readable receipts
Paper/DOI: Comparison of Wind Farm Layout Optimization Results Using a
Simple Wake Model and Gradient-Based Optimization to Large Eddy Simulations;
10.2514/6.2019-0538
Public source, missing facts, conflict resolutions, semantic IDs, backend and
claim boundary: include/core99/thomas_t16.hpp
Controlling contract: shared/contracts/core99_t16_thomas_2019.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/thomas_t16.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string mode = "optimize";
    std::string data;
    std::string output;
    int workers = 20;
    int start_index = 0;
    int maximum_evaluations_per_stage = 220;
    std::uint64_t seed = 20260731;
    bool smoke_lifecycle = false;
    bool calculate_gradient = false;
    int rotor_points = 1;
    core99::t16::TurbulenceMode turbulence =
        core99::t16::TurbulenceMode::ambient_only;
    double wec_factor = 1.0;
};

Arguments parse(const int argc, char** argv) {
    Arguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument("missing value for " + flag);
            }
            return std::string(argv[index]);
        };
        if (flag == "--mode") arguments.mode = value();
        else if (flag == "--data") arguments.data = value();
        else if (flag == "--output") arguments.output = value();
        else if (flag == "--workers") arguments.workers = std::stoi(value());
        else if (flag == "--start-index") {
            arguments.start_index = std::stoi(value());
        } else if (flag == "--maxeval-per-stage") {
            arguments.maximum_evaluations_per_stage = std::stoi(value());
        } else if (flag == "--seed") {
            arguments.seed = std::stoull(value());
        } else if (flag == "--wec-factor") {
            arguments.wec_factor = std::stod(value());
        } else if (flag == "--rotor-points") {
            arguments.rotor_points = std::stoi(value());
        } else if (flag == "--turbulence") {
            const std::string mode = value();
            if (mode == "ambient") {
                arguments.turbulence =
                    core99::t16::TurbulenceMode::ambient_only;
            } else if (mode == "smooth") {
                arguments.turbulence =
                    core99::t16::TurbulenceMode::smooth_local;
            } else if (mode == "hard") {
                arguments.turbulence =
                    core99::t16::TurbulenceMode::hard_local;
            } else {
                throw std::invalid_argument("unknown turbulence mode");
            }
        } else if (flag == "--gradient") {
            arguments.calculate_gradient = true;
        } else if (flag == "--smoke-lifecycle") {
            arguments.smoke_lifecycle = true;
        } else {
            throw std::invalid_argument("unknown T16 flag: " + flag);
        }
    }
    if (arguments.data.empty()) {
        throw std::invalid_argument("--data is required");
    }
    return arguments;
}

std::string turbulence_name(const core99::t16::TurbulenceMode mode) {
    if (mode == core99::t16::TurbulenceMode::ambient_only) return "ambient";
    if (mode == core99::t16::TurbulenceMode::smooth_local) return "smooth";
    return "hard";
}

std::string evaluation_json(const core99::t16::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"aep_gwh\":" << value.aep_gwh
        << ",\"maximum_constraint_violation_m\":"
        << value.maximum_constraint_violation_m
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"seconds\":" << value.seconds
        << ",\"directional_power_mw\":[";
    for (std::size_t index = 0;
         index < value.directional_power_mw.size();
         ++index) {
        if (index != 0U) output << ',';
        output << value.directional_power_mw[index];
    }
    output << ']';
    if (!value.gradient_gwh_per_m.empty()) {
        output << ",\"gradient_gwh_per_m\":[";
        for (std::size_t index = 0;
             index < value.gradient_gwh_per_m.size();
             ++index) {
            if (index != 0U) output << ',';
            output << value.gradient_gwh_per_m[index];
        }
        output << ']';
    }
    output << '}';
    return output.str();
}

std::string layout_json(const std::vector<core99::t16::Point>& layout) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index != 0U) output << ',';
        output << '[' << layout[index].x_m << ',' << layout[index].y_m << ']';
    }
    output << ']';
    return output.str();
}

void emit(const std::string& content, const std::string& path) {
    if (path.empty()) {
        std::cout << content;
        return;
    }
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("cannot open T16 output: " + path);
    stream << content;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        const core99::t16::Problem problem(arguments.data);
        if (arguments.mode == "evaluate") {
            fode::PersistentExecutor executor(arguments.workers);
            core99::t16::EvaluationSettings settings;
            settings.wec_factor = arguments.wec_factor;
            settings.turbulence_mode = arguments.turbulence;
            settings.rotor_sample_points = arguments.rotor_points;
            settings.calculate_gradient = arguments.calculate_gradient;
            const auto layout = problem.reconstructed_start(
                arguments.start_index, arguments.seed
            );
            const auto evaluation = problem.evaluate(
                layout, settings, executor
            );
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"mode\":\"evaluate\","
                << "\"problem_semantic_id\":\"" << problem.semantic_id()
                << "\",\"start_index\":" << arguments.start_index
                << ",\"wec_factor\":" << arguments.wec_factor
                << ",\"turbulence\":\""
                << turbulence_name(arguments.turbulence)
                << "\",\"rotor_points\":" << arguments.rotor_points
                << ",\"evaluation\":" << evaluation_json(evaluation)
                << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument("unknown T16 mode: " + arguments.mode);
        }
        core99::t16::RunConfig config;
        config.workers = arguments.workers;
        config.start_index = arguments.start_index;
        config.seed = arguments.seed;
        config.maximum_evaluations_per_stage =
            arguments.maximum_evaluations_per_stage;
        config.run_full_wec_lifecycle = !arguments.smoke_lifecycle;
        const core99::t16::RunResult result =
            core99::t16::run(problem, config);
        std::ostringstream output;
        output << std::setprecision(17)
            << "{\n"
            << "  \"mode\":\"optimization\",\n"
            << "  \"problem_semantic_id\":\""
            << result.problem_semantic_id << "\",\n"
            << "  \"method_semantic_id\":\""
            << result.method_semantic_id << "\",\n"
            << "  \"start_index\":" << result.start_index << ",\n"
            << "  \"seed\":" << result.seed << ",\n"
            << "  \"requested_workers\":" << result.requested_workers << ",\n"
            << "  \"observed_workers\":" << result.observed_workers << ",\n"
            << "  \"evaluator_seconds\":" << result.evaluator_seconds << ",\n"
            << "  \"optimizer_seconds\":" << result.optimizer_seconds << ",\n"
            << "  \"end_to_end_seconds\":" << result.end_to_end_seconds << ",\n"
            << "  \"initial_optimization_evaluation\":"
            << evaluation_json(result.initial_optimization_evaluation)
            << ",\n"
            << "  \"final_optimization_evaluation\":"
            << evaluation_json(result.final_optimization_evaluation)
            << ",\n"
            << "  \"final_paper_assessment\":"
            << evaluation_json(result.final_paper_assessment) << ",\n"
            << "  \"stages\":[\n";
        for (std::size_t index = 0; index < result.stages.size(); ++index) {
            const auto& stage = result.stages[index];
            output << "    {\"wec_factor\":" << stage.wec_factor
                << ",\"turbulence\":\""
                << turbulence_name(stage.turbulence_mode)
                << "\",\"objective_calls\":" << stage.objective_calls
                << ",\"gradient_calls\":" << stage.gradient_calls
                << ",\"constraint_calls\":" << stage.constraint_calls
                << ",\"optimizer_status\":" << stage.optimizer_status
                << ",\"optimizer_status_name\":\""
                << stage.optimizer_status_name
                << "\",\"start_aep_gwh\":" << stage.start_aep_gwh
                << ",\"end_aep_gwh\":" << stage.end_aep_gwh
                << ",\"seconds\":" << stage.seconds << '}';
            if (index + 1U != result.stages.size()) output << ',';
            output << '\n';
        }
        output << "  ],\n"
            << "  \"final_layout\":" << layout_json(result.final_layout)
            << ",\n"
            << "  \"scientific_hash\":\"" << std::hex
            << result.scientific_hash << std::dec << "\"\n"
            << "}\n";
        emit(output.str(), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T16 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
