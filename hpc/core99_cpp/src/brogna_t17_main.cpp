/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T17 command-line driver and machine-readable receipt
Paper/DOI: A New Wake Model and Comparison of Eight Algorithms for Layout
Optimization of Wind Farms in Complex Terrain; 10.1016/j.apenergy.2019.114189
Public source/missing/reconstruction: include/core99/brogna_t17.hpp
Semantic IDs: t17_brogna_private_site_open_flow_proxy_v1;
t17_double_stage_rs_declared_reconstruction_v1
Controlling contract: shared/contracts/core99_t17_brogna_2020.json
Claim boundary: academic declared reproduction, not private-site replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/brogna_t17.hpp"

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
    std::uint64_t seed = 1701;
    std::uint64_t stage1_fes = 100;
    std::uint64_t stage2_fes = 400;
    int workers = 20;
    bool random_initial = false;
    bool no_wakes = false;
    double streamwise_d = 5.0;
    double radial_d = 0.0;
    double ct = 0.747;
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
        else if (flag == "--seed") arguments.seed = std::stoull(value());
        else if (flag == "--stage1-fes") arguments.stage1_fes = std::stoull(value());
        else if (flag == "--stage2-fes") arguments.stage2_fes = std::stoull(value());
        else if (flag == "--workers") arguments.workers = std::stoi(value());
        else if (flag == "--random-initial") arguments.random_initial = true;
        else if (flag == "--no-wakes") arguments.no_wakes = true;
        else if (flag == "--streamwise-d") arguments.streamwise_d = std::stod(value());
        else if (flag == "--radial-d") arguments.radial_d = std::stod(value());
        else if (flag == "--ct") arguments.ct = std::stod(value());
        else throw std::invalid_argument("unknown T17 flag: " + flag);
    }
    if (arguments.mode != "gaussian" && arguments.proxy.empty()) {
        throw std::invalid_argument("--proxy is required");
    }
    return arguments;
}

std::string evaluation_json(const core99::t17::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"objective\":" << value.objective
        << ",\"constraint_violation_m\":" << value.constraint_violation_m
        << ",\"includes_wakes\":" << (value.includes_wakes ? "true" : "false")
        << '}';
    return output.str();
}

std::string layout_json(const std::vector<core99::t17::Point>& layout) {
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
    if (!stream) throw std::runtime_error("cannot open T17 output: " + path);
    stream << content;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "gaussian") {
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"deficit_ratio\":"
                << core99::t17::gaussian_deficit_ratio(
                    arguments.streamwise_d, arguments.radial_d, arguments.ct
                ) << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        const core99::t17::Problem problem(arguments.proxy);
        if (arguments.mode == "evaluate") {
            fode::PersistentExecutor executor(arguments.workers);
            const auto layout = core99::t17::paper_figure_2_layout();
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"mode\":\"evaluate\",\"problem_semantic_id\":\""
                << problem.semantic_id() << "\",\"evaluation\":"
                << evaluation_json(problem.evaluate(
                    layout, !arguments.no_wakes, executor
                )) << "}\n";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument("unknown T17 mode: " + arguments.mode);
        }
        const core99::t17::RunResult result = core99::t17::run_double_stage_rs(
            problem,
            arguments.seed,
            arguments.workers,
            {
                arguments.stage1_fes,
                arguments.stage2_fes,
                arguments.random_initial,
            }
        );
        std::ostringstream output;
        output << std::setprecision(17)
            << "{\n"
            << "  \"mode\":\"optimization\",\n"
            << "  \"problem_semantic_id\":\"" << result.problem_semantic_id << "\",\n"
            << "  \"method_semantic_id\":\"" << result.method_semantic_id << "\",\n"
            << "  \"seed\":" << result.seed << ",\n"
            << "  \"stage1_physical_fes\":" << result.stage1_physical_fes << ",\n"
            << "  \"stage2_physical_fes\":" << result.stage2_physical_fes << ",\n"
            << "  \"physical_fes\":" << result.physical_fes << ",\n"
            << "  \"requested_workers\":" << result.requested_workers << ",\n"
            << "  \"observed_workers\":" << result.observed_workers << ",\n"
            << "  \"evaluator_seconds\":" << result.evaluator_seconds << ",\n"
            << "  \"algorithm_seconds\":" << result.algorithm_seconds << ",\n"
            << "  \"end_to_end_seconds\":" << result.end_to_end_seconds << ",\n"
            << "  \"initial_wake_evaluation\":"
            << evaluation_json(result.initial_wake_evaluation) << ",\n"
            << "  \"stage1_evaluation\":"
            << evaluation_json(result.stage1_evaluation) << ",\n"
            << "  \"final_evaluation\":"
            << evaluation_json(result.final_evaluation) << ",\n"
            << "  \"final_layout\":" << layout_json(result.final_layout) << ",\n"
            << "  \"scientific_hash\":\"" << std::hex
            << result.scientific_hash << std::dec << "\"\n"
            << "}\n";
        emit(output.str(), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T17 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
