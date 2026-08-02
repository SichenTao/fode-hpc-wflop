/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T63 command-line driver and machine-readable receipt
Paper/DOI: Wind Farm Layout Optimization on Complex Terrains - Integrating a
CFD Wake Model with Mixed-Integer Programming;
10.1016/j.apenergy.2016.06.085
Public source/missing/reconstruction: include/core99/kuo_t63.hpp
Semantic IDs: t63_carleton_figure_proxy_cfd_surrogate_v1;
t63_iterative_cfd_mip_highs_reconstruction_v1
Controlling contract: shared/contracts/core99_t63_kuo_2016.json
Claim boundary: academic declared proxy reproduction, not author CFD/Gurobi
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/kuo_t63.hpp"

#include <cmath>
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
    std::string proxy;
    std::string output;
    double relaxation = 0.2;
    int workers = 20;
    int maximum_iterations = 401;
    double mip_time_limit_seconds = 30.0;
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
        else if (flag == "--proxy") arguments.proxy = value();
        else if (flag == "--output") arguments.output = value();
        else if (flag == "--relaxation") arguments.relaxation = std::stod(value());
        else if (flag == "--workers") arguments.workers = std::stoi(value());
        else if (flag == "--maximum-iterations") {
            arguments.maximum_iterations = std::stoi(value());
        } else if (flag == "--mip-time-limit-seconds") {
            arguments.mip_time_limit_seconds = std::stod(value());
        } else {
            throw std::invalid_argument("unknown T63 flag: " + flag);
        }
    }
    if (arguments.proxy.empty()) {
        throw std::invalid_argument("--proxy is required");
    }
    return arguments;
}

std::string integer_array_json(const std::vector<int>& values) {
    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) output << ',';
        output << values[index];
    }
    output << ']';
    return output.str();
}

std::string number_json(const double value) {
    if (!std::isfinite(value)) return "null";
    std::ostringstream output;
    output << std::setprecision(17) << value;
    return output.str();
}

std::string history_json(
    const std::vector<core99::t63::IterationReceipt>& history
) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < history.size(); ++index) {
        if (index != 0U) output << ',';
        const auto& receipt = history[index];
        output << "{\"iteration\":" << receipt.iteration
            << ",\"new_cfd_locations\":" << receipt.new_cfd_locations
            << ",\"cumulative_cfd_locations\":"
            << receipt.cumulative_cfd_locations
            << ",\"cfd_simulations\":" << receipt.cfd_simulations
            << ",\"mip_objective\":" << number_json(receipt.mip_objective)
            << ",\"mip_dual_bound\":" << number_json(receipt.mip_dual_bound)
            << ",\"mip_gap\":" << number_json(receipt.mip_gap)
            << ",\"mip_seconds\":" << receipt.mip_seconds
            << ",\"mip_status\":\"" << receipt.mip_status << "\""
            << ",\"selected_cells\":"
            << integer_array_json(receipt.selected_cells) << '}';
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
    if (!stream) throw std::runtime_error("cannot open T63 output: " + path);
    stream << content;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        const core99::t63::Problem problem(arguments.proxy);
        if (arguments.mode == "inspect") {
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"mode\":\"inspect\",\"problem_semantic_id\":\""
                << problem.semantic_id() << "\",\"grid_size\":"
                << problem.grid_size() << ",\"turbines\":"
                << problem.turbine_count() << ",\"corner_elevation_m\":"
                << problem.elevation_m(0) << ",\"northwest_elevation_m\":"
                << problem.elevation_m(380) << ",\"west_probability\":"
                << problem.wind_probability(9) << ",\"cell0_speed_mps\":"
                << problem.background_speed_mps(0, 0) << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument("unknown T63 mode: " + arguments.mode);
        }
        const auto result = core99::t63::run(problem, {
            arguments.relaxation,
            arguments.workers,
            arguments.maximum_iterations,
            arguments.mip_time_limit_seconds,
        });
        std::ostringstream output;
        output << std::setprecision(17)
            << "{\n"
            << "  \"mode\":\"optimization\",\n"
            << "  \"problem_semantic_id\":\"" << result.problem_semantic_id << "\",\n"
            << "  \"method_semantic_id\":\"" << result.method_semantic_id << "\",\n"
            << "  \"relaxation\":" << result.relaxation << ",\n"
            << "  \"requested_workers\":" << result.requested_workers << ",\n"
            << "  \"observed_workers\":" << result.observed_workers << ",\n"
            << "  \"iterations\":" << result.iterations << ",\n"
            << "  \"cfd_locations\":" << result.cfd_locations << ",\n"
            << "  \"cfd_simulations\":" << result.cfd_simulations << ",\n"
            << "  \"final_true_objective\":" << result.final_true_objective << ",\n"
            << "  \"no_wake_upper_bound\":" << result.no_wake_upper_bound << ",\n"
            << "  \"layout_efficiency\":" << result.layout_efficiency << ",\n"
            << "  \"field_generation_seconds\":"
            << result.field_generation_seconds << ",\n"
            << "  \"mip_seconds\":" << result.mip_seconds << ",\n"
            << "  \"end_to_end_seconds\":" << result.end_to_end_seconds << ",\n"
            << "  \"final_layout\":" << integer_array_json(result.final_layout) << ",\n"
            << "  \"history\":" << history_json(result.history) << ",\n"
            << "  \"scientific_hash\":\"" << std::hex
            << result.scientific_hash << std::dec << "\"\n"
            << "}\n";
        emit(output.str(), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T63 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
