/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T04 pure-C++ paper-profile CLI and result receipt
Paper title and DOI: Unrestricted Wind Farm Layout Optimization,
10.1016/j.renene.2011.06.033.
Public source: no author implementation was located.
Missing fields and Reconstruction:
include/core99/uwflo_t04.hpp
Semantic IDs and Contract: shared/contracts/core99_t04_uwflo_cases.json.
Claim boundary: academic declared reconstruction, not author-source or
author-exact numerical reproduction.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/uwflo_t04.hpp"

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
    std::string problem = "t04_uwflo_case1_n9";
    std::string output;
    std::string evaluate_variables;
    std::uint64_t seed = 20260731;
    std::uint64_t physical_fes = 0;
    int workers = 20;
};

Arguments parse(int argc, char** argv) {
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
        } else if (flag == "--output") {
            args.output = value();
        } else if (flag == "--evaluate-variables") {
            args.evaluate_variables = value();
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

std::string evaluation_json(
    const std::string& problem,
    const std::vector<double>& variables,
    const core99::t04::Evaluation& value
) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\n"
        << "  \"mode\": \"fixed_layout_evaluation\",\n"
        << "  \"problem_id\": \"" << problem << "\",\n"
        << "  \"problem_semantic_id\": "
           "\"t04_uwflo_cases_declared_v1\",\n"
        << "  \"variables\": " << vector_json(variables) << ",\n"
        << "  \"farm_power_w\": " << value.farm_power_w << ",\n"
        << "  \"farm_efficiency\": " << value.farm_efficiency << ",\n"
        << "  \"constraint_violation\": "
        << value.constraint_violation << "\n"
        << "}\n";
    return out.str();
}

std::string result_json(const core99::t04::RunResult& result) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\n"
        << "  \"mode\": \"optimization\",\n"
        << "  \"algorithm_id\": \"t04_uwflo_constrained_pso\",\n"
        << "  \"method_semantic_id\": "
           "\"t04_uwflo_constrained_pso_declared_v1\",\n"
        << "  \"problem_id\": \"" << result.problem_id << "\",\n"
        << "  \"problem_semantic_id\": "
           "\"t04_uwflo_cases_declared_v1\",\n"
        << "  \"best_variables\": "
        << vector_json(result.best_variables) << ",\n"
        << "  \"best_farm_power_w\": "
        << result.best_evaluation.farm_power_w << ",\n"
        << "  \"best_farm_efficiency\": "
        << result.best_evaluation.farm_efficiency << ",\n"
        << "  \"best_constraint_violation\": "
        << result.best_evaluation.constraint_violation << ",\n"
        << "  \"seed\": " << result.seed << ",\n"
        << "  \"physical_fes\": " << result.physical_fes << ",\n"
        << "  \"requested_workers\": " << result.requested_workers << ",\n"
        << "  \"observed_workers\": " << result.observed_workers << ",\n"
        << "  \"evaluator_seconds\": " << result.evaluator_seconds << ",\n"
        << "  \"algorithm_seconds\": " << result.algorithm_seconds << ",\n"
        << "  \"end_to_end_seconds\": "
        << result.end_to_end_seconds << ",\n"
        << "  \"scientific_hash\": \"" << std::hex
        << result.scientific_hash << std::dec << "\"\n"
        << "}\n";
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
        const auto args = parse(argc, argv);
        const core99::t04::Problem problem(args.problem);
        if (!args.evaluate_variables.empty()) {
            const auto variables = parse_variables(args.evaluate_variables);
            emit(
                evaluation_json(
                    problem.id(),
                    variables,
                    problem.evaluate(variables)
                ),
                args.output
            );
            return EXIT_SUCCESS;
        }
        const std::uint64_t fes = args.physical_fes == 0
            ? core99::t04::paper_physical_fes(problem)
            : args.physical_fes;
        emit(
            result_json(
                core99::t04::run(
                    problem,
                    args.seed,
                    fes,
                    args.workers
                )
            ),
            args.output
        );
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "core99_t04_hpc error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
