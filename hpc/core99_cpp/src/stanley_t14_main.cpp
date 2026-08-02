/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T14 pure-C++ HPC command-line and JSON receipt
Paper DOI: 10.5194/wes-4-663-2019
Public source: https://github.com/byuflowlab/stanley2019-variable-reduction
revision 62b590065f9541c4296338b3f1a0ee07cfcd28bc
Missing/conflicts and reconstruction: include/core99/stanley_t14.hpp
Method/problem semantic IDs: t14_boundary_grid_parameterization_v1;
t14_stanley_2019_seven_unique_cases_v1
Controlling contract: shared/contracts/core99_t14_stanley_2019.json
Claim boundary: academic declared reproduction receipt
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/stanley_t14.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string algorithm = "t14_boundary_grid";
    std::string case_id = "t14_spacing4_amalia_north_island";
    std::string output;
    std::uint64_t seed = 20260731;
    std::uint64_t physical_fes_limit = 0;
    int workers = 20;
    bool evaluate_reference = false;
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
        } else if (flag == "--case") {
            arguments.case_id = value();
        } else if (flag == "--output") {
            arguments.output = value();
        } else if (flag == "--seed") {
            arguments.seed = std::stoull(value());
        } else if (flag == "--physical-fes-limit") {
            arguments.physical_fes_limit = std::stoull(value());
        } else if (flag == "--workers") {
            arguments.workers = std::stoi(value());
        } else if (flag == "--evaluate-reference") {
            arguments.evaluate_reference = true;
        } else {
            throw std::invalid_argument("unknown T14 flag: " + flag);
        }
    }
    if (arguments.workers < 1) {
        throw std::invalid_argument("workers must be positive");
    }
    return arguments;
}

core99::t14::Case find_case(const std::string& id) {
    for (const auto& paper_case : core99::t14::paper_cases()) {
        if (paper_case.id == id) {
            return paper_case;
        }
    }
    throw std::invalid_argument("unknown T14 case: " + id);
}

std::string layout_json(const std::vector<core99::t14::Point>& layout) {
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

std::string result_json(const core99::t14::RunResult& result) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\n"
        << "  \"mode\": \"optimization\",\n"
        << "  \"algorithm_id\": \"" << result.algorithm_id << "\",\n"
        << "  \"method_semantic_id\": "
           "\"t14_boundary_grid_parameterization_v1\",\n"
        << "  \"problem_id\": \"" << result.problem_id << "\",\n"
        << "  \"problem_semantic_id\": "
           "\"t14_stanley_2019_seven_unique_cases_v1\",\n"
        << "  \"optimizer_driver\": "
           "\"declared_parallel_feasibility_first_es_not_snopt\",\n"
        << "  \"best_layout\": " << layout_json(result.best_layout) << ",\n"
        << "  \"best_optimization_aep_gwh\": "
        << result.best_evaluation.optimization_aep_gwh << ",\n"
        << "  \"best_final_aep_gwh\": "
        << result.best_evaluation.final_aep_gwh << ",\n"
        << "  \"best_constraint_violation_m\": "
        << result.best_evaluation.constraint_violation_m << ",\n"
        << "  \"seed\": " << result.seed << ",\n"
        << "  \"physical_fes\": " << result.physical_fes << ",\n"
        << "  \"physical_fes_limit\": " << result.physical_fes_limit << ",\n"
        << "  \"requested_workers\": " << result.requested_workers << ",\n"
        << "  \"observed_workers\": " << result.observed_workers << ",\n"
        << "  \"evaluator_seconds\": " << result.evaluator_seconds << ",\n"
        << "  \"algorithm_seconds\": " << result.algorithm_seconds << ",\n"
        << "  \"end_to_end_seconds\": " << result.end_to_end_seconds << ",\n"
        << "  \"scientific_hash\": \"" << std::hex
        << result.scientific_hash << std::dec << "\"\n"
        << "}\n";
    return output.str();
}

std::string reference_json(
    const core99::t14::Problem& problem,
    const std::string& algorithm,
    std::uint64_t seed
) {
    const auto representation = core99::t14::representation_from_id(algorithm);
    const auto layout = core99::t14::decode_reference_layout(
        problem,
        representation,
        seed
    );
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\n"
        << "  \"mode\": \"reference_layout_evaluation\",\n"
        << "  \"algorithm_id\": \"" << algorithm << "\",\n"
        << "  \"problem_id\": \"" << problem.paper_case().id << "\",\n"
        << "  \"layout\": " << layout_json(layout) << ",\n"
        << "  \"optimization_aep_gwh\": "
        << problem.evaluate_optimization(layout) << ",\n"
        << "  \"constraint_violation_m\": "
        << problem.constraint_violation(layout) << "\n"
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
        throw std::runtime_error("cannot open T14 output: " + path);
    }
    stream << content;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        const core99::t14::Problem problem(find_case(arguments.case_id));
        if (arguments.evaluate_reference) {
            emit(
                reference_json(
                    problem,
                    arguments.algorithm,
                    arguments.seed
                ),
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        emit(
            result_json(core99::t14::run(
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
        std::cerr << "T14 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
