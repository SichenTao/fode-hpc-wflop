/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T03 pure-C++ paper-profile CLI and result receipt
Paper/DOI/evidence/missing/resolution/semantics/claim:
include/core99/kusiak_t03.hpp
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/kusiak_t03.hpp"

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
    std::string problem = "t03_kusiak_s1_n6";
    std::string output;
    std::string evaluate_layout;
    std::uint64_t seed = 20260731;
    std::uint64_t physical_fes = 12120;
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

std::vector<core99::t03::Point> parse_layout(const std::string& text) {
    std::vector<core99::t03::Point> result;
    std::stringstream rows(text);
    std::string row;
    while (std::getline(rows, row, ';')) {
        std::stringstream columns(row);
        std::string x;
        std::string y;
        if (
            !std::getline(columns, x, ',')
            || !std::getline(columns, y, ',')
        ) {
            throw std::invalid_argument("layout must be x,y;x,y");
        }
        result.push_back({std::stod(x), std::stod(y)});
    }
    return result;
}

std::string points_json(const std::vector<core99::t03::Point>& points) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        out << '[' << points[index].x << ',' << points[index].y << ']';
    }
    out << ']';
    return out.str();
}

std::string evaluation_json(
    const std::string& problem,
    const std::vector<core99::t03::Point>& layout,
    const core99::t03::Evaluation& value
) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\n"
        << "  \"mode\": \"fixed_layout_evaluation\",\n"
        << "  \"problem_id\": \"" << problem << "\",\n"
        << "  \"problem_semantic_id\": "
           "\"t03_kusiak_circular_expected_power_v1\",\n"
        << "  \"layout_xy_m\": " << points_json(layout) << ",\n"
        << "  \"expected_power_kw\": " << value.expected_power_kw << ",\n"
        << "  \"inverse_power\": " << value.inverse_power << ",\n"
        << "  \"constraint_violation\": "
        << value.constraint_violation << "\n"
        << "}\n";
    return out.str();
}

std::string result_json(const core99::t03::RunResult& result) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\n"
        << "  \"mode\": \"optimization\",\n"
        << "  \"algorithm_id\": \"t03_kusiak_spea_es\",\n"
        << "  \"method_semantic_id\": "
           "\"t03_kusiak_spea_es_declared_v1\",\n"
        << "  \"problem_id\": \"" << result.problem_id << "\",\n"
        << "  \"problem_semantic_id\": "
           "\"t03_kusiak_circular_expected_power_v1\",\n"
        << "  \"best_layout_xy_m\": "
        << points_json(result.best_layout) << ",\n"
        << "  \"best_expected_power_kw\": "
        << result.best_evaluation.expected_power_kw << ",\n"
        << "  \"best_inverse_power\": "
        << result.best_evaluation.inverse_power << ",\n"
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
        const core99::t03::Problem problem(args.problem);
        if (!args.evaluate_layout.empty()) {
            const auto layout = parse_layout(args.evaluate_layout);
            emit(
                evaluation_json(
                    problem.id(),
                    layout,
                    problem.evaluate(layout)
                ),
                args.output
            );
            return EXIT_SUCCESS;
        }
        emit(
            result_json(
                core99::t03::run(
                    problem,
                    args.seed,
                    args.physical_fes,
                    args.workers
                )
            ),
            args.output
        );
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "core99_t03_hpc error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
