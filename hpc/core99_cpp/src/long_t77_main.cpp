/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T77 ADE-GRNN command-line production driver
Paper title: A Data-Driven Evolutionary Algorithm for Wind Farm Layout
Optimization
Paper DOI: 10.1016/j.energy.2020.118310
Public source: no paper-linked author code or data archive found.
Missing, conflicts, reconstruction, semantic identities, HPC route, and claim
boundary: include/core99/long_t77.hpp.
Contract: shared/contracts/core99_t77_long_ade_grnn_2020.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/long_t77.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void evaluation(
    std::ostream& out,
    const core99::t77::Evaluation& value
) {
    out << "{\"expected_power_kw\":" << std::setprecision(17)
        << value.expected_power_kw
        << ",\"constraint_violation_m\":"
        << value.constraint_violation_m << "}";
}

void result(std::ostream& out, const core99::t77::RunResult& run) {
    out << "{\"problem_semantic_id\":\""
        << run.problem_semantic_id
        << "\",\"method_semantic_id\":\""
        << run.method_semantic_id
        << "\",\"protocol_semantic_id\":\""
        << run.protocol_semantic_id
        << "\",\"scenario\":" << run.scenario
        << ",\"turbine_count\":" << run.turbine_count
        << std::setprecision(17)
        << ",\"farm_side_m\":" << run.farm_side_m
        << ",\"seed\":" << run.seed
        << ",\"generations\":" << run.generations
        << ",\"candidate_proposals\":" << run.candidate_proposals
        << ",\"physical_exact_fes\":" << run.physical_exact_fes
        << ",\"surrogate_inferences\":" << run.surrogate_inferences
        << ",\"requested_workers\":" << run.requested_workers
        << ",\"observed_workers\":" << run.observed_workers
        << ",\"initial_best_power_kw\":" << run.initial_best_power_kw
        << ",\"best_evaluation\":";
    evaluation(out, run.best_evaluation);
    out << ",\"best_layout\":[";
    for (std::size_t i = 0; i < run.best_layout.size(); ++i) {
        if (i) out << ",";
        out << "[" << run.best_layout[i].x_m << ","
            << run.best_layout[i].y_m << "]";
    }
    out << "],\"best_history_kw\":[";
    for (std::size_t i = 0; i < run.best_history_kw.size(); ++i) {
        if (i) out << ",";
        out << run.best_history_kw[i];
    }
    out << "],\"final_mean_f1\":" << run.final_mean_f1
        << ",\"final_mean_f2\":" << run.final_mean_f2
        << ",\"final_mean_moved_turbines\":"
        << run.final_mean_moved_turbines
        << ",\"exact_evaluator_seconds\":"
        << run.exact_evaluator_seconds
        << ",\"surrogate_seconds\":" << run.surrogate_seconds
        << ",\"operator_seconds\":" << run.operator_seconds
        << ",\"end_to_end_seconds\":" << run.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex
        << run.scientific_hash << std::dec << "\"}";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        int scenario = 1;
        int turbines = 25;
        std::uint64_t seed = 2020118310ULL;
        std::string output;
        core99::t77::RunConfig config;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            auto next = [&]() -> std::string {
                if (++i >= argc) throw std::invalid_argument("missing value");
                return argv[i];
            };
            if (argument == "--case") {
                const std::string value = next();
                const auto split = value.find("_n");
                if (!value.starts_with("t77_ws") || split == std::string::npos) {
                    throw std::invalid_argument("invalid T77 case");
                }
                scenario = std::stoi(value.substr(6, split - 6));
                turbines = std::stoi(value.substr(split + 2));
            } else if (argument == "--scenario") {
                scenario = std::stoi(next());
            } else if (argument == "--turbines") {
                turbines = std::stoi(next());
            } else if (argument == "--seed") {
                seed = std::stoull(next());
            } else if (argument == "--workers") {
                config.workers = std::stoi(next());
            } else if (argument == "--generations") {
                config.generations = std::stoi(next());
            } else if (argument == "--stage1-generations") {
                config.exact_stage_generations = std::stoi(next());
            } else if (argument == "--output") {
                output = next();
            } else if (argument == "--mode") {
                (void)next();
            } else {
                throw std::invalid_argument(
                    "unknown argument " + argument
                );
            }
        }
        config.training_capacity =
            config.population * config.exact_stage_generations;
        const core99::t77::Problem problem(scenario, turbines);
        const auto run = core99::t77::run(problem, seed, config);
        std::ostringstream payload;
        payload << "{\"schema_version\":1,\"corpus_id\":\"T77\","
                << "\"runs\":[";
        result(payload, run);
        payload << "]}";
        if (output.empty()) {
            std::cout << payload.str() << "\n";
        } else {
            std::ofstream stream(output);
            if (!stream) throw std::runtime_error("cannot open output");
            stream << payload.str() << "\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "core99_t77_hpc: " << error.what() << "\n";
        return 2;
    }
}
