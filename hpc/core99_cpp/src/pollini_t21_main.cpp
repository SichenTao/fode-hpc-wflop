/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T21 pure-C++ CPU-HPC command-line driver and receipts
Paper/DOI: Topology Optimization of Wind Farm Layouts;
10.1016/j.renene.2022.06.019
Public source:
https://github.com/byuflowlab/iea37-wflo-casestudies revision
af88908d22795030ac2dfbe37bc38e912aee8ed6
Missing/conflicts/completion:
hpc/core99_cpp/include/core99/pollini_t21.hpp
Independent oracle: scripts/validate_core99_t21.py
HPC design: pure C++20, immutable wake-deficit precomputation, persistent
full-core objective/gradient evaluation, deterministic task reduction, and
pinned NLopt LD_MMA; no Python is present in the production path
Method/problem semantic IDs: t21_ramp_mma_declared_reconstruction_v1;
t21_pollini_two_circle_density_wflop_v1
Controlling contract: shared/contracts/core99_t21_pollini_2022.json
Claim boundary: academic declared reconstruction, not author MATLAB/MMA replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/pollini_t21.hpp"

#include "fode/executor.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Arguments {
    std::string case_name = "small";
    std::string output;
    std::uint64_t seed = 20260731;
    int workers = 20;
    int maximum_evaluations = 1000;
    int runs = 1;
    int evaluation_repeats = 1;
    double evaluation_density = -1.0;
    bool linear_interpolation = false;
};

Arguments parse(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() -> std::string {
            if (++index >= argc) {
                throw std::invalid_argument("missing value for " + flag);
            }
            return argv[index];
        };
        if (flag == "--case") {
            result.case_name = value();
        } else if (flag == "--output") {
            result.output = value();
        } else if (flag == "--seed") {
            result.seed = std::stoull(value());
        } else if (flag == "--workers") {
            result.workers = std::stoi(value());
        } else if (flag == "--maximum-evaluations") {
            result.maximum_evaluations = std::stoi(value());
        } else if (flag == "--runs") {
            result.runs = std::stoi(value());
        } else if (flag == "--evaluation-repeats") {
            result.evaluation_repeats = std::stoi(value());
        } else if (flag == "--evaluate-density") {
            result.evaluation_density = std::stod(value());
        } else if (flag == "--linear-interpolation") {
            result.linear_interpolation = true;
        } else {
            throw std::invalid_argument("unknown flag: " + flag);
        }
    }
    if (
        (result.case_name != "small" && result.case_name != "large")
        || result.workers <= 0
        || result.maximum_evaluations <= 0
        || result.runs <= 0
        || result.evaluation_repeats <= 0
        || result.evaluation_density > 1.0
    ) {
        throw std::invalid_argument("invalid T21 command-line configuration");
    }
    return result;
}

core99::t21::CaseId case_id(const std::string& name) {
    return name == "small"
        ? core99::t21::CaseId::radius_1300
        : core99::t21::CaseId::radius_3000;
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

std::string run_json(const core99::t21::RunResult& run) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{"
        << "\"problem_id\":\"" << run.problem_id << "\","
        << "\"problem_semantic_id\":\""
        << run.problem_semantic_id << "\","
        << "\"method_semantic_id\":\""
        << run.method_semantic_id << "\","
        << "\"optimizer_backend\":\""
        << run.optimizer_backend << "\","
        << "\"seed\":" << run.seed << ','
        << "\"start_index\":" << run.start_index << ','
        << "\"requested_workers\":" << run.requested_workers << ','
        << "\"observed_workers\":" << run.observed_workers << ','
        << "\"potential_sites\":" << run.potential_sites << ','
        << "\"discrete_turbines\":" << run.discrete_turbines << ','
        << "\"objective_evaluations\":"
        << run.objective_evaluations << ','
        << "\"gradient_evaluations\":"
        << run.gradient_evaluations << ','
        << "\"optimizer_status\":" << run.optimizer_status << ','
        << "\"optimizer_status_name\":\""
        << run.optimizer_status_name << "\","
        << "\"initial_q\":" << run.initial_q << ','
        << "\"q_increment\":" << run.q_increment << ','
        << "\"final_q\":" << run.final_q << ','
        << "\"relaxed_aep_gwh\":" << run.relaxed_aep_gwh << ','
        << "\"discrete_aep_gwh\":" << run.discrete_aep_gwh << ','
        << "\"relaxed_constraint_violation\":"
        << run.relaxed_constraint_violation << ','
        << "\"evaluator_seconds\":" << run.evaluator_seconds << ','
        << "\"optimizer_seconds\":" << run.optimizer_seconds << ','
        << "\"end_to_end_seconds\":" << run.end_to_end_seconds << ','
        << "\"scientific_hash\":\"" << std::hex
        << run.scientific_hash << std::dec << "\","
        << "\"densities\":" << vector_json(run.densities)
        << '}';
    return out.str();
}

std::string evaluate(
    const core99::t21::Problem& problem,
    const Arguments& args,
    double preprocessing_seconds
) {
    const std::vector<double> densities(
        static_cast<std::size_t>(problem.potential_sites()),
        args.evaluation_density
    );
    fode::PersistentExecutor executor(args.workers);
    executor.reset_work_receipt();
    core99::t21::Evaluation receipt;
    const auto started = Clock::now();
    for (int repeat = 0; repeat < args.evaluation_repeats; ++repeat) {
        receipt = problem.evaluate(
            densities,
            0.0,
            executor,
            true
        );
    }
    const double elapsed = std::chrono::duration<double>(
        Clock::now() - started
    ).count();
    double gradient_norm_squared = 0.0;
    for (const double value : receipt.objective_gradient) {
        gradient_norm_squared += value * value;
    }
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\n"
        << "  \"mode\":\"fixed_density_evaluation\",\n"
        << "  \"problem_id\":\"" << problem.id() << "\",\n"
        << "  \"problem_semantic_id\":"
           "\"t21_pollini_two_circle_density_wflop_v1\",\n"
        << "  \"density\":" << args.evaluation_density << ",\n"
        << "  \"potential_sites\":" << problem.potential_sites() << ",\n"
        << "  \"spacing_pairs\":" << problem.spacing_pairs().size() << ",\n"
        << "  \"aep_gwh\":" << receipt.aep_gwh << ",\n"
        << "  \"objective\":" << receipt.objective << ",\n"
        << "  \"minimum_count_constraint\":"
        << receipt.minimum_count_constraint << ",\n"
        << "  \"maximum_count_constraint\":"
        << receipt.maximum_count_constraint << ",\n"
        << "  \"maximum_spacing_constraint\":"
        << receipt.maximum_spacing_constraint << ",\n"
        << "  \"objective_gradient\":"
        << vector_json(receipt.objective_gradient) << ",\n"
        << "  \"objective_gradient_l2_squared\":"
        << gradient_norm_squared << ",\n"
        << "  \"requested_workers\":" << args.workers << ",\n"
        << "  \"observed_workers\":"
        << executor.work_receipt().distinct_participants << ",\n"
        << "  \"evaluation_repeats\":"
        << args.evaluation_repeats << ",\n"
        << "  \"preprocessing_seconds\":"
        << preprocessing_seconds << ",\n"
        << "  \"evaluator_seconds\":" << elapsed << ",\n"
        << "  \"seconds_per_evaluation\":"
        << elapsed / args.evaluation_repeats << "\n"
        << "}\n";
    return out.str();
}

std::string optimize(
    const core99::t21::Problem& problem,
    const Arguments& args,
    double preprocessing_seconds
) {
    std::vector<core99::t21::RunResult> runs(
        static_cast<std::size_t>(args.runs)
    );
    const auto campaign_started = Clock::now();
    const int run_parallelism = std::min(args.workers, args.runs);
    const int workers_per_run = std::max(
        1,
        args.workers / run_parallelism
    );
    auto execute_run = [&](int index) {
        core99::t21::RunConfig config;
        config.seed = args.seed;
        config.start_index = index;
        config.workers = workers_per_run;
        config.maximum_objective_evaluations =
            args.maximum_evaluations;
        config.random_start = index != 0;
        config.linear_interpolation = args.linear_interpolation;
        runs[static_cast<std::size_t>(index)] =
            core99::t21::run(problem, config);
    };
    int campaign_observed_workers = 1;
    if (run_parallelism == 1) {
        for (int index = 0; index < args.runs; ++index) {
            execute_run(index);
        }
    } else {
        fode::PersistentExecutor campaign_executor(run_parallelism);
        campaign_executor.reset_work_receipt();
        campaign_executor.parallel_for(0, args.runs, execute_run);
        campaign_observed_workers =
            campaign_executor.work_receipt().distinct_participants;
    }
    const double campaign_seconds = std::chrono::duration<double>(
        Clock::now() - campaign_started
    ).count();
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\n"
        << "  \"mode\":\"paper_target_campaign\",\n"
        << "  \"corpus_id\":\"T21\",\n"
        << "  \"problem_id\":\"" << problem.id() << "\",\n"
        << "  \"problem_semantic_id\":"
           "\"t21_pollini_two_circle_density_wflop_v1\",\n"
        << "  \"method_semantic_id\":"
           "\"t21_ramp_mma_declared_reconstruction_v1\",\n"
        << "  \"optimizer_backend\":"
           "\"nlopt_ld_mma_declared_replacement\",\n"
        << "  \"linear_interpolation\":"
        << (args.linear_interpolation ? "true" : "false") << ",\n"
        << "  \"paper_protocol_runs\":" << args.runs << ",\n"
        << "  \"requested_workers\":" << args.workers << ",\n"
        << "  \"run_parallelism\":" << run_parallelism << ",\n"
        << "  \"workers_per_run\":" << workers_per_run << ",\n"
        << "  \"campaign_observed_workers\":"
        << campaign_observed_workers << ",\n"
        << "  \"maximum_objective_evaluations_per_run\":"
        << args.maximum_evaluations << ",\n"
        << "  \"preprocessing_seconds\":"
        << preprocessing_seconds << ",\n"
        << "  \"campaign_seconds\":" << campaign_seconds << ",\n"
        << "  \"runs\":[\n";
    for (std::size_t index = 0; index < runs.size(); ++index) {
        if (index != 0) {
            out << ",\n";
        }
        out << "    " << run_json(runs[index]);
    }
    out << "\n  ]\n}\n";
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
        const auto preprocessing_started = Clock::now();
        const core99::t21::Problem problem(
            case_id(args.case_name),
            args.workers
        );
        const double preprocessing_seconds =
            std::chrono::duration<double>(
                Clock::now() - preprocessing_started
            ).count();
        const std::string payload = args.evaluation_density >= 0.0
            ? evaluate(problem, args, preprocessing_seconds)
            : optimize(problem, args, preprocessing_seconds);
        emit(payload, args.output);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "T21 failure: " << error.what() << '\n';
        return 2;
    }
}
