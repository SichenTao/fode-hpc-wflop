/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0245 pure-C++ CPU-HPC command interface and receipts.
Paper/DOI, public assets, missing data, conflicts, reconstruction decisions,
semantic IDs, HPC design and claim boundary:
hpc/core99_cpp/include/core99/padron_l0245.hpp.
Controlling contract: shared/contracts/core99_l0245_padron_2019.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/padron_l0245.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string action = "evaluate";
    std::string data;
    std::string output;
    std::string layout = "amalia";
    std::string method = "pcr_coarse";
    std::uint64_t seed = 2019024501ULL;
    int workers = 20;
    int repeats = 1;
    int evaluations = 1000;
    double maximum_seconds = 0.0;
    bool gradient = false;
    bool smoke = false;
    bool reference = false;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument("L0245 missing " + flag);
            }
            return std::string(argv[index]);
        };
        if (flag == "--action") result.action = value();
        else if (flag == "--data") result.data = value();
        else if (flag == "--output") result.output = value();
        else if (flag == "--layout") result.layout = value();
        else if (flag == "--method") result.method = value();
        else if (flag == "--seed") result.seed = std::stoull(value());
        else if (flag == "--workers") result.workers = std::stoi(value());
        else if (flag == "--repeats") result.repeats = std::stoi(value());
        else if (flag == "--evaluations") {
            result.evaluations = std::stoi(value());
        } else if (flag == "--maximum-seconds") {
            result.maximum_seconds = std::stod(value());
        } else if (flag == "--gradient") result.gradient = true;
        else if (flag == "--smoke") result.smoke = true;
        else if (flag == "--reference") result.reference = true;
        else throw std::invalid_argument("L0245 unknown flag " + flag);
    }
    if (result.action != "list-roles" && result.data.empty()) {
        throw std::invalid_argument("L0245 --data is required");
    }
    return result;
}

void emit(const std::string& payload, const std::string& output) {
    if (output.empty()) {
        std::cout << payload << '\n';
        return;
    }
    std::ofstream stream(output);
    if (!stream) throw std::runtime_error("L0245 cannot write output");
    stream << payload << '\n';
}

std::string evaluation_json(const core99::l0245::Evaluation& result) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"aep_gwh\":" << result.aep_gwh
        << ",\"expected_power_mw\":" << result.expected_power_mw
        << ",\"minimum_spacing_margin_m\":"
        << result.minimum_spacing_margin_m
        << ",\"maximum_boundary_violation_m\":"
        << result.maximum_boundary_violation_m
        << ",\"feasible\":" << (result.feasible ? "true" : "false")
        << ",\"selected_polynomial_degree\":"
        << result.selected_polynomial_degree
        << ",\"physical_wake_simulations\":"
        << result.physical_wake_simulations
        << ",\"requested_workers\":" << result.requested_workers
        << ",\"observed_workers\":" << result.observed_workers
        << ",\"scenario_seconds\":" << result.scenario_seconds
        << ",\"regression_seconds\":" << result.regression_seconds
        << ",\"gradient_gwh_per_m\":[";
    for (std::size_t index = 0; index < result.gradient_gwh_per_m.size(); ++index) {
        if (index) out << ',';
        out << result.gradient_gwh_per_m[index];
    }
    out << "]}";
    return out.str();
}

std::string layout_json(const std::vector<core99::l0245::Point>& layout) {
    std::ostringstream out;
    out << '[' << std::setprecision(17);
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index) out << ',';
        out << '[' << layout[index].x_m << ',' << layout[index].y_m << ']';
    }
    out << ']';
    return out.str();
}

std::string run_json(const core99::l0245::RunResult& result) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"schema_version\":1,\"corpus_id\":\"L0245\""
        << ",\"problem_semantic_id\":\"" << result.problem_semantic_id
        << "\",\"method_semantic_id\":\"" << result.method_semantic_id
        << "\",\"protocol_semantic_id\":\"" << result.protocol_semantic_id
        << "\",\"starting_layout\":\"" << result.starting_layout
        << "\",\"method\":\"" << result.method
        << "\",\"seed\":" << result.seed
        << ",\"requested_workers\":" << result.requested_workers
        << ",\"observed_workers\":" << result.observed_workers
        << ",\"objective_calls\":" << result.objective_calls
        << ",\"gradient_calls\":" << result.gradient_calls
        << ",\"optimizer_status\":" << result.optimizer_status
        << ",\"optimizer_status_name\":\"" << result.optimizer_status_name
        << "\",\"initial_evaluation\":"
        << evaluation_json(result.initial_evaluation)
        << ",\"final_evaluation\":"
        << evaluation_json(result.final_evaluation)
        << ",\"reference_seed\":" << result.reference_seed
        << ",\"reference_evaluation\":"
        << evaluation_json(result.reference_evaluation)
        << ",\"best_history_gwh\":[";
    for (std::size_t index = 0; index < result.best_history_gwh.size(); ++index) {
        if (index) out << ',';
        out << result.best_history_gwh[index];
    }
    out << "]"
        << ",\"final_layout_m\":" << layout_json(result.final_layout)
        << ",\"evaluator_seconds\":" << result.evaluator_seconds
        << ",\"regression_seconds\":" << result.regression_seconds
        << ",\"optimizer_seconds\":" << result.optimizer_seconds
        << ",\"end_to_end_seconds\":" << result.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex
        << result.scientific_hash << std::dec << "\"}";
    return out.str();
}

std::string profile_json(const core99::l0245::ProfileReceipt& result) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"schema_version\":1,\"corpus_id\":\"L0245\""
        << ",\"method_semantic_id\":"
        << "\"l0245_pcr_cv_gradient_slsqp_declared_v1\""
        << ",\"problem_semantic_id\":"
        << "\"l0245_amalia60_two_uncertainty_floris_declared_v1\""
        << ",\"method\":\"" << result.method
        << "\",\"layout\":\"" << result.layout
        << "\",\"repeats\":" << result.repeats
        << ",\"requested_workers\":" << result.requested_workers
        << ",\"observed_workers\":" << result.observed_workers
        << ",\"physical_wake_simulations\":"
        << result.physical_wake_simulations
        << ",\"aep_checksum_gwh\":" << result.aep_checksum_gwh
        << ",\"seconds\":" << result.seconds
        << ",\"scientific_hash\":\"" << std::hex
        << result.scientific_hash << std::dec << "\"}";
    return out.str();
}

std::string direct_evaluation_json(
    const core99::l0245::Evaluation& result,
    const std::string& method,
    const std::string& layout,
    const std::uint64_t seed
) {
    std::ostringstream out;
    out << "{\"schema_version\":1,\"corpus_id\":\"L0245\""
        << ",\"method_semantic_id\":"
        << "\"l0245_pcr_cv_gradient_slsqp_declared_v1\""
        << ",\"problem_semantic_id\":"
        << "\"l0245_amalia60_two_uncertainty_floris_declared_v1\""
        << ",\"method\":\"" << method
        << "\",\"layout\":\"" << layout
        << "\",\"seed\":" << seed
        << ",\"evaluation\":" << evaluation_json(result) << '}';
    return out.str();
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.action == "list-roles") {
            emit(
                "{\"protocol_semantic_id\":"
                "\"l0245_four_layout_convergence_three_start_10set_v1\","
                "\"layouts\":[\"grid\",\"amalia\",\"optimized\",\"random\"],"
                "\"optimization_starts\":[\"amalia\",\"grid\",\"random\"],"
                "\"methods\":[\"pcr_coarse\",\"pcr_fine\","
                "\"rectangle_coarse\",\"rectangle_fine\"],"
                "\"reference_method\":\"monte_carlo_reference\","
                "\"sample_counts\":[231,630,225,625,200000],"
                "\"sample_sets\":10,\"required_optimization_runs\":120}",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        core99::l0245::Problem problem(arguments.data);
        const auto layout = core99::l0245::parse_layout(arguments.layout);
        const auto method = core99::l0245::parse_method(arguments.method);
        if (arguments.action == "evaluate") {
            emit(direct_evaluation_json(
                problem.evaluate(
                    problem.layout(layout), method, arguments.seed,
                    arguments.workers, arguments.gradient
                ), arguments.method, arguments.layout, arguments.seed
            ), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action == "profile") {
            emit(profile_json(problem.profile(
                layout, method, arguments.seed, arguments.workers,
                arguments.repeats
            )), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action == "optimize") {
            core99::l0245::RunConfig config;
            config.starting_layout = layout;
            config.method = method;
            config.seed = arguments.seed;
            config.workers = arguments.workers;
            config.maximum_evaluations = arguments.evaluations;
            config.maximum_seconds = arguments.maximum_seconds;
            config.smoke = arguments.smoke;
            config.evaluate_monte_carlo_reference = arguments.reference;
            emit(run_json(problem.optimize(config)), arguments.output);
            return EXIT_SUCCESS;
        }
        throw std::invalid_argument(
            "L0245 action list-roles/evaluate/profile/optimize"
        );
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
