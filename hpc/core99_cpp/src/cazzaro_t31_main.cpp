/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T31 pure-C++ official-data VNS command-line driver
Paper DOI: 10.1016/j.cor.2021.105588
Dataset DOI: 10.11583/DTU.13134731.
Source/missing/completions/semantics/HPC/claim boundary:
include/core99/cazzaro_t31.hpp.
Contract: shared/contracts/core99_t31_cazzaro_vns_2022.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/cazzaro_t31.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void evaluation(
    std::ostream& out,
    const core99::t31::Evaluation& value
) {
    out << std::setprecision(17)
        << "{\"objective_mwh_equivalent\":"
        << value.objective_mwh_equivalent
        << ",\"aep_mwh\":" << value.aep_mwh
        << ",\"foundation_cost_eur\":" << value.foundation_cost_eur
        << ",\"spacing_violation_m\":" << value.spacing_violation_m
        << "}";
}

void result(std::ostream& out, const core99::t31::RunResult& run) {
    out << "{\"case_id\":\"" << run.case_id
        << "\",\"problem_semantic_id\":\""
        << run.problem_semantic_id
        << "\",\"method_semantic_id\":\""
        << run.method_semantic_id
        << "\",\"protocol_semantic_id\":\""
        << run.protocol_semantic_id
        << "\",\"foundation_mode\":\""
        << core99::t31::foundation_mode_name(run.foundation_mode)
        << "\",\"shake_mode\":\""
        << core99::t31::shake_mode_name(run.shake_mode)
        << "\",\"seed\":" << run.seed
        << ",\"requested_workers\":" << run.requested_workers
        << ",\"observed_workers\":" << run.observed_workers
        << ",\"completed_vns_iterations\":"
        << run.completed_vns_iterations
        << ",\"matrix_pair_evaluations\":"
        << run.matrix_pair_evaluations
        << ",\"wake_state_evaluations\":"
        << run.wake_state_evaluations
        << ",\"delta_candidate_evaluations\":"
        << run.delta_candidate_evaluations
        << ",\"initial\":";
    evaluation(out, run.initial);
    out << ",\"best\":";
    evaluation(out, run.best);
    out << ",\"best_positions\":[";
    for (std::size_t index = 0; index < run.best_positions.size(); ++index) {
        if (index) out << ",";
        out << run.best_positions[index];
    }
    out << "],\"best_history_mwh\":[";
    for (std::size_t index = 0;
         index < run.best_history_mwh.size();
         ++index) {
        if (index) out << ",";
        out << run.best_history_mwh[index];
    }
    out << "],\"time_checkpoints\":[";
    for (std::size_t index = 0;
         index < run.time_checkpoints.size();
         ++index) {
        if (index) out << ",";
        const auto& checkpoint = run.time_checkpoints[index];
        out << std::setprecision(17)
            << "{\"target_seconds\":" << checkpoint.target_seconds
            << ",\"observed_seconds\":" << checkpoint.observed_seconds
            << ",\"best_objective_mwh_equivalent\":"
            << checkpoint.best_objective_mwh_equivalent << "}";
    }
    out << std::setprecision(17)
        << "],\"problem_preprocessing_seconds\":"
        << run.problem_preprocessing_seconds
        << ",\"matrix_seconds\":" << run.matrix_seconds
        << ",\"initialization_seconds\":" << run.initialization_seconds
        << ",\"shake_seconds\":" << run.shake_seconds
        << ",\"local_search_seconds\":" << run.local_search_seconds
        << ",\"optimization_seconds\":" << run.optimization_seconds
        << ",\"end_to_end_seconds\":" << run.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex
        << run.scientific_hash << std::dec << "\"}";
}

core99::t31::FoundationMode parse_mode(const std::string& value) {
    if (value == "none") return core99::t31::FoundationMode::none;
    if (value == "low_cost") {
        return core99::t31::FoundationMode::low_cost;
    }
    if (value == "high_cost") {
        return core99::t31::FoundationMode::high_cost;
    }
    throw std::invalid_argument("invalid foundation mode");
}

core99::t31::ShakeMode parse_shake_mode(const std::string& value) {
    using Mode = core99::t31::ShakeMode;
    if (value == "circular") return Mode::circular;
    if (value == "conic") return Mode::conic;
    if (value == "directional") return Mode::directional;
    if (value == "displacement") return Mode::displacement;
    if (value == "random") return Mode::random;
    if (value == "random_directional") return Mode::random_directional;
    if (value == "circular_displacement") {
        return Mode::circular_displacement;
    }
    if (value == "random_conic") return Mode::random_conic;
    if (value == "directional_conic") return Mode::directional_conic;
    if (value == "all") return Mode::all;
    throw std::invalid_argument("invalid shake mode");
}

std::vector<double> parse_checkpoints(const std::string& value) {
    std::vector<double> result;
    std::size_t start = 0;
    while (start < value.size()) {
        const std::size_t end = value.find(',', start);
        result.push_back(std::stod(value.substr(start, end - start)));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string case_id = "t31_mosetti_di";
        std::string data_root;
        std::string output;
        std::uint64_t seed = 202231105588ULL;
        core99::t31::RunConfig config;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            auto next = [&]() -> std::string {
                if (++index >= argc) {
                    throw std::invalid_argument("missing argument value");
                }
                return argv[index];
            };
            if (argument == "--case") case_id = next();
            else if (argument == "--data-root") data_root = next();
            else if (argument == "--seed") seed = std::stoull(next());
            else if (argument == "--workers") {
                config.workers = std::stoi(next());
            } else if (argument == "--time-seconds") {
                config.time_limit_seconds = std::stod(next());
            } else if (argument == "--iterations") {
                config.fixed_iterations = std::stoull(next());
            } else if (argument == "--foundation-mode") {
                config.foundation_mode = parse_mode(next());
            } else if (argument == "--shake-mode") {
                config.shake_mode = parse_shake_mode(next());
            } else if (argument == "--matrix-cache") {
                config.matrix_cache = next();
            } else if (argument == "--checkpoint-seconds") {
                config.checkpoint_seconds = parse_checkpoints(next());
            } else if (argument == "--output") {
                output = next();
            } else if (argument == "--mode") {
                const std::string mode = next();
                if (mode != "optimize") {
                    throw std::invalid_argument("unsupported T31 mode");
                }
            } else {
                throw std::invalid_argument(
                    "unknown argument " + argument
                );
            }
        }
        if (
            case_id.starts_with("t31_official_")
            && data_root.empty()
        ) {
            throw std::invalid_argument(
                "official T31 case requires --data-root"
            );
        }
        const core99::t31::Problem problem(
            data_root,
            case_id,
            config.foundation_mode,
            config.workers
        );
        const auto run = core99::t31::run(problem, seed, config);
        std::ostringstream payload;
        payload << "{\"schema_version\":1,\"corpus_id\":\"T31\","
                << "\"problem_info\":{\"available_positions\":"
                << problem.info().available_positions
                << ",\"fixed_turbines\":" << problem.info().fixed_turbines
                << ",\"wind_states\":" << problem.info().wind_states
                << ",\"zone_quotas\":[";
        for (std::size_t index = 0;
             index < problem.info().zone_quotas.size();
             ++index) {
            if (index) payload << ",";
            payload << problem.info().zone_quotas[index];
        }
        payload << "]},\"runs\":[";
        result(payload, run);
        payload << "]}";
        if (output.empty()) {
            std::cout << payload.str() << "\n";
        } else {
            std::ofstream stream(output);
            if (!stream) {
                throw std::runtime_error("cannot open T31 output");
            }
            stream << payload.str() << "\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "core99_t31_hpc: " << error.what() << "\n";
        return 2;
    }
}
