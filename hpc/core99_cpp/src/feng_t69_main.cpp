/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T69 pure-C++ CPU-HPC command line
Paper/DOI: Feng and Shen; 10.1016/j.enconman.2017.06.005
Facts, missing fields, conflicts and completions: include/core99/feng_t69.hpp
Claim boundary: flexible academic reconstruction, not author numeric replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/feng_t69.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Arguments {
    std::string mode = "optimize";
    core99::t69::RunConfig config;
    std::uint64_t scenario_seed = 69000;
    int scenarios = 1000;
    std::filesystem::path output;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) throw std::invalid_argument("T69 missing " + key);
            return std::string(argv[index]);
        };
        if (key == "--mode") result.mode = value();
        else if (key == "--study") {
            const std::string item = value();
            if (item == "short") result.config.study = core99::t69::Study::short_term;
            else if (item == "long") result.config.study = core99::t69::Study::long_term;
            else if (item == "overall") result.config.study = core99::t69::Study::overall;
            else throw std::invalid_argument("T69 study short/long/overall");
        } else if (key == "--profile") {
            const std::string item = value();
            if (item == "equation_declared") {
                result.config.conflict_profile =
                    core99::t69::ConflictProfile::equation_declared;
            } else if (item == "table3_compatible") {
                result.config.conflict_profile =
                    core99::t69::ConflictProfile::table3_compatible;
            } else throw std::invalid_argument("T69 profile unknown");
        } else if (key == "--weight") result.config.weight = std::stod(value());
        else if (key == "--alpha") result.config.alpha = std::stod(value());
        else if (key == "--beta") result.config.beta = std::stod(value());
        else if (key == "--workers") result.config.workers = std::stoi(value());
        else if (key == "--seed") result.config.seed = std::stoull(value());
        else if (key == "--fes") result.config.physical_fes = std::stoull(value());
        else if (key == "--scenario-seed") result.scenario_seed = std::stoull(value());
        else if (key == "--scenarios") result.scenarios = std::stoi(value());
        else if (key == "--output") result.output = value();
        else throw std::invalid_argument("T69 unknown option " + key);
    }
    return result;
}

std::string evaluation_json(const core99::t69::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"mean_power_mw\":" << value.mean_power_mw
        << ",\"aep_mwh_paper_8770\":" << value.aep_mwh_paper_8770
        << ",\"aep_mwh_calendar_8760\":" << value.aep_mwh_calendar_8760
        << ",\"variability_of_power\":" << value.variability_of_power
        << ",\"long_term_mean_mw\":" << value.long_term_mean_mw
        << ",\"long_term_std_mw\":" << value.long_term_std_mw
        << ",\"short_robustness\":" << value.short_robustness
        << ",\"long_robustness\":" << value.long_robustness
        << ",\"table3_compatible_long_robustness\":"
        << value.table3_compatible_long_robustness
        << ",\"overall_robustness\":" << value.overall_robustness
        << ",\"feasible\":" << (value.feasible ? "true" : "false") << '}';
    return out.str();
}

std::string layout_json(const std::vector<core99::t69::Point>& values) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) out << ',';
        out << "[" << values[index].x_m << ',' << values[index].y_m << ']';
    }
    return out.str() + ']';
}

std::string result_json(const core99::t69::RunResult& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"case_id\":\"" << value.case_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"protocol_semantic_id\":\"" << value.protocol_semantic_id
        << "\",\"study\":\"" << value.study
        << "\",\"conflict_profile\":\"" << value.conflict_profile
        << "\",\"weight\":" << value.weight
        << ",\"effective_alpha\":" << value.effective_alpha
        << ",\"effective_beta\":" << value.effective_beta
        << ",\"effective_gamma\":" << value.effective_gamma
        << ",\"seed\":" << value.seed
        << ",\"physical_fes\":" << value.physical_fes
        << ",\"infeasible_proposals\":" << value.infeasible_proposals
        << ",\"accepted_moves\":" << value.accepted_moves
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"parallel_regions\":" << value.parallel_regions
        << ",\"reference\":" << evaluation_json(value.reference)
        << ",\"final_evaluation\":" << evaluation_json(value.final_evaluation)
        << ",\"final_layout\":" << layout_json(value.final_layout)
        << ",\"wind_scenario_precompute_seconds\":"
        << value.wind_scenario_precompute_seconds
        << ",\"wake_update_seconds\":" << value.wake_update_seconds
        << ",\"robustness_metric_seconds\":"
        << value.robustness_metric_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex << value.scientific_hash
        << std::dec << "\"}";
    return out.str();
}

void emit(const std::string& payload, const std::filesystem::path& output) {
    if (output.empty()) {
        std::cout << payload << '\n';
        return;
    }
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    std::ofstream stream(output);
    if (!stream) throw std::runtime_error("T69 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            emit("{\"studies\":[\"short\",\"long\",\"overall\"],"
                 "\"weights\":[0,0.05,0.5,0.95,1],"
                 "\"paper_native_cases\":15,"
                 "\"conflict_profiles\":[\"equation_declared\","
                 "\"table3_compatible\"]}", arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument("T69 mode list-cases/optimize");
        }
        const core99::t69::Problem problem(
            arguments.config.workers,
            arguments.scenario_seed,
            arguments.scenarios);
        emit(result_json(core99::t69::run(problem, arguments.config)),
             arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T69 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
