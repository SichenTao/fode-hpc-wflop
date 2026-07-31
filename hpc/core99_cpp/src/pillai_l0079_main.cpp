/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0079 pure-C++ CPU-HPC command line
Paper/DOI: Pillai et al.; 10.1016/j.oceaneng.2017.04.049
Public asset and source provenance: target repository, thesis and pinned data
are declared in include/core99/pillai_l0079.hpp.
Facts, assets, omissions, conflicts and completions:
include/core99/pillai_l0079.hpp
Method semantic IDs: l0079_adaptive_ga_three_encoding_declared_v1;
l0079_gbest_pso_three_encoding_declared_v1.
Controlling contract:
shared/contracts/core99_l0079_pillai_middelgrunden_2017.json
Claim boundary: flexible academic reproduction, not author numeric replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/pillai_l0079.hpp"

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
    std::string action = "optimize";
    core99::l0079::RunConfig config;
    std::filesystem::path output;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) throw std::invalid_argument("L0079 missing " + key);
            return std::string(argv[index]);
        };
        if (key == "--action") result.action = value();
        else if (key == "--optimizer") {
            const std::string item = value();
            if (item == "ga") result.config.optimizer = core99::l0079::Optimizer::adaptive_ga;
            else if (item == "pso") result.config.optimizer = core99::l0079::Optimizer::gbest_pso;
            else throw std::invalid_argument("L0079 optimizer ga/pso");
        } else if (key == "--constraint-mode") {
            const std::string item = value();
            if (item == "array") result.config.mode = core99::l0079::ConstraintMode::array;
            else if (item == "binary") result.config.mode = core99::l0079::ConstraintMode::binary;
            else if (item == "continuous") result.config.mode = core99::l0079::ConstraintMode::continuous;
            else throw std::invalid_argument("L0079 mode array/binary/continuous");
        } else if (key == "--candidate-profile") {
            const std::string item = value();
            if (item == "journal_628") {
                result.config.candidate_profile = core99::l0079::CandidateProfile::journal_628;
            } else if (item == "thesis_658") {
                result.config.candidate_profile = core99::l0079::CandidateProfile::thesis_658;
            } else throw std::invalid_argument("L0079 candidate profile unknown");
        } else if (key == "--seed") result.config.seed = std::stoull(value());
        else if (key == "--workers") result.config.workers = std::stoi(value());
        else if (key == "--population") result.config.population = std::stoi(value());
        else if (key == "--maximum-generations") {
            result.config.maximum_generations = std::stoi(value());
        } else if (key == "--no-improvement-generations") {
            result.config.no_improvement_generations = std::stoi(value());
        } else if (key == "--disable-convergence") {
            result.config.enable_convergence = false;
        } else if (key == "--output") result.output = value();
        else throw std::invalid_argument("L0079 unknown option " + key);
    }
    return result;
}

std::string evaluation_json(const core99::l0079::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"gross_aep_mwh_8766\":" << value.gross_aep_mwh_8766
        << ",\"net_aep_mwh_8766\":" << value.net_aep_mwh_8766
        << ",\"net_aep_mwh_8760\":" << value.net_aep_mwh_8760
        << ",\"cable_length_m\":" << value.cable_length_m
        << ",\"electrical_loss_fraction\":" << value.electrical_loss_fraction
        << ",\"lifetime_cost_gbp\":" << value.lifetime_cost_gbp
        << ",\"lcoe_gbp_per_mwh\":" << value.lcoe_gbp_per_mwh
        << ",\"minimum_spacing_m\":" << value.minimum_spacing_m
        << ",\"feasible\":" << (value.feasible ? "true" : "false") << '}';
    return out.str();
}

std::string layout_json(const std::vector<core99::l0079::Point>& values) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) out << ',';
        out << '[' << values[index].x_m << ',' << values[index].y_m << ']';
    }
    return out.str() + ']';
}

std::string result_json(const core99::l0079::RunResult& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"case_id\":\"" << value.case_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"protocol_semantic_id\":\"" << value.protocol_semantic_id
        << "\",\"optimizer\":\"" << value.optimizer
        << "\",\"constraint_mode\":\"" << value.constraint_mode
        << "\",\"candidate_profile\":\"" << value.candidate_profile
        << "\",\"seed\":" << value.seed
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"parallel_regions\":" << value.parallel_regions
        << ",\"population\":" << value.population
        << ",\"generations\":" << value.generations
        << ",\"physical_fes\":" << value.physical_fes
        << ",\"convergence_reason\":\"" << value.convergence_reason
        << "\",\"reference_evaluation\":"
        << evaluation_json(value.reference_evaluation)
        << ",\"best_evaluation\":" << evaluation_json(value.best_evaluation)
        << ",\"best_layout\":" << layout_json(value.best_layout)
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
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
    if (!stream) throw std::runtime_error("L0079 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        const core99::l0079::Problem problem(arguments.config.candidate_profile);
        if (arguments.action == "list-cases") {
            emit("{\"paper_native_roles\":[\"ga_array\",\"ga_binary\","
                 "\"ga_continuous\",\"pso_array\",\"pso_binary\","
                 "\"pso_continuous\"],\"native_repeats\":1,"
                 "\"population_or_swarm\":100,\"maximum_generations\":1000,"
                 "\"journal_candidates\":628,\"thesis_candidates\":658}",
                 arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action == "evaluate-reference") {
            const auto evaluation = problem.evaluate_layout(problem.as_built_layout());
            emit("{\"candidate_profile\":\""
                 + core99::l0079::to_string(arguments.config.candidate_profile)
                 + "\",\"candidate_count\":"
                 + std::to_string(problem.candidate_count())
                 + ",\"domain_area_km2\":5.7,\"layout\":"
                 + layout_json(problem.as_built_layout())
                 + ",\"evaluation\":" + evaluation_json(evaluation) + "}",
                 arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action != "optimize") {
            throw std::invalid_argument("L0079 action list-cases/evaluate-reference/optimize");
        }
        emit(result_json(core99::l0079::run(problem,arguments.config)),
             arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "L0079 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
