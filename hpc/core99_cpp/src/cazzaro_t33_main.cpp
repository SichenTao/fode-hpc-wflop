/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T33 pure-C++ paper-case CLI
Paper/DOI: Combined Layout and Cable Optimization of Offshore Wind Farms;
10.1016/j.ejor.2023.04.046
Public source: official dataset DOI 10.11583/DTU.13134731; no target code.
Missing/conflict/reconstruction completion, semantic IDs, HPC facts, and
claim boundary:
include/core99/cazzaro_t33.hpp.
Contract: shared/contracts/core99_t33_cazzaro_combined_2023.json.
Claim boundary: declared academic reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/cazzaro_t33.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string mode = "optimize";
    std::filesystem::path data_root;
    std::string case_name = "a_low";
    std::filesystem::path matrix_cache;
    std::filesystem::path output;
    std::uint64_t seed = 20260731;
    int workers = 20;
    std::uint64_t cycles = 0;
    double seconds = 0.0;
};

Arguments parse_arguments(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument("T33 missing value for " + key);
            }
            return std::string(argv[index]);
        };
        if (key == "--mode") result.mode = value();
        else if (key == "--data-root") result.data_root = value();
        else if (key == "--case") result.case_name = value();
        else if (key == "--matrix-cache") result.matrix_cache = value();
        else if (key == "--output") result.output = value();
        else if (key == "--seed") {
            result.seed = std::stoull(value());
        } else if (key == "--workers") {
            result.workers = std::stoi(value());
        } else if (key == "--cycles") {
            result.cycles = std::stoull(value());
        } else if (key == "--seconds") {
            result.seconds = std::stod(value());
        } else {
            throw std::invalid_argument("T33 unknown option " + key);
        }
    }
    return result;
}

std::pair<char, core99::t33::Density> parse_case(
    std::string value
) {
    const std::string prefix = "t33_official_";
    if (value.starts_with(prefix)) value.erase(0, prefix.size());
    if (
        value.size() < 5
        || value[0] < 'a' || value[0] > 'j'
        || value[1] != '_'
    ) {
        throw std::invalid_argument("T33 case must be a_low ... j_high");
    }
    const char site = static_cast<char>(value[0] - 'a' + 'A');
    const std::string density = value.substr(2);
    if (density == "low") {
        return {site, core99::t33::Density::low};
    }
    if (density == "high") {
        return {site, core99::t33::Density::high};
    }
    throw std::invalid_argument("T33 density must be low or high");
}

std::string vector_json(const std::vector<int>& values) {
    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) output << ',';
        output << values[index];
    }
    output << ']';
    return output.str();
}

std::string double_vector_json(const std::vector<double>& values) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) output << ',';
        output << values[index];
    }
    output << ']';
    return output.str();
}

std::string evaluation_json(const core99::t33::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"aep_mwh\":" << value.aep_mwh
        << ",\"lifetime_revenue_eur\":"
        << value.lifetime_revenue_eur
        << ",\"foundation_cost_eur\":"
        << value.foundation_cost_eur
        << ",\"cable_cost_eur\":" << value.cable_cost_eur
        << ",\"npv_eur\":" << value.npv_eur
        << ",\"spacing_violation_m\":"
        << value.spacing_violation_m
        << ",\"cable_crossings\":" << value.cable_crossings
        << ",\"feasible\":"
        << (value.feasible ? "true" : "false") << '}';
    return output.str();
}

std::string run_json(const core99::t33::RunResult& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"case_id\":\"" << value.case_id
        << "\",\"problem_semantic_id\":\""
        << value.problem_semantic_id
        << "\",\"method_semantic_id\":\""
        << value.method_semantic_id
        << "\",\"protocol_semantic_id\":\""
        << value.protocol_semantic_id
        << "\",\"seed\":" << value.seed
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"completed_vns_cycles\":"
        << value.completed_vns_cycles
        << ",\"matrix_pair_evaluations\":"
        << value.matrix_pair_evaluations
        << ",\"wake_state_evaluations\":"
        << value.wake_state_evaluations
        << ",\"layout_candidate_evaluations\":"
        << value.layout_candidate_evaluations
        << ",\"cable_route_evaluations\":"
        << value.cable_route_evaluations
        << ",\"initial\":" << evaluation_json(value.initial)
        << ",\"best\":" << evaluation_json(value.best)
        << ",\"best_positions\":" << vector_json(value.best_positions)
        << ",\"best_npv_history_eur\":"
        << double_vector_json(value.best_npv_history_eur)
        << ",\"problem_preprocessing_seconds\":"
        << value.problem_preprocessing_seconds
        << ",\"matrix_seconds\":" << value.matrix_seconds
        << ",\"initialization_seconds\":"
        << value.initialization_seconds
        << ",\"cable_seconds\":" << value.cable_seconds
        << ",\"candidate_seconds\":" << value.candidate_seconds
        << ",\"optimization_seconds\":"
        << value.optimization_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":" << value.scientific_hash << '}';
    return output.str();
}

void emit(
    const std::string& payload,
    const std::filesystem::path& output
) {
    if (output.empty()) {
        std::cout << payload << '\n';
        return;
    }
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    std::ofstream stream(output);
    if (!stream) throw std::runtime_error("T33 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse_arguments(argc, argv);
        if (arguments.mode == "list-cases") {
            std::ostringstream output;
            output << "{\"cases\":[";
            const auto cases = core99::t33::paper_case_ids();
            for (std::size_t index = 0; index < cases.size(); ++index) {
                if (index > 0) output << ',';
                output << '"' << cases[index] << '"';
            }
            output << "]}";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.data_root.empty()) {
            throw std::invalid_argument("T33 --data-root is required");
        }
        const auto [site, density] = parse_case(arguments.case_name);
        const core99::t33::Problem problem(
            arguments.data_root, site, density, arguments.workers
        );
        if (arguments.mode == "inspect") {
            const auto& info = problem.info();
            std::ostringstream output;
            output << "{\"case_id\":\"" << info.case_id
                << "\",\"site\":\"" << info.site
                << "\",\"density\":\""
                << core99::t33::density_name(info.density)
                << "\",\"available_positions\":"
                << info.available_positions
                << ",\"fixed_turbines\":" << info.fixed_turbines
                << ",\"turbines\":" << info.turbines
                << ",\"zones\":" << info.zones
                << ",\"wind_states\":" << info.wind_states
                << ",\"zone_quotas\":"
                << vector_json(info.zone_quotas)
                << ",\"energy_price_factor_eur_per_mwh\":"
                << problem.energy_price_factor_eur_per_mwh()
                << ",\"paper_fixed_cycles\":"
                << problem.paper_fixed_cycles()
                << ",\"paper_time_limit_seconds\":"
                << problem.paper_time_limit_seconds() << '}';
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "evaluate") {
            const auto layout =
                problem.deterministic_reference_layout();
            std::ostringstream output;
            output << "{\"case_id\":\"" << problem.info().case_id
                << "\",\"evaluation\":"
                << evaluation_json(
                    problem.evaluate_direct(layout, arguments.workers)
                )
                << ",\"positions\":" << vector_json(layout) << '}';
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument(
                "T33 mode must be list-cases, inspect, evaluate, or optimize"
            );
        }
        core99::t33::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.fixed_vns_cycles = arguments.cycles;
        config.time_limit_seconds = arguments.seconds;
        config.matrix_cache = arguments.matrix_cache;
        emit(
            run_json(core99::t33::run(problem, config)),
            arguments.output
        );
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T33 failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
