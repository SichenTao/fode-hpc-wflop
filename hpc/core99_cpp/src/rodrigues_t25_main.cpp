/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T25 pure-C++ CPU-HPC command line and JSON receipts
Paper/DOI: Rodrigues et al. 2024; 10.5194/wes-9-321-2024
Public assets, missing fields, conflict resolutions, semantic IDs and claim
boundary: include/core99/rodrigues_t25.hpp
Controlling contract: shared/contracts/core99_t25_rodrigues_2024.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/rodrigues_t25.hpp"

#include <algorithm>
#include <cmath>
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
    std::string action = "evaluate";
    core99::t25::ProblemConfig problem;
    core99::t25::OptimizationConfig optimization;
    core99::t25::GradientMode gradient = core99::t25::GradientMode::exact_reverse;
    std::filesystem::path output;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) throw std::invalid_argument("T25 missing " + key);
            return std::string(argv[index]);
        };
        if (key == "--action") result.action = value();
        else if (key == "--family") {
            const std::string item = value();
            if (item == "iea37") {
                result.problem.family = core99::t25::ProblemFamily::iea37;
            } else if (item == "horns_rev") {
                result.problem.family = core99::t25::ProblemFamily::horns_rev;
            } else throw std::invalid_argument("T25 family iea37/horns_rev");
        } else if (key == "--turbines") {
            result.problem.turbine_count = std::stoi(value());
        } else if (key == "--directions") {
            result.problem.direction_count = std::stoi(value());
        } else if (key == "--speeds") {
            result.problem.speed_count = std::stoi(value());
        } else if (key == "--workers") {
            result.optimization.workers = std::stoi(value());
        } else if (key == "--seed") {
            result.optimization.seed = std::stoull(value());
        } else if (key == "--start-index") {
            result.optimization.start_index = std::stoi(value());
        } else if (key == "--random-percent") {
            result.optimization.random_percent = std::stoi(value());
        } else if (key == "--grid-r") {
            result.optimization.grid_resolution_rotor_radii = std::stod(value());
        } else if (key == "--max-evaluations") {
            result.optimization.maximum_evaluations = std::stoi(value());
        } else if (key == "--xtol-rel") {
            result.optimization.relative_x_tolerance = std::stod(value());
        } else if (key == "--initialization") {
            const std::string item = value();
            if (item == "smast") result.optimization.use_smart_start = true;
            else if (item == "random") result.optimization.use_smart_start = false;
            else throw std::invalid_argument("T25 initialization smast/random");
        } else if (key == "--gradient") {
            const std::string item = value();
            if (item == "none") result.gradient = core99::t25::GradientMode::none;
            else if (item == "exact_reverse") {
                result.gradient = core99::t25::GradientMode::exact_reverse;
            } else if (item == "central_fd") {
                result.gradient = core99::t25::GradientMode::central_finite_difference;
            } else throw std::invalid_argument("T25 gradient mode unknown");
        } else if (key == "--output") result.output = value();
        else throw std::invalid_argument("T25 unknown option " + key);
    }
    return result;
}

std::string evaluation_json(
    const core99::t25::Problem& problem,
    const core99::t25::Evaluation& value,
    const core99::t25::GradientMode gradient
) {
    double maximum_gradient = 0.0;
    for (const double item : value.gradient_gwh_per_m) {
        maximum_gradient = std::max(maximum_gradient, std::abs(item));
    }
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"problem_semantic_id\":\"" << problem.semantic_id()
        << "\",\"method_semantic_id\":\"t25_smast_slsqp_exact_reverse_v1"
        << "\",\"family\":\"" << core99::t25::family_name(problem.config().family)
        << "\",\"turbine_count\":" << problem.config().turbine_count
        << ",\"direction_count\":" << problem.config().direction_count
        << ",\"speed_count\":" << problem.config().speed_count
        << ",\"gradient_mode\":\"" << core99::t25::gradient_name(gradient)
        << "\",\"aep_gwh\":" << value.aep_gwh
        << ",\"maximum_abs_gradient_gwh_per_m\":" << maximum_gradient
        << ",\"physical_layout_evaluations\":" << value.physical_layout_evaluations
        << ",\"flow_cases\":" << value.flow_cases
        << ",\"pair_interactions\":" << value.pair_interactions
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"seconds\":" << value.seconds << '}';
    return out.str();
}

std::string smart_start_json(
    const core99::t25::Problem& problem,
    const core99::t25::SmartStartReceipt& value
) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"problem_semantic_id\":\"" << problem.semantic_id()
        << "\",\"method_semantic_id\":\"t25_smast_slsqp_exact_reverse_v1"
        << "\",\"turbine_count\":" << value.layout.size()
        << ",\"aep_gwh\":" << value.aep_gwh
        << ",\"minimum_spacing_m\":" << value.minimum_spacing_m
        << ",\"grid_points_initial\":" << value.grid_points_initial
        << ",\"grid_points_remaining\":" << value.grid_points_remaining
        << ",\"random_percent\":" << value.random_percent
        << ",\"grid_resolution_rotor_radii\":"
        << value.grid_resolution_rotor_radii
        << ",\"candidate_flow_updates\":" << value.candidate_flow_updates
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"seconds\":" << value.seconds << '}';
    return out.str();
}

std::string optimization_json(const core99::t25::OptimizationReceipt& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"turbine_count\":" << value.turbine_count
        << ",\"direction_count\":" << value.direction_count
        << ",\"speed_count\":" << value.speed_count
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"seed\":" << value.seed
        << ",\"start_index\":" << value.start_index
        << ",\"random_percent\":" << value.random_percent
        << ",\"optimizer_status\":" << value.optimizer_status
        << ",\"optimizer_status_name\":\"" << value.optimizer_status_name
        << "\",\"objective_calls\":" << value.objective_calls
        << ",\"gradient_calls\":" << value.gradient_calls
        << ",\"constraint_calls\":" << value.constraint_calls
        << ",\"physical_layout_evaluations\":" << value.physical_layout_evaluations
        << ",\"initial_aep_gwh\":" << value.initial_aep_gwh
        << ",\"final_aep_gwh\":" << value.final_aep_gwh
        << ",\"minimum_spacing_m\":" << value.minimum_spacing_m
        << ",\"maximum_boundary_violation_m\":"
        << value.maximum_boundary_violation_m
        << ",\"initialization_seconds\":" << value.initialization_seconds
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
        << ",\"optimizer_seconds\":" << value.optimizer_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex << value.scientific_hash
        << std::dec << "\"}";
    return out.str();
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
    if (!stream) throw std::runtime_error("T25 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.action == "describe") {
            emit(
                "{\"paper_role_count\":65,\"platform_executable_role_count\":55,"
                "\"observation_only_role_count\":10,\"problem_families\":[\"iea37\","
                "\"horns_rev\"],\"iea37_turbines\":[16,36,64,130,279,566],"
                "\"horns_rev_turbines\":[100,200,300,400,500],"
                "\"target_mechanisms\":[\"exact_reverse_gradient\","
                "\"finite_difference\",\"flow_case_parallelization\","
                "\"outer_multistart_parallelization\",\"incremental_smast\","
                "\"slsqp\"]}",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        const core99::t25::Problem problem(arguments.problem);
        if (arguments.action == "evaluate") {
            fode::PersistentExecutor executor(arguments.optimization.workers);
            emit(
                evaluation_json(
                    problem,
                    problem.evaluate(
                        problem.reference_layout(), arguments.gradient, executor
                    ),
                    arguments.gradient
                ),
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        if (arguments.action == "smart-start") {
            fode::PersistentExecutor executor(arguments.optimization.workers);
            emit(
                smart_start_json(
                    problem,
                    problem.smart_start(
                        arguments.optimization.random_percent,
                        arguments.optimization.grid_resolution_rotor_radii,
                        arguments.optimization.seed,
                        arguments.optimization.start_index,
                        executor
                    )
                ),
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        if (arguments.action == "optimize") {
            emit(
                optimization_json(core99::t25::optimize(
                    problem, arguments.optimization
                )),
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        throw std::invalid_argument("T25 action describe/evaluate/smart-start/optimize");
    } catch (const std::exception& error) {
        std::cerr << "T25 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
