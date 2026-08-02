/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T84 CLI and machine-readable experiment receipts
Paper/DOI: Wake Expansion Continuation: Multi-Modality Reduction in the Wind
Farm Layout Optimization Problem; 10.1002/we.2692
Public source, missing facts, conflicts and reconstruction resolution:
include/core99/thomas_t84.hpp; paper source commit 8ff27d66079591f25619a.
Semantic IDs: t84_wec_four_case_author_data_v1 and four t84_* method IDs.
Production backend: pure C++ CPU-HPC; this CLI writes complete machine-readable
algorithm, physics-work, timing, worker and scientific-hash receipts.
Controlling contract: shared/contracts/core99_t84_thomas_2022.json
Claim boundary: flexible academic reproduction, not author solver or numeric
trajectory replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/thomas_t84.hpp"

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
    int case_id = 1;
    int start_index = 0;
    int workers = 20;
    int maximum_slsqp_evaluations_per_stage = 220;
    std::uint64_t seed = 20260731;
    core99::t84::WakeModel wake = core99::t84::WakeModel::bastankhah;
    core99::t84::OptimizerFamily optimizer =
        core99::t84::OptimizerFamily::slsqp_open_snopt_replacement;
    bool use_wec = true;
    bool smoke = false;
    bool gradient = false;
    double wec_factor = 1.0;
    core99::t84::TurbulenceMode turbulence =
        core99::t84::TurbulenceMode::ambient_only;
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
        else if (flag == "--case") arguments.case_id = std::stoi(value());
        else if (flag == "--start-index") arguments.start_index = std::stoi(value());
        else if (flag == "--workers") arguments.workers = std::stoi(value());
        else if (flag == "--seed") arguments.seed = std::stoull(value());
        else if (flag == "--maxeval-per-stage") {
            arguments.maximum_slsqp_evaluations_per_stage = std::stoi(value());
        } else if (flag == "--wake") {
            const std::string wake = value();
            if (wake == "bastankhah") {
                arguments.wake = core99::t84::WakeModel::bastankhah;
            } else if (wake == "jensen") {
                arguments.wake = core99::t84::WakeModel::jensen_cosine;
            } else {
                throw std::invalid_argument("unknown T84 wake model");
            }
        } else if (flag == "--optimizer") {
            const std::string optimizer = value();
            if (optimizer == "slsqp") {
                arguments.optimizer =
                    core99::t84::OptimizerFamily::slsqp_open_snopt_replacement;
            } else if (optimizer == "alpso") {
                arguments.optimizer =
                    core99::t84::OptimizerFamily::augmented_lagrangian_pso;
            } else {
                throw std::invalid_argument("unknown T84 optimizer");
            }
        } else if (flag == "--wec") {
            arguments.use_wec = true;
        } else if (flag == "--no-wec") {
            arguments.use_wec = false;
        } else if (flag == "--smoke") {
            arguments.smoke = true;
        } else if (flag == "--gradient") {
            arguments.gradient = true;
        } else if (flag == "--wec-factor") {
            arguments.wec_factor = std::stod(value());
        } else if (flag == "--turbulence") {
            const std::string turbulence = value();
            if (turbulence == "ambient") {
                arguments.turbulence = core99::t84::TurbulenceMode::ambient_only;
            } else if (turbulence == "smooth") {
                arguments.turbulence = core99::t84::TurbulenceMode::smooth_local;
            } else if (turbulence == "hard") {
                arguments.turbulence = core99::t84::TurbulenceMode::hard_local;
            } else {
                throw std::invalid_argument("unknown T84 turbulence mode");
            }
        } else {
            throw std::invalid_argument("unknown T84 flag: " + flag);
        }
    }
    if (arguments.data.empty()) throw std::invalid_argument("--data is required");
    return arguments;
}

std::string evaluation_json(const core99::t84::Evaluation& evaluation) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"aep_gwh\":" << evaluation.aep_gwh
        << ",\"gross_aep_gwh\":" << evaluation.gross_aep_gwh
        << ",\"wake_loss_percent\":" << evaluation.wake_loss_percent
        << ",\"maximum_constraint_violation_m\":"
        << evaluation.maximum_constraint_violation_m
        << ",\"requested_workers\":" << evaluation.requested_workers
        << ",\"observed_workers\":" << evaluation.observed_workers
        << ",\"seconds\":" << evaluation.seconds
        << ",\"directional_power_mw\":[";
    for (std::size_t index = 0; index < evaluation.directional_power_mw.size(); ++index) {
        if (index != 0U) output << ',';
        output << evaluation.directional_power_mw[index];
    }
    output << ']';
    if (!evaluation.gradient_gwh_per_m.empty()) {
        output << ",\"gradient_gwh_per_m\":[";
        for (std::size_t index = 0; index < evaluation.gradient_gwh_per_m.size(); ++index) {
            if (index != 0U) output << ',';
            output << evaluation.gradient_gwh_per_m[index];
        }
        output << ']';
    }
    output << '}';
    return output.str();
}

std::string layout_json(const std::vector<core99::t84::Point>& layout) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index != 0U) output << ',';
        output << '[' << layout[index].x_m << ',' << layout[index].y_m << ']';
    }
    output << ']';
    return output.str();
}

void emit(const std::string& payload, const std::string& path) {
    if (path.empty()) {
        std::cout << payload;
        return;
    }
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("cannot open T84 output: " + path);
    stream << payload;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        const core99::t84::Problem problem(arguments.data, arguments.case_id);
        if (arguments.mode == "evaluate") {
            fode::PersistentExecutor executor(arguments.workers);
            core99::t84::EvaluationSettings settings;
            settings.wake_model = arguments.wake;
            settings.wec_factor = arguments.wec_factor;
            settings.turbulence_mode = arguments.turbulence;
            settings.calculate_gradient = arguments.gradient;
            const auto evaluation = problem.evaluate(
                problem.start(arguments.start_index), settings, executor
            );
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"mode\":\"evaluate\",\"problem_semantic_id\":\""
                << problem.semantic_id() << "\",\"case_id\":" << problem.case_id()
                << ",\"start_index\":" << arguments.start_index
                << ",\"wake_model\":\"" << core99::t84::wake_model_name(arguments.wake)
                << "\",\"wec_factor\":" << arguments.wec_factor
                << ",\"turbulence\":\""
                << core99::t84::turbulence_name(arguments.turbulence)
                << "\",\"evaluation\":" << evaluation_json(evaluation) << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument("unknown T84 mode: " + arguments.mode);
        }
        core99::t84::RunConfig config;
        config.workers = arguments.workers;
        config.start_index = arguments.start_index;
        config.seed = arguments.seed;
        config.wake_model = arguments.wake;
        config.optimizer = arguments.optimizer;
        config.use_wec = arguments.use_wec;
        config.smoke = arguments.smoke;
        config.maximum_slsqp_evaluations_per_stage =
            arguments.maximum_slsqp_evaluations_per_stage;
        const auto result = core99::t84::run(problem, config);
        std::ostringstream output;
        output << std::setprecision(17)
            << "{\n  \"mode\":\"optimization\",\n"
            << "  \"problem_semantic_id\":\"" << result.problem_semantic_id << "\",\n"
            << "  \"method_semantic_id\":\"" << result.method_semantic_id << "\",\n"
            << "  \"case_id\":" << result.case_id << ",\n"
            << "  \"start_index\":" << result.start_index << ",\n"
            << "  \"seed\":" << result.seed << ",\n"
            << "  \"wake_model\":\"" << core99::t84::wake_model_name(result.wake_model) << "\",\n"
            << "  \"optimizer\":\"" << core99::t84::optimizer_name(result.optimizer) << "\",\n"
            << "  \"use_wec\":" << (result.use_wec ? "true" : "false") << ",\n"
            << "  \"requested_workers\":" << result.requested_workers << ",\n"
            << "  \"observed_workers\":" << result.observed_workers << ",\n"
            << "  \"paper_function_call_budget\":" << result.paper_function_call_budget << ",\n"
            << "  \"executed_function_calls\":" << result.executed_function_calls << ",\n"
            << "  \"evaluator_seconds\":" << result.evaluator_seconds << ",\n"
            << "  \"optimizer_seconds\":" << result.optimizer_seconds << ",\n"
            << "  \"end_to_end_seconds\":" << result.end_to_end_seconds << ",\n"
            << "  \"initial_assessment\":" << evaluation_json(result.initial_assessment) << ",\n"
            << "  \"final_assessment\":" << evaluation_json(result.final_assessment) << ",\n"
            << "  \"stages\":[\n";
        for (std::size_t index = 0; index < result.stages.size(); ++index) {
            const auto& stage = result.stages[index];
            output << "    {\"wec_factor\":" << stage.wec_factor
                << ",\"turbulence\":\"" << core99::t84::turbulence_name(stage.turbulence_mode)
                << "\",\"objective_calls\":" << stage.objective_calls
                << ",\"gradient_calls\":" << stage.gradient_calls
                << ",\"population_evaluations\":" << stage.population_evaluations
                << ",\"optimizer_status\":" << stage.optimizer_status
                << ",\"optimizer_status_name\":\"" << stage.optimizer_status_name
                << "\",\"start_aep_gwh\":" << stage.start_aep_gwh
                << ",\"end_aep_gwh\":" << stage.end_aep_gwh
                << ",\"seconds\":" << stage.seconds << '}';
            if (index + 1U != result.stages.size()) output << ',';
            output << '\n';
        }
        output << "  ],\n  \"final_layout\":" << layout_json(result.final_layout)
            << ",\n  \"scientific_hash\":\"" << std::hex << result.scientific_hash
            << std::dec << "\"\n}\n";
        emit(output.str(), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T84 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
