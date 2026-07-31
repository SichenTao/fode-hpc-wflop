/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T22 pure-C++ DEBO CLI and machine-readable receipt
Paper DOI: 10.5194/wes-8-865-2023
Public source: paper-linked archive DOI 10.5281/zenodo.7125349
Missing information and reconstruction decisions:
include/core99/debo_t22.hpp
Method/problem semantic IDs: t22_debo_paper_reconstruction_v1;
t22_iea37_cs4_gaussian_aep_v1
Controlling contract: shared/contracts/core99_t22_iea37_cs4.json
Claim boundary: academic declared reproduction, not author DEBO source
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/debo_t22.hpp"

#include <chrono>
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
    std::string output;
    std::string evaluate_layout;
    std::string author_layout;
    std::uint64_t seed = 20260731;
    std::uint64_t physical_fes_limit = 0;
    int evaluation_repeats = 1;
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
        if (flag == "--output") {
            args.output = value();
        } else if (flag == "--evaluate-layout") {
            args.evaluate_layout = value();
        } else if (flag == "--author-layout") {
            args.author_layout = value();
        } else if (flag == "--seed") {
            args.seed = std::stoull(value());
        } else if (flag == "--physical-fes-limit") {
            args.physical_fes_limit = std::stoull(value());
        } else if (flag == "--evaluation-repeats") {
            args.evaluation_repeats = std::stoi(value());
        } else if (flag == "--workers") {
            args.workers = std::stoi(value());
        } else {
            throw std::invalid_argument("unknown flag: " + flag);
        }
    }
    return args;
}

std::vector<core99::t22::Point> parse_layout(const std::string& text) {
    std::vector<double> values;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (!token.empty()) {
            values.push_back(std::stod(token));
        }
    }
    if (values.size() % 2 != 0) {
        throw std::invalid_argument(
            "T22 layout needs comma-separated x,y pairs"
        );
    }
    std::vector<core99::t22::Point> result;
    result.reserve(values.size() / 2);
    for (std::size_t index = 0; index < values.size(); index += 2) {
        result.push_back({values[index], values[index + 1]});
    }
    return result;
}

std::string layout_json(const std::vector<core99::t22::Point>& layout) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        out << '[' << layout[index].x << ',' << layout[index].y << ']';
    }
    out << ']';
    return out.str();
}

struct FixedEvaluationReceipt {
    core99::t22::Evaluation value;
    int requested_workers = 0;
    int observed_workers = 0;
    int repeats = 0;
    double total_seconds = 0.0;
};

FixedEvaluationReceipt evaluate_fixed(
    const core99::t22::Problem& problem,
    const std::vector<core99::t22::Point>& layout,
    int workers,
    int repeats
) {
    if (workers <= 0 || repeats <= 0) {
        throw std::invalid_argument(
            "T22 fixed evaluation needs positive workers and repeats"
        );
    }
    fode::PersistentExecutor executor(workers);
    executor.reset_work_receipt();
    core99::t22::Evaluation value;
    const auto start = std::chrono::steady_clock::now();
    for (int repeat = 0; repeat < repeats; ++repeat) {
        value = problem.evaluate(layout, executor);
    }
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start
    ).count();
    return {
        value,
        workers,
        executor.work_receipt().distinct_participants,
        repeats,
        seconds
    };
}

std::string evaluation_json(
    const std::string& label,
    const std::vector<core99::t22::Point>& layout,
    const FixedEvaluationReceipt& receipt,
    double ideal_aep
) {
    const auto& value = receipt.value;
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\n"
        << "  \"mode\": \"fixed_layout_evaluation\",\n"
        << "  \"layout_label\": \"" << label << "\",\n"
        << "  \"problem_id\": \"t22_iea37_cs4\",\n"
        << "  \"problem_semantic_id\": "
           "\"t22_iea37_cs4_gaussian_aep_v1\",\n"
        << "  \"layout\": " << layout_json(layout) << ",\n"
        << "  \"aep_mwh\": " << value.aep_mwh << ",\n"
        << "  \"ideal_aep_mwh\": " << ideal_aep << ",\n"
        << "  \"wake_loss_fraction\": "
        << value.wake_loss_fraction << ",\n"
        << "  \"constraint_violation_m\": "
        << value.constraint_violation_m << ",\n"
        << "  \"requested_workers\": "
        << receipt.requested_workers << ",\n"
        << "  \"observed_workers\": "
        << receipt.observed_workers << ",\n"
        << "  \"evaluation_repeats\": " << receipt.repeats << ",\n"
        << "  \"evaluator_seconds\": "
        << receipt.total_seconds << ",\n"
        << "  \"seconds_per_evaluation\": "
        << receipt.total_seconds / receipt.repeats << "\n"
        << "}\n";
    return out.str();
}

std::string result_json(const core99::t22::RunResult& result) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\n"
        << "  \"mode\": \"optimization\",\n"
        << "  \"algorithm_id\": \"t22_debo\",\n"
        << "  \"method_semantic_id\": "
           "\"t22_debo_paper_reconstruction_v1\",\n"
        << "  \"problem_id\": \"" << result.problem_id << "\",\n"
        << "  \"problem_semantic_id\": "
           "\"t22_iea37_cs4_gaussian_aep_v1\",\n"
        << "  \"best_layout\": "
        << layout_json(result.best_layout) << ",\n"
        << "  \"best_aep_mwh\": "
        << result.best_evaluation.aep_mwh << ",\n"
        << "  \"best_wake_loss_fraction\": "
        << result.best_evaluation.wake_loss_fraction << ",\n"
        << "  \"best_constraint_violation_m\": "
        << result.best_evaluation.constraint_violation_m << ",\n"
        << "  \"seed\": " << result.seed << ",\n"
        << "  \"physical_fes\": " << result.physical_fes << ",\n"
        << "  \"paper_termination_reached\": "
        << (result.paper_termination_reached ? "true" : "false") << ",\n"
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
        const Arguments args = parse(argc, argv);
        const core99::t22::Problem problem;
        if (!args.author_layout.empty()) {
            const auto layout = args.author_layout == "base"
                ? problem.author_base_layout()
                : args.author_layout == "debo"
                    ? problem.author_debo_layout()
                    : throw std::invalid_argument(
                        "author layout must be base or debo"
                    );
            emit(
                evaluation_json(
                    args.author_layout,
                    layout,
                    evaluate_fixed(
                        problem,
                        layout,
                        args.workers,
                        args.evaluation_repeats
                    ),
                    problem.ideal_aep_mwh()
                ),
                args.output
            );
            return EXIT_SUCCESS;
        }
        if (!args.evaluate_layout.empty()) {
            const auto layout = parse_layout(args.evaluate_layout);
            emit(
                evaluation_json(
                    "provided",
                    layout,
                    evaluate_fixed(
                        problem,
                        layout,
                        args.workers,
                        args.evaluation_repeats
                    ),
                    problem.ideal_aep_mwh()
                ),
                args.output
            );
            return EXIT_SUCCESS;
        }
        emit(
            result_json(
                core99::t22::run(
                    problem,
                    args.seed,
                    args.physical_fes_limit,
                    args.workers
                )
            ),
            args.output
        );
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "core99_t22_hpc error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
