/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T11 command-line and full-core campaign driver
Paper/DOI: BlockCopy-Based Operators for Evolving Efficient Wind Farm
Layouts; 10.1109/CEC.2016.7743909
Public source, missing/conflicting facts and completion policy:
hpc/core99_cpp/include/core99/blockcopy_t11.hpp
Method/problem semantic IDs: t11_blockcopy_four_es_methods_v1;
t11_kusiak_and_2014_competition_four_cases_v1
Controlling contract: shared/contracts/core99_t11_blockcopy_2016.json
HPC design: a single trajectory assigns its direction/turbine evaluator work
to all requested cores; multi-run paper campaigns assign independent
trajectories to one persistent all-core team without nested oversubscription.
Claim boundary: source-backed flexible academic reproduction, not author
source, RNG or exact-number replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/blockcopy_t11.hpp"

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
    std::string problem = "t11_ks1_n100";
    std::string algorithm = "t11_1plus1_blockcopy_mutation";
    int workers = 20;
    int runs = 1;
    std::uint64_t physical_fes = 2000;
    std::uint64_t seed = 20260731;
    int evaluation_repeats = 1;
    std::string layout_csv;
    std::string incremental_child_csv;
    std::string output;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument("missing value for " + flag);
            }
            return std::string(argv[index]);
        };
        if (flag == "--problem") {
            result.problem = value();
        } else if (flag == "--algorithm") {
            result.algorithm = value();
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
        } else if (flag == "--layout-csv") {
            result.layout_csv = value();
        } else if (flag == "--incremental-child-csv") {
            result.incremental_child_csv = value();
        } else if (flag == "--output") {
            result.output = value();
        } else {
            throw std::invalid_argument("unknown T11 flag: " + flag);
        }
    }
    if (
        result.workers <= 0 || result.runs <= 0
        || result.physical_fes == 0 || result.evaluation_repeats <= 0
    ) {
        throw std::invalid_argument("invalid T11 execution configuration");
    }
    return result;
}

std::vector<core99::t11::Point> parse_layout(
    const std::string& csv,
    const int turbines
) {
    std::istringstream input(csv);
    std::string token;
    std::vector<double> values;
    while (std::getline(input, token, ',')) values.push_back(std::stod(token));
    if (values.size() != static_cast<std::size_t>(2 * turbines)) {
        throw std::invalid_argument("T11 layout CSV cardinality mismatch");
    }
    std::vector<core99::t11::Point> result(
        static_cast<std::size_t>(turbines)
    );
    for (int index = 0; index < turbines; ++index) {
        result[static_cast<std::size_t>(index)] = {
            values[static_cast<std::size_t>(2 * index)],
            values[static_cast<std::size_t>(2 * index + 1)]
        };
    }
    return result;
}

std::string evaluation_json(const core99::t11::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << '{'
        << "\"energy_cost\":" << value.energy_cost << ','
        << "\"energy_output_kw\":" << value.energy_output_kw << ','
        << "\"wake_free_ratio\":" << value.wake_free_ratio << ','
        << "\"constraint_violation_m\":"
        << value.constraint_violation_m << ','
        << "\"feasible\":" << (value.feasible ? "true" : "false")
        << '}';
    return out.str();
}

std::string layout_json(const std::vector<core99::t11::Point>& layout) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index != 0U) out << ',';
        out << '[' << layout[index].x_m << ',' << layout[index].y_m << ']';
    }
    out << ']';
    return out.str();
}

std::string run_json(const core99::t11::RunResult& run) {
    std::ostringstream out;
    out << std::setprecision(17)
        << '{'
        << "\"problem_id\":\"" << run.problem_id << "\","
        << "\"algorithm_id\":\"" << run.algorithm_id << "\","
        << "\"problem_semantic_id\":\""
        << run.problem_semantic_id << "\","
        << "\"method_semantic_id\":\""
        << run.method_semantic_id << "\","
        << "\"seed\":" << run.seed << ','
        << "\"physical_fes\":" << run.physical_fes << ','
        << "\"requested_workers\":" << run.requested_workers << ','
        << "\"observed_workers\":" << run.observed_workers << ','
        << "\"accepted_offspring\":" << run.accepted_offspring << ','
        << "\"initial_energy_cost\":" << run.initial_energy_cost << ','
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
    if (!stream) throw std::runtime_error("cannot open T11 output: " + path);
    stream << payload;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        core99::t11::Problem problem(arguments.problem);
        if (!arguments.layout_csv.empty()) {
            const auto layout = parse_layout(
                arguments.layout_csv, problem.turbine_count()
            );
            fode::PersistentExecutor executor(arguments.workers);
            executor.reset_work_receipt();
            const auto start = std::chrono::steady_clock::now();
            core99::t11::Evaluation evaluation;
            std::unique_ptr<core99::t11::Problem::State> parent;
            std::unique_ptr<core99::t11::Problem::State> child;
            if (arguments.incremental_child_csv.empty()) {
                for (int repeat = 0;
                     repeat < arguments.evaluation_repeats;
                     ++repeat) {
                    evaluation = problem.evaluate_parallel(layout, executor);
                }
            } else {
                const auto child_layout = parse_layout(
                    arguments.incremental_child_csv,
                    problem.turbine_count()
                );
                parent = problem.make_state(layout, executor);
                for (int repeat = 0;
                     repeat < arguments.evaluation_repeats;
                     ++repeat) {
                    child = problem.update_state(
                        *parent, child_layout, executor
                    );
                }
                evaluation = problem.state_evaluation(*child);
            }
            const double seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start
            ).count();
            const auto receipt = executor.work_receipt();
            std::ostringstream out;
            out << std::setprecision(17)
                << "{\n"
                << "  \"mode\":\""
                << (
                    arguments.incremental_child_csv.empty()
                    ? "fixed_layout_evaluation"
                    : "incremental_layout_evaluation"
                ) << "\",\n"
                << "  \"problem_id\":\"" << problem.id() << "\",\n"
                << "  \"evaluation\":" << evaluation_json(evaluation) << ",\n"
                << "  \"requested_workers\":" << arguments.workers << ",\n"
                << "  \"observed_workers\":"
                << receipt.distinct_participants << ",\n"
                << "  \"evaluation_repeats\":"
                << arguments.evaluation_repeats << ",\n"
                << "  \"total_seconds\":" << seconds << ",\n"
                << "  \"seconds_per_evaluation\":"
                << seconds
                    / static_cast<double>(arguments.evaluation_repeats)
                << "\n}\n";
            emit(out.str(), arguments.output);
            return 0;
        }

        std::vector<core99::t11::RunResult> runs(
            static_cast<std::size_t>(arguments.runs)
        );
        int campaign_observed_workers = 0;
        const auto campaign_start = std::chrono::steady_clock::now();
        if (arguments.runs == 1) {
            runs[0] = core99::t11::run(problem, {
                .algorithm_id = arguments.algorithm,
                .seed = arguments.seed,
                .physical_fes = arguments.physical_fes,
                .workers = arguments.workers
            });
            campaign_observed_workers = runs[0].observed_workers;
        } else {
            fode::PersistentExecutor campaign(arguments.workers);
            campaign.reset_work_receipt();
            campaign.parallel_for(0, arguments.runs, [&](const int index) {
                runs[static_cast<std::size_t>(index)] =
                    core99::t11::run(problem, {
                        .algorithm_id = arguments.algorithm,
                        .seed = arguments.seed
                            + static_cast<std::uint64_t>(index),
                        .physical_fes = arguments.physical_fes,
                        .workers = 1
                    });
            });
            campaign_observed_workers =
                campaign.work_receipt().distinct_participants;
        }
        const double campaign_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - campaign_start
        ).count();
        std::ostringstream out;
        out << std::setprecision(17)
            << "{\n"
            << "  \"mode\":\"optimization_campaign\",\n"
            << "  \"problem_id\":\"" << problem.id() << "\",\n"
            << "  \"algorithm_id\":\"" << arguments.algorithm << "\",\n"
            << "  \"runs\":" << arguments.runs << ",\n"
            << "  \"requested_workers\":" << arguments.workers << ",\n"
            << "  \"campaign_observed_workers\":"
            << campaign_observed_workers << ",\n"
            << "  \"campaign_seconds\":" << campaign_seconds << ",\n"
            << "  \"run_receipts\":[\n";
        for (std::size_t index = 0; index < runs.size(); ++index) {
            out << "    " << run_json(runs[index]);
            if (index + 1U != runs.size()) out << ',';
            out << '\n';
        }
        out << "  ]\n}\n";
        emit(out.str(), arguments.output);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "core99_t11_hpc: " << error.what() << '\n';
        return 1;
    }
}
