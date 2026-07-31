/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T01/T02 paper-profile CLI and result receipt
Papers: Mosetti 1994 and Grady 2005.
Paper DOIs: T01 10.1016/0167-6105(94)90080-9; T02
10.1016/j.renene.2004.05.007
Public source: no author implementation; later no-license WFLOPG cross-check.
Missing fields and Reconstruction:
include/core99/historical_grid.hpp and
shared/contracts/core99_mosetti_grady_cases.json
Semantic IDs and Contract: shared/contracts/core99_mosetti_grady_cases.json.
Claim boundary: academic declared reconstruction, not author-original code or
author-exact numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/historical_grid.hpp"

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
    std::string algorithm = "t01_mosetti_ga";
    std::string problem = "t01_mosetti_case_a";
    std::string output;
    std::string evaluate_layout;
    std::uint64_t seed = 20260731;
    std::uint64_t physical_fes = 0;
    int workers = 0;
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
        if (flag == "--algorithm") {
            args.algorithm = value();
        } else if (flag == "--problem") {
            args.problem = value();
        } else if (flag == "--output") {
            args.output = value();
        } else if (flag == "--evaluate-layout") {
            args.evaluate_layout = value();
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

std::vector<int> parse_cells(const std::string& text) {
    std::vector<int> cells;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (!token.empty()) {
            cells.push_back(std::stoi(token));
        }
    }
    return cells;
}

std::string json_cells(const core99::LayoutBits& layout) {
    const auto cells = core99::layout_cells(layout);
    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < cells.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << cells[index];
    }
    output << ']';
    return output.str();
}

std::string evaluation_json(
    const std::string& problem,
    const core99::LayoutBits& layout,
    const core99::HistoricalEvaluation& value
) {
    std::ostringstream out;
    out << std::setprecision(17);
    out << "{\n"
        << "  \"mode\": \"fixed_layout_evaluation\",\n"
        << "  \"problem_id\": \"" << problem << "\",\n"
        << "  \"problem_semantic_id\": "
           "\"core99_mosetti_grady_historical_grid_v1\",\n"
        << "  \"layout_cells_1based\": " << json_cells(layout) << ",\n"
        << "  \"turbine_count\": " << value.turbine_count << ",\n"
        << "  \"expected_power_kw\": " << value.expected_power_kw << ",\n"
        << "  \"objective\": " << value.objective << "\n"
        << "}\n";
    return out.str();
}

std::string result_json(const core99::HistoricalRunResult& result) {
    std::ostringstream out;
    out << std::setprecision(17);
    out << "{\n"
        << "  \"mode\": \"optimization\",\n"
        << "  \"algorithm_id\": \"" << result.algorithm_id << "\",\n"
        << "  \"method_semantic_id\": \"" << result.method_semantic_id
        << "\",\n"
        << "  \"problem_id\": \"" << result.problem_id << "\",\n"
        << "  \"problem_semantic_id\": \"" << result.problem_semantic_id
        << "\",\n"
        << "  \"best_layout_cells_1based\": "
        << json_cells(result.best_layout) << ",\n"
        << "  \"best_turbine_count\": "
        << result.best_evaluation.turbine_count << ",\n"
        << "  \"best_expected_power_kw\": "
        << result.best_evaluation.expected_power_kw << ",\n"
        << "  \"best_objective\": "
        << result.best_evaluation.objective << ",\n"
        << "  \"seed\": " << result.seed << ",\n"
        << "  \"physical_fes\": " << result.physical_fes << ",\n"
        << "  \"completed_generations\": " << result.generations << ",\n"
        << "  \"requested_workers\": " << result.requested_workers << ",\n"
        << "  \"observed_workers\": " << result.observed_workers << ",\n"
        << "  \"evaluator_seconds\": " << result.evaluator_seconds << ",\n"
        << "  \"algorithm_seconds\": " << result.algorithm_seconds << ",\n"
        << "  \"end_to_end_seconds\": " << result.end_to_end_seconds << ",\n"
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
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot open output: " + path);
    }
    output << payload;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto args = parse(argc, argv);
        const core99::HistoricalGridProblem problem(args.problem);
        if (!args.evaluate_layout.empty()) {
            const auto layout = core99::layout_from_cells(
                parse_cells(args.evaluate_layout)
            );
            emit(
                evaluation_json(args.problem, layout, problem.evaluate(layout)),
                args.output
            );
            return EXIT_SUCCESS;
        }
        const auto profile = core99::historical_profile(args.algorithm);
        const std::uint64_t physical_fes = args.physical_fes == 0
            ? core99::default_physical_fes(profile, problem.id())
            : args.physical_fes;
        const int workers = args.workers <= 0 ? 20 : args.workers;
        const auto result = core99::run_historical_ga(
            problem,
            profile,
            {args.seed, physical_fes, workers}
        );
        emit(result_json(result), args.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "core99_historical_grid_hpc error: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
