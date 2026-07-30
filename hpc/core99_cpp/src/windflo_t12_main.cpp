/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T12 pure-C++ HPC command-line and JSON receipt
Paper DOI: 10.1016/j.renene.2018.03.052
Public source: https://github.com/d9w/WindFLO revision
9e85a67bb2ca019768ea51dd0b634a46c8406ba2, MIT license
Missing/conflicts and reconstruction: include/core99/windflo_t12.hpp
Method/problem semantic IDs: t12_four_competition_methods_v1;
t12_windflo_2015_five_scenarios_v1
Controlling contract: shared/contracts/core99_t12_windflo_2015.json
Claim boundary: academic declared reproduction receipt
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/windflo_t12.hpp"

#include "fode/executor.hpp"

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
    std::string algorithm = "t12_goldman_lattice";
    std::string output;
    std::string evaluate_layout;
    int scenario = 1;
    std::uint64_t seed = 20260731;
    std::uint64_t physical_fes_limit = 0;
    int workers = 20;
};

Arguments parse(int argc, char** argv) {
    Arguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument("missing value for " + flag);
            }
            return std::string(argv[index]);
        };
        if (flag == "--algorithm") {
            arguments.algorithm = value();
        } else if (flag == "--output") {
            arguments.output = value();
        } else if (flag == "--evaluate-layout") {
            arguments.evaluate_layout = value();
        } else if (flag == "--scenario") {
            arguments.scenario = std::stoi(value());
        } else if (flag == "--seed") {
            arguments.seed = std::stoull(value());
        } else if (flag == "--physical-fes-limit") {
            arguments.physical_fes_limit = std::stoull(value());
        } else if (flag == "--workers") {
            arguments.workers = std::stoi(value());
        } else {
            throw std::invalid_argument("unknown flag: " + flag);
        }
    }
    if (arguments.scenario < 1 || arguments.scenario > 5) {
        throw std::invalid_argument("scenario must be 1..5");
    }
    return arguments;
}

std::vector<core99::t12::Point> layout_from_text(const std::string& text) {
    std::stringstream stream(text);
    std::string token;
    std::vector<double> values;
    while (std::getline(stream, token, ',')) {
        if (!token.empty()) {
            values.push_back(std::stod(token));
        }
    }
    if (values.size() % 2 != 0) {
        throw std::invalid_argument("layout needs comma-separated x,y pairs");
    }
    std::vector<core99::t12::Point> layout;
    for (std::size_t index = 0; index < values.size(); index += 2) {
        layout.push_back({values[index], values[index + 1]});
    }
    return layout;
}

std::string layout_json(const std::vector<core99::t12::Point>& layout) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << '[' << layout[index].x << ',' << layout[index].y << ']';
    }
    output << ']';
    return output.str();
}

std::string evaluation_json(
    const core99::t12::Problem& problem,
    const std::vector<core99::t12::Point>& layout,
    const core99::t12::Evaluation& value,
    int workers
) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\n"
        << "  \"mode\": \"fixed_layout_evaluation\",\n"
        << "  \"problem_id\": \"" << problem.id() << "\",\n"
        << "  \"problem_semantic_id\": "
           "\"t12_windflo_2015_five_scenarios_v1\",\n"
        << "  \"layout\": " << layout_json(layout) << ",\n"
        << "  \"energy_cost\": " << value.energy_cost << ",\n"
        << "  \"wake_free_ratio\": " << value.wake_free_ratio << ",\n"
        << "  \"energy_output_kw\": " << value.energy_output_kw << ",\n"
        << "  \"constraint_violation_m\": "
        << value.constraint_violation_m << ",\n"
        << "  \"requested_workers\": " << workers << "\n"
        << "}\n";
    return output.str();
}

std::string result_json(const core99::t12::RunResult& result) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\n"
        << "  \"mode\": \"optimization\",\n"
        << "  \"algorithm_id\": \"" << result.algorithm_id << "\",\n"
        << "  \"method_semantic_id\": "
           "\"t12_four_competition_methods_v1\",\n"
        << "  \"problem_id\": \"" << result.problem_id << "\",\n"
        << "  \"problem_semantic_id\": "
           "\"t12_windflo_2015_five_scenarios_v1\",\n"
        << "  \"best_layout\": " << layout_json(result.best_layout) << ",\n"
        << "  \"best_energy_cost\": "
        << result.best_evaluation.energy_cost << ",\n"
        << "  \"best_wake_free_ratio\": "
        << result.best_evaluation.wake_free_ratio << ",\n"
        << "  \"best_energy_output_kw\": "
        << result.best_evaluation.energy_output_kw << ",\n"
        << "  \"best_constraint_violation_m\": "
        << result.best_evaluation.constraint_violation_m << ",\n"
        << "  \"seed\": " << result.seed << ",\n"
        << "  \"physical_fes\": " << result.physical_fes << ",\n"
        << "  \"physical_fes_limit\": "
        << result.physical_fes_limit << ",\n"
        << "  \"requested_workers\": " << result.requested_workers << ",\n"
        << "  \"observed_workers\": " << result.observed_workers << ",\n"
        << "  \"evaluator_seconds\": " << result.evaluator_seconds << ",\n"
        << "  \"algorithm_seconds\": " << result.algorithm_seconds << ",\n"
        << "  \"end_to_end_seconds\": "
        << result.end_to_end_seconds << ",\n"
        << "  \"scientific_hash\": \"" << std::hex
        << result.scientific_hash << std::dec << "\"\n"
        << "}\n";
    return output.str();
}

void emit(const std::string& content, const std::string& path) {
    if (path.empty()) {
        std::cout << content;
        return;
    }
    std::ofstream stream(path);
    if (!stream) {
        throw std::runtime_error("cannot open output: " + path);
    }
    stream << content;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        const core99::t12::Problem problem(arguments.scenario - 1);
        if (!arguments.evaluate_layout.empty()) {
            const auto layout = layout_from_text(arguments.evaluate_layout);
            fode::PersistentExecutor executor(arguments.workers);
            emit(
                evaluation_json(
                    problem,
                    layout,
                    problem.evaluate(layout, executor),
                    arguments.workers
                ),
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        emit(
            result_json(core99::t12::run(
                problem,
                arguments.algorithm,
                arguments.seed,
                arguments.physical_fes_limit,
                arguments.workers
            )),
            arguments.output
        );
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T12 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
