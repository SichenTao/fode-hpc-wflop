/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T20 pure-C++ benchmark and H6 CLI
Paper title and DOI: Comparative Performance of Twelve Metaheuristics for
Wind Farm Layout Optimisation, 10.1007/s11831-021-09586-7.
Public source: no paper-linked author code or data archive was located.
Missing fields and Reconstruction: include/core99/kunakote_t20.hpp.
Semantic IDs and Contract: shared/contracts/core99_t20_kunakote_2022.json.
Claim boundary: problem and comparison-interface reproduction, not author
code or twelve-baseline numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/kunakote_t20.hpp"

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
    std::string problem = "t20_case1_variable_hub";
    std::string mode = "batch";
    std::string variables;
    std::string output;
    std::uint64_t seed = 20260731;
    std::uint64_t physical_fes = 1000;
    int workers = 20;
};

Arguments parse(const int argc, char** argv) {
    Arguments args;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() -> std::string {
            if (++index >= argc) {
                throw std::invalid_argument("missing value for " + flag);
            }
            return argv[index];
        };
        if (flag == "--problem") {
            args.problem = value();
        } else if (flag == "--mode") {
            args.mode = value();
        } else if (flag == "--variables") {
            args.variables = value();
        } else if (flag == "--output") {
            args.output = value();
        } else if (flag == "--seed") {
            args.seed = std::stoull(value());
        } else if (flag == "--physical-fes") {
            args.physical_fes = std::stoull(value());
        } else if (flag == "--workers") {
            args.workers = std::stoi(value());
        } else {
            throw std::invalid_argument("unknown flag: " + flag);
        }
    }
    return args;
}

std::vector<double> parse_variables(const std::string& text) {
    std::vector<double> result;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (!token.empty()) {
            result.push_back(std::stod(token));
        }
    }
    return result;
}

std::string vector_json(const std::vector<double>& values) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        out << values[index];
    }
    out << ']';
    return out.str();
}

std::string evaluation_fields(const core99::t20::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "  \"objective\": " << value.objective << ",\n"
        << "  \"average_power_kw\": " << value.average_power_kw << ",\n"
        << "  \"cost\": " << value.cost << ",\n"
        << "  \"constraint_violation\": "
        << value.constraint_violation << ",\n"
        << "  \"turbine_count\": " << value.turbine_count;
    return out.str();
}

void emit(const std::string& payload, const std::string& path) {
    if (path.empty()) {
        std::cout << payload;
        return;
    }
    std::ofstream stream(path);
    if (!stream) {
        throw std::runtime_error("cannot open output: " + path);
    }
    stream << payload;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments args = parse(argc, argv);
        const core99::t20::Problem problem(args.problem);
        std::ostringstream out;
        out << std::setprecision(17);
        if (args.mode == "figure5") {
            const auto value = problem.evaluate_layout(
                core99::t20::paper_figure_5_layout()
            );
            out << "{\n"
                << "  \"mode\": \"paper_figure_5_evaluation\",\n"
                << "  \"problem_id\": \"" << problem.id() << "\",\n"
                << "  \"problem_semantic_id\": "
                   "\"t20_kunakote_four_case_benchmark_declared_v1\",\n"
                << evaluation_fields(value) << "\n}\n";
        } else if (args.mode == "evaluate") {
            const auto values = parse_variables(args.variables);
            const auto value = problem.evaluate(values);
            out << "{\n"
                << "  \"mode\": \"fixed_variables_evaluation\",\n"
                << "  \"problem_id\": \"" << problem.id() << "\",\n"
                << "  \"problem_semantic_id\": "
                   "\"t20_kunakote_four_case_benchmark_declared_v1\",\n"
                << evaluation_fields(value) << "\n}\n";
        } else if (args.mode == "batch") {
            const auto receipt = core99::t20::run_batch_profile(
                problem,
                args.seed,
                args.physical_fes,
                args.workers
            );
            out << "{\n"
                << "  \"mode\": \"paper_problem_batch_profile\",\n"
                << "  \"method_semantic_id\": "
                   "\"t20_comparison_protocol_no_target_optimizer_v1\",\n"
                << "  \"problem_id\": \"" << receipt.problem_id << "\",\n"
                << "  \"problem_semantic_id\": "
                   "\"t20_kunakote_four_case_benchmark_declared_v1\",\n"
                << "  \"seed\": " << receipt.seed << ",\n"
                << "  \"physical_fes\": " << receipt.physical_fes << ",\n"
                << "  \"requested_workers\": "
                << receipt.requested_workers << ",\n"
                << "  \"observed_workers\": "
                << receipt.observed_workers << ",\n"
                << "  \"evaluator_seconds\": "
                << receipt.evaluator_seconds << ",\n"
                << "  \"algorithm_seconds\": "
                << receipt.algorithm_seconds << ",\n"
                << "  \"end_to_end_seconds\": "
                << receipt.end_to_end_seconds << ",\n"
                << "  \"best_variables\": "
                << vector_json(receipt.best_variables) << ",\n"
                << evaluation_fields(receipt.best_evaluation) << ",\n"
                << "  \"scientific_hash\": \"" << std::hex
                << receipt.scientific_hash << std::dec << "\"\n"
                << "}\n";
        } else {
            throw std::invalid_argument("unknown T20 mode: " + args.mode);
        }
        emit(out.str(), args.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "core99_t20_hpc error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
