/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0649 pure-C++ CPU-HPC CLI and JSON receipts
Paper/DOI: LoCascio et al.; 10.1002/WE.2954.
Public source: the paper-linked unlicensed author repository and its exact
paper-era revision are declared in include/core99/locascio_l0649.hpp.
Missing and Reconstruction: SNOPT and trajectory assets are replaced by the
declared projected-L-BFGS completion in that header.
Semantic IDs: l0649_flowers_aep_analytic_gradient_projected_lbfgs_v1,
l0649_wr7_nine_turbine_14d_square_v1 and
l0649_native_single_optimization_plus_n500_h6_v1.
Claim boundary: source-oracled flexible academic reproduction; full boundary
is recorded in include/core99/locascio_l0649.hpp.
Controlling contract: shared/contracts/core99_l0649_flowers_aep_2024.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/locascio_l0649.hpp"

#include "fode/executor.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string action = "optimize";
    std::string output;
    int workers = 20;
    int turbines = 500;
    int maximum_iterations = 200;
    double tolerance = 1.0e-3;
    bool smoke = false;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument("L0649 missing " + flag);
            }
            return std::string(argv[index]);
        };
        if (flag == "--action") result.action = value();
        else if (flag == "--output") result.output = value();
        else if (flag == "--workers") result.workers = std::stoi(value());
        else if (flag == "--turbines") result.turbines = std::stoi(value());
        else if (flag == "--iterations") {
            result.maximum_iterations = std::stoi(value());
        } else if (flag == "--tolerance") {
            result.tolerance = std::stod(value());
        } else if (flag == "--smoke") result.smoke = true;
        else throw std::invalid_argument("L0649 unknown flag " + flag);
    }
    return result;
}

std::string evaluation_json(const core99::l0649::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"aep_wh\":" << value.aep_wh
        << ",\"turbines\":" << value.turbines
        << ",\"fourier_modes\":" << value.fourier_modes
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"ordered_pair_terms\":" << value.ordered_pair_terms
        << ",\"seconds\":" << value.seconds << '}';
    return out.str();
}

std::string points_json(const std::vector<core99::l0649::Point>& points) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (index) out << ',';
        out << "{\"x_m\":" << points[index].x_m
            << ",\"y_m\":" << points[index].y_m << '}';
    }
    out << ']';
    return out.str();
}

std::string run_json(const core99::l0649::RunResult& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"schema_version\":1,\"corpus_id\":\"L0649\","
        << "\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"protocol_semantic_id\":\"" << value.protocol_semantic_id
        << "\",\"paper_role\":\"wr7_nine_turbine_flowers_opt\""
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"iterations\":" << value.iterations
        << ",\"objective_gradient_calls\":"
        << value.objective_gradient_calls
        << ",\"initial_evaluation\":"
        << evaluation_json(value.initial_evaluation)
        << ",\"final_evaluation\":"
        << evaluation_json(value.final_evaluation)
        << ",\"final_layout\":" << points_json(value.final_layout)
        << ",\"history\":[";
    for (std::size_t index = 0; index < value.history.size(); ++index) {
        if (index) out << ',';
        const auto& row = value.history[index];
        out << "{\"iteration\":" << row.iteration
            << ",\"objective\":" << row.objective
            << ",\"aep_wh\":" << row.aep_wh
            << ",\"projected_gradient_inf\":"
            << row.projected_gradient_inf
            << ",\"accepted_step\":" << row.accepted_step << '}';
    }
    out << "],\"objective_gain_percent\":" << value.objective_gain_percent
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex << value.scientific_hash
        << std::dec << "\"}";
    return out.str();
}

void emit(const std::string& payload, const std::string& output) {
    if (output.empty()) {
        std::cout << payload << '\n';
        return;
    }
    std::ofstream stream(output);
    if (!stream) throw std::runtime_error("L0649 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.action == "list-roles") {
            emit("{\"protocol_semantic_id\":\"l0649_native_single_optimization_plus_n500_h6_v1\",\"native_roles\":[\"wr7_nine_turbine_flowers_opt\"],\"native_repeats\":1,\"h6_extension\":\"wr7_n500_m10_evaluation\"}", arguments.output);
            return EXIT_SUCCESS;
        }
        const core99::l0649::FlowersModel model(arguments.workers, 10);
        if (arguments.action == "evaluate-scale") {
            fode::PersistentExecutor executor(arguments.workers);
            const auto layout = core99::l0649::make_paper_scale_layout(
                arguments.turbines);
            const auto evaluation = model.evaluate(layout, true, executor);
            std::ostringstream out;
            out << "{\"schema_version\":1,\"corpus_id\":\"L0649\","
                << "\"problem_semantic_id\":\"l0649_wr7_n500_m10_h6_extension_v1\","
                << "\"method_semantic_id\":\"l0649_flowers_aep_analytic_gradient_projected_lbfgs_v1\","
                << "\"evaluation\":" << evaluation_json(evaluation) << '}';
            emit(out.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action != "optimize") {
            throw std::invalid_argument(
                "L0649 action list-roles/evaluate-scale/optimize");
        }
        core99::l0649::RunConfig config;
        config.workers = arguments.workers;
        config.maximum_iterations = arguments.maximum_iterations;
        config.optimality_tolerance = arguments.tolerance;
        config.smoke = arguments.smoke;
        emit(run_json(core99::l0649::run(model, config)), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
