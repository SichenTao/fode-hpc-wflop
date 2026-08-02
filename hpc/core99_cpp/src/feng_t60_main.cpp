/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T60 pure-C++ command-line and full-core campaign driver
Paper/DOI: Solving the Wind Farm Layout Optimization Problem Using Random
Search Algorithm; 10.1016/j.renene.2015.01.005
Public source, missing/conflicting fields and completion policy:
hpc/core99_cpp/include/core99/feng_t60.hpp
Method/problem semantic IDs: t60_improved_rs_incremental_v1;
t60_ideal_continuous_jensen_v1; t60_hornsrev_jensen_v80_v1
Controlling contract: shared/contracts/core99_t60_feng_shen_2015.json
HPC design: independent paper runs are assigned to a persistent full-core
worker team; each run uses the paper-specific O(S*N) incremental evaluator;
fixed-layout full evaluation can instead parallelize wind directions.
Claim boundary: declared flexible academic reproduction, not author replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/feng_t60.hpp"

#include "fode/executor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Arguments {
    std::string problem = "t60_ideal_case1";
    int direction_sectors = 360;
    int workers = 20;
    int runs = 1;
    std::uint64_t physical_fes = 100000;
    std::uint64_t seed = 20260731;
    int evaluation_repeats = 1;
    bool random_initial_layout = false;
    double direction_rotation_degrees = 0.0;
    double weibull_scale_multiplier = 1.0;
    double weibull_shape_multiplier = 1.0;
    std::string layout_csv;
    std::string output;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() -> std::string {
            if (++index >= argc) {
                throw std::invalid_argument("missing value for " + flag);
            }
            return argv[index];
        };
        if (flag == "--problem") {
            result.problem = value();
        } else if (flag == "--direction-sectors") {
            result.direction_sectors = std::stoi(value());
        } else if (flag == "--workers") {
            result.workers = std::stoi(value());
        } else if (flag == "--runs") {
            result.runs = std::stoi(value());
        } else if (flag == "--physical-fes") {
            result.physical_fes = std::stoull(value());
        } else if (flag == "--seed") {
            result.seed = std::stoull(value());
        } else if (flag == "--evaluation-repeats") {
            result.evaluation_repeats = std::stoi(value());
        } else if (flag == "--initial") {
            const std::string mode = value();
            if (mode == "random") {
                result.random_initial_layout = true;
            } else if (mode == "paper") {
                result.random_initial_layout = false;
            } else {
                throw std::invalid_argument("T60 initial must be paper/random");
            }
        } else if (flag == "--direction-rotation-degrees") {
            result.direction_rotation_degrees = std::stod(value());
        } else if (flag == "--weibull-scale-multiplier") {
            result.weibull_scale_multiplier = std::stod(value());
        } else if (flag == "--weibull-shape-multiplier") {
            result.weibull_shape_multiplier = std::stod(value());
        } else if (flag == "--layout-csv") {
            result.layout_csv = value();
        } else if (flag == "--output") {
            result.output = value();
        } else {
            throw std::invalid_argument("unknown T60 flag: " + flag);
        }
    }
    if (
        result.workers <= 0
        || result.runs <= 0
        || result.evaluation_repeats <= 0
        || result.physical_fes == 0
    ) {
        throw std::invalid_argument("invalid T60 execution configuration");
    }
    return result;
}

std::vector<core99::t60::Point> parse_layout(
    const std::string& csv,
    const int turbines
) {
    std::istringstream input(csv);
    std::string token;
    std::vector<double> values;
    while (std::getline(input, token, ',')) {
        values.push_back(std::stod(token));
    }
    if (values.size() != static_cast<std::size_t>(2 * turbines)) {
        throw std::invalid_argument("T60 layout CSV cardinality mismatch");
    }
    std::vector<core99::t60::Point> result(
        static_cast<std::size_t>(turbines)
    );
    for (int turbine = 0; turbine < turbines; ++turbine) {
        result[static_cast<std::size_t>(turbine)] = {
            values[static_cast<std::size_t>(2 * turbine)],
            values[static_cast<std::size_t>(2 * turbine + 1)],
        };
    }
    return result;
}

std::string evaluation_json(
    const core99::t60::Evaluation& evaluation
) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{"
        << "\"expected_power_kw\":" << evaluation.expected_power_kw << ','
        << "\"no_wake_power_kw\":" << evaluation.no_wake_power_kw << ','
        << "\"efficiency\":" << evaluation.efficiency << ','
        << "\"constraint_violation_m\":"
        << evaluation.constraint_violation_m << ','
        << "\"feasible\":" << (evaluation.feasible ? "true" : "false")
        << '}';
    return out.str();
}

std::string layout_json(
    const std::vector<core99::t60::Point>& layout
) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index != 0U) out << ',';
        out << '[' << layout[index].x_m << ',' << layout[index].y_m << ']';
    }
    out << ']';
    return out.str();
}

std::string run_json(const core99::t60::RunResult& run) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{"
        << "\"problem_id\":\"" << run.problem_id << "\","
        << "\"problem_semantic_id\":\""
        << run.problem_semantic_id << "\","
        << "\"method_semantic_id\":\""
        << run.method_semantic_id << "\","
        << "\"seed\":" << run.seed << ','
        << "\"physical_fes\":" << run.physical_fes << ','
        << "\"random_initial_layout\":"
        << (run.random_initial_layout ? "true" : "false") << ','
        << "\"feasible_proposals\":" << run.feasible_proposals << ','
        << "\"rejected_infeasible_proposals\":"
        << run.rejected_infeasible_proposals << ','
        << "\"accepted_moves\":" << run.accepted_moves << ','
        << "\"initial_power_kw\":" << run.initial_power_kw << ','
        << "\"evaluator_seconds\":" << run.evaluator_seconds << ','
        << "\"algorithm_seconds\":" << run.algorithm_seconds << ','
        << "\"end_to_end_seconds\":" << run.end_to_end_seconds << ','
        << "\"final_evaluation\":" << evaluation_json(run.final_evaluation)
        << ",\"scientific_hash\":\""
        << std::hex << run.scientific_hash << std::dec << "\","
        << "\"final_layout\":" << layout_json(run.final_layout)
        << '}';
    return out.str();
}

void emit(const std::string& payload, const std::string& path) {
    if (path.empty()) {
        std::cout << payload;
        return;
    }
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("cannot open T60 output: " + path);
    stream << payload;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        core99::t60::Problem problem(
            arguments.problem,
            arguments.direction_sectors,
            arguments.direction_rotation_degrees,
            arguments.weibull_scale_multiplier,
            arguments.weibull_shape_multiplier
        );
        if (!arguments.layout_csv.empty()) {
            const auto layout = parse_layout(
                arguments.layout_csv, problem.turbine_count()
            );
            fode::PersistentExecutor executor(arguments.workers);
            executor.reset_work_receipt();
            core99::t60::Evaluation evaluation;
            const auto evaluation_start =
                std::chrono::steady_clock::now();
            for (
                int repeat = 0;
                repeat < arguments.evaluation_repeats;
                ++repeat
            ) {
                evaluation = problem.evaluate_parallel(layout, executor);
            }
            const double evaluator_seconds =
                std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - evaluation_start
                ).count();
            const auto receipt = executor.work_receipt();
            std::ostringstream out;
            out << std::setprecision(17)
                << "{\n"
                << "  \"mode\":\"fixed_layout_evaluation\",\n"
                << "  \"problem_id\":\"" << problem.id() << "\",\n"
                << "  \"problem_semantic_id\":\""
                << problem.semantic_id() << "\",\n"
                << "  \"direction_sectors\":"
                << problem.direction_count() << ",\n"
                << "  \"requested_workers\":" << arguments.workers << ",\n"
                << "  \"observed_workers\":"
                << receipt.distinct_participants << ",\n"
                << "  \"evaluation_repeats\":"
                << arguments.evaluation_repeats << ",\n"
                << "  \"evaluator_seconds\":"
                << evaluator_seconds << ",\n"
                << "  \"seconds_per_evaluation\":"
                << evaluator_seconds
                    / static_cast<double>(arguments.evaluation_repeats)
                << ",\n"
                << "  \"evaluation\":" << evaluation_json(evaluation)
                << ",\n"
                << "  \"layout\":" << layout_json(layout) << "\n"
                << "}\n";
            emit(out.str(), arguments.output);
            return 0;
        }

        std::vector<core99::t60::RunResult> runs(
            static_cast<std::size_t>(arguments.runs)
        );
        fode::PersistentExecutor executor(arguments.workers);
        executor.reset_work_receipt();
        executor.parallel_for(
            0,
            arguments.runs,
            [&](const int index) {
                runs[static_cast<std::size_t>(index)] = core99::t60::run(
                    problem,
                    {
                        .seed = arguments.seed
                            + static_cast<std::uint64_t>(index),
                        .physical_fes = arguments.physical_fes,
                        .random_initial_layout =
                            arguments.random_initial_layout,
                    }
                );
            }
        );
        const auto receipt = executor.work_receipt();
        std::ostringstream out;
        out << std::setprecision(17)
            << "{\n"
            << "  \"mode\":\"optimization_campaign\",\n"
            << "  \"problem_id\":\"" << problem.id() << "\",\n"
            << "  \"problem_semantic_id\":\""
            << problem.semantic_id() << "\",\n"
            << "  \"method_semantic_id\":"
               "\"t60_improved_rs_incremental_v1\",\n"
            << "  \"direction_sectors\":" << problem.direction_count()
            << ",\n"
            << "  \"runs\":" << arguments.runs << ",\n"
            << "  \"physical_fes_per_run\":"
            << arguments.physical_fes << ",\n"
            << "  \"requested_workers\":" << arguments.workers << ",\n"
            << "  \"campaign_observed_workers\":"
            << receipt.distinct_participants << ",\n"
            << "  \"run_receipts\":[";
        for (std::size_t index = 0; index < runs.size(); ++index) {
            if (index != 0U) out << ',';
            out << run_json(runs[index]);
        }
        out << "]\n}\n";
        emit(out.str(), arguments.output);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "core99_t60_hpc: " << error.what() << '\n';
        return 2;
    }
}
