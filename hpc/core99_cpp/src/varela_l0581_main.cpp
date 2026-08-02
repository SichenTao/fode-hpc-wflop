/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0581 pure-C++ CPU-HPC command interface and receipts.
Paper/DOI, public source, missing assets, conflicts, reconstruction,
semantic IDs, HPC design and claim boundary:
hpc/core99_cpp/include/core99/varela_l0581.hpp.
Controlling contract: shared/contracts/core99_l0581_sparse_gradient_2023.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/varela_l0581.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string action = "accuracy";
    std::string mode = "sparse";
    std::string output;
    int turbines = 95;
    int workers = 20;
    int iterations = 24;
    double threshold = 1.0e-12;
    double direction = 270.0;
    std::uint64_t seed = 2023058101ULL;
    bool smoke = false;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument("L0581 missing " + flag);
            }
            return std::string(argv[index]);
        };
        if (flag == "--action") result.action = value();
        else if (flag == "--mode") result.mode = value();
        else if (flag == "--output") result.output = value();
        else if (flag == "--turbines") result.turbines = std::stoi(value());
        else if (flag == "--workers") result.workers = std::stoi(value());
        else if (flag == "--iterations") result.iterations = std::stoi(value());
        else if (flag == "--threshold") result.threshold = std::stod(value());
        else if (flag == "--direction") result.direction = std::stod(value());
        else if (flag == "--seed") result.seed = std::stoull(value());
        else if (flag == "--smoke") result.smoke = true;
        else throw std::invalid_argument("L0581 unknown flag " + flag);
    }
    return result;
}

core99::l0581::GradientMode parse_mode(const std::string& value) {
    if (value == "dense") return core99::l0581::GradientMode::dense;
    if (value == "sparse") return core99::l0581::GradientMode::sparse;
    throw std::invalid_argument("L0581 mode must be dense or sparse");
}

void emit(const std::string& payload, const std::string& output) {
    if (output.empty()) {
        std::cout << payload << '\n';
        return;
    }
    std::ofstream stream(output);
    if (!stream) throw std::runtime_error("L0581 cannot write output");
    stream << payload << '\n';
}

std::string layout_json(const core99::l0581::Layout& layout) {
    std::ostringstream out;
    out << '[' << std::setprecision(17);
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index) out << ',';
        out << '[' << layout[index].x_d << ',' << layout[index].y_d << ']';
    }
    out << ']';
    return out.str();
}

std::string gradient_json(const core99::l0581::GradientResult& result) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"schema_version\":1,\"corpus_id\":\"L0581\","
        << "\"method_semantic_id\":"
        << "\"l0581_adaptive_sparse_colored_forward_ad_v1\","
        << "\"problem_semantic_id\":"
        << "\"l0581_round_n38_n349_single_direction_v1\","
        << "\"turbines\":" << result.turbines
        << ",\"variables\":" << result.variables
        << ",\"colors\":" << result.colors
        << ",\"dual_sweeps\":" << result.dual_sweeps
        << ",\"requested_workers\":" << result.requested_workers
        << ",\"observed_workers\":" << result.observed_workers
        << ",\"threshold\":" << result.threshold
        << ",\"normalized_aep\":" << result.normalized_aep
        << ",\"seconds\":" << result.seconds
        << ",\"gradient\":[";
    for (std::size_t index = 0; index < result.gradient.size(); ++index) {
        if (index) out << ',';
        out << result.gradient[index];
    }
    out << "],\"scientific_hash\":\"" << std::hex
        << result.scientific_hash << std::dec << "\"}";
    return out.str();
}

std::string accuracy_json(const core99::l0581::AccuracyResult& result) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"schema_version\":1,\"corpus_id\":\"L0581\","
        << "\"method_semantic_id\":"
        << "\"l0581_adaptive_sparse_colored_forward_ad_v1\","
        << "\"problem_semantic_id\":"
        << "\"l0581_round_n38_n349_single_direction_v1\","
        << "\"turbines\":" << result.turbines
        << ",\"threshold\":" << result.threshold
        << ",\"dense_colors\":" << result.dense_colors
        << ",\"sparse_colors\":" << result.sparse_colors
        << ",\"requested_workers\":" << result.requested_workers
        << ",\"observed_workers\":" << result.observed_workers
        << ",\"color_fraction\":" << result.color_fraction
        << ",\"maximum_scaled_error\":" << result.maximum_scaled_error
        << ",\"dense_seconds\":" << result.dense_seconds
        << ",\"sparse_seconds\":" << result.sparse_seconds
        << ",\"scientific_hash\":\"" << std::hex
        << result.scientific_hash << std::dec << "\"}";
    return out.str();
}

std::string optimization_json(const core99::l0581::OptimizationResult& result) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"schema_version\":1,\"corpus_id\":\"L0581\","
        << "\"method_semantic_id\":\"" << result.method_semantic_id
        << "\",\"problem_semantic_id\":\"" << result.problem_semantic_id
        << "\",\"protocol_semantic_id\":\"" << result.protocol_semantic_id
        << "\",\"mode\":\"" << core99::l0581::gradient_mode_name(result.mode)
        << "\",\"seed\":" << result.seed
        << ",\"requested_workers\":" << result.requested_workers
        << ",\"observed_workers\":" << result.observed_workers
        << ",\"iterations\":" << result.iterations
        << ",\"pattern_rebuilds\":" << result.pattern_rebuilds
        << ",\"final_colors\":" << result.final_colors
        << ",\"final_threshold\":" << result.final_threshold
        << ",\"initial_wake_loss_percent\":"
        << result.initial_wake_loss_percent
        << ",\"final_wake_loss_percent\":" << result.final_wake_loss_percent
        << ",\"wake_loss_reduction_points\":"
        << result.wake_loss_reduction_points
        << ",\"gradient_seconds\":" << result.gradient_seconds
        << ",\"algorithm_seconds\":" << result.algorithm_seconds
        << ",\"end_to_end_seconds\":" << result.end_to_end_seconds
        << ",\"best_history\":[";
    for (std::size_t index = 0; index < result.best_history.size(); ++index) {
        if (index) out << ',';
        out << result.best_history[index];
    }
    out << "],\"final_layout_d\":" << layout_json(result.final_layout)
        << ",\"scientific_hash\":\"" << std::hex
        << result.scientific_hash << std::dec << "\"}";
    return out.str();
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.action == "list-roles") {
            std::ostringstream out;
            out << "{\"protocol_semantic_id\":"
                << "\"l0581_accuracy_8_sizes_plus_10_paired_starts_v1\","
                << "\"accuracy_sizes\":[";
            const auto sizes = core99::l0581::paper_accuracy_sizes();
            for (std::size_t index = 0; index < sizes.size(); ++index) {
                if (index) out << ',';
                out << sizes[index];
            }
            out << "],\"optimization_turbines\":95,"
                << "\"optimization_wind_states\":12,"
                << "\"paired_random_starts\":10,"
                << "\"required_optimization_runs\":20}";
            emit(out.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action == "describe") {
            const auto spec = core99::l0581::farm_spec(arguments.turbines);
            std::ostringstream out;
            out << std::setprecision(17)
                << "{\"turbines\":" << spec.turbines
                << ",\"rings\":" << spec.rings
                << ",\"boundary_radius_d\":" << spec.boundary_radius_d
                << ",\"actual_minimum_initial_spacing_d\":"
                << spec.actual_minimum_initial_spacing_d << '}';
            emit(out.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action == "gradient") {
            const auto result = core99::l0581::calculate_gradient(
                core99::l0581::round_layout(arguments.turbines),
                arguments.direction, parse_mode(arguments.mode),
                arguments.threshold, arguments.workers
            );
            emit(gradient_json(result), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action == "accuracy") {
            emit(accuracy_json(core99::l0581::compare_accuracy(
                arguments.turbines, arguments.threshold, arguments.workers
            )), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action == "optimize") {
            core99::l0581::OptimizationConfig config;
            config.mode = parse_mode(arguments.mode);
            config.seed = arguments.seed;
            config.workers = arguments.workers;
            config.maximum_iterations = arguments.iterations;
            config.smoke = arguments.smoke;
            emit(optimization_json(core99::l0581::optimize(config)),
                 arguments.output);
            return EXIT_SUCCESS;
        }
        throw std::invalid_argument(
            "L0581 action list-roles/describe/gradient/accuracy/optimize"
        );
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
