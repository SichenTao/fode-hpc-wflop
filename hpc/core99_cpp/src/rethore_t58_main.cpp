/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T58 pure-C++ CPU-HPC CLI and JSON receipts
Paper/DOI: Rethore et al.; 10.1002/we.1667
Assets, omissions, reconstruction, semantic IDs and claim boundary:
include/core99/rethore_t58.hpp.
Public source provenance: official DTU reports and later TopFarm2 lineage are
evidence oracles only; the CLI executes the project-native implementation.
Controlling contract: shared/contracts/core99_t58_rethore_topfarm_2014.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/rethore_t58.hpp"

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
    std::string action = "optimize";
    std::string output;
    core99::t58::CaseId case_id = core99::t58::CaseId::fictitious_2x3;
    core99::t58::RunConfig config;
    core99::t58::EvaluationSettings evaluation;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() {
            if (++index >= argc) throw std::invalid_argument("T58 missing " + flag);
            return std::string(argv[index]);
        };
        if (flag == "--action") result.action = value();
        else if (flag == "--case") {
            const std::string item = value();
            if (item == "2x3") result.case_id = core99::t58::CaseId::fictitious_2x3;
            else if (item == "stags") result.case_id = core99::t58::CaseId::stags_holt_coldham;
            else if (item == "middelgrunden") result.case_id = core99::t58::CaseId::middelgrunden;
            else throw std::invalid_argument("T58 case 2x3/stags/middelgrunden");
        } else if (flag == "--method") {
            const std::string item = value();
            if (item == "slp") result.config.method = core99::t58::Method::slp_only;
            else if (item == "sga") result.config.method = core99::t58::Method::sga_only;
            else if (item == "sga-slp") result.config.method = core99::t58::Method::sga_slp;
            else throw std::invalid_argument("T58 method slp/sga/sga-slp");
        } else if (flag == "--workers") result.config.workers = std::stoi(value());
        else if (flag == "--seed") result.config.seed = std::stoull(value());
        else if (flag == "--sga-generations") {
            result.config.sga_generations_override = std::stoi(value());
        } else if (flag == "--slp-iterations") {
            result.config.slp_iterations_override = std::stoi(value());
        } else if (flag == "--fatigue-scale") {
            result.evaluation.fatigue_scale = std::stod(value());
        } else if (flag == "--cable-scale") {
            result.evaluation.cable_scale = std::stod(value());
        } else if (flag == "--fidelity") {
            const std::string item = value();
            if (item == "level1") {
                result.evaluation.fidelity = core99::t58::Fidelity::level1_coarse;
            } else if (item == "level2") {
                result.evaluation.fidelity = core99::t58::Fidelity::level2_fine;
            } else throw std::invalid_argument("T58 fidelity level1/level2");
        } else if (flag == "--smoke") result.config.smoke = true;
        else if (flag == "--output") result.output = value();
        else throw std::invalid_argument("T58 unknown flag " + flag);
    }
    return result;
}

std::string evaluation_json(const core99::t58::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"gross_aep_mwh_per_year\":" << value.gross_aep_mwh_per_year
        << ",\"net_aep_mwh_per_year\":" << value.net_aep_mwh_per_year
        << ",\"energy_efficiency_percent\":" << value.energy_efficiency_percent
        << ",\"power_value_meur\":" << value.power_value_meur
        << ",\"foundation_cost_meur\":" << value.foundation_cost_meur
        << ",\"cable_cost_meur\":" << value.cable_cost_meur
        << ",\"degradation_cost_meur\":" << value.degradation_cost_meur
        << ",\"maintenance_cost_meur\":" << value.maintenance_cost_meur
        << ",\"financial_balance_meur\":" << value.financial_balance_meur
        << ",\"cable_length_m\":" << value.cable_length_m
        << ",\"minimum_spacing_m\":" << value.minimum_spacing_m
        << ",\"maximum_constraint_violation_m\":"
        << value.maximum_constraint_violation_m
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"feasible\":" << (value.feasible ? "true" : "false")
        << ",\"seconds\":" << value.seconds << '}';
    return out.str();
}

std::string layout_json(const std::vector<core99::t58::Point>& layout) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index) out << ',';
        out << '[' << layout[index].x_m << ',' << layout[index].y_m << ']';
    }
    return out.str() + ']';
}

void emit(const std::string& payload, const std::string& path) {
    if (path.empty()) {
        std::cout << payload << '\n';
        return;
    }
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("T58 cannot write output");
    stream << payload << '\n';
}

std::string run_json(const core99::t58::RunResult& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"protocol_semantic_id\":\"" << value.protocol_semantic_id
        << "\",\"case_id\":\"" << core99::t58::case_name(value.case_id)
        << "\",\"method\":\"" << core99::t58::method_name(value.method)
        << "\",\"seed\":" << value.seed
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"sga_population\":" << value.sga_population
        << ",\"sga_generations\":" << value.sga_generations
        << ",\"slp_iterations\":" << value.slp_iterations
        << ",\"physical_fes\":" << value.physical_fes
        << ",\"initial_evaluation\":" << evaluation_json(value.initial_evaluation)
        << ",\"final_evaluation\":" << evaluation_json(value.final_evaluation)
        << ",\"stages\":[";
    for (std::size_t index = 0; index < value.stages.size(); ++index) {
        if (index) out << ',';
        const auto& stage = value.stages[index];
        out << "{\"stage\":\"" << stage.stage
            << "\",\"iterations\":" << stage.iterations
            << ",\"physical_fes\":" << stage.physical_fes
            << ",\"start_balance_meur\":" << stage.start_balance_meur
            << ",\"end_balance_meur\":" << stage.end_balance_meur
            << ",\"evaluator_seconds\":" << stage.evaluator_seconds
            << ",\"seconds\":" << stage.seconds << '}';
    }
    out << "],\"final_layout\":" << layout_json(value.final_layout)
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex << value.scientific_hash
        << std::dec << "\"}";
    return out.str();
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        const core99::t58::Problem problem(arguments.case_id);
        if (arguments.action == "list-roles") {
            emit("{\"protocol_semantic_id\":\"t58_native_five_role_single_run_v1\","
                 "\"native_roles\":[\"2x3_slp\",\"2x3_sga\","
                 "\"2x3_sga_slp\",\"stags_sga_slp\","
                 "\"middelgrunden_sga_slp\"],\"native_repeats\":1}",
                 arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action == "evaluate-baseline") {
            fode::PersistentExecutor executor(arguments.config.workers);
            const auto value = problem.evaluate(
                problem.baseline_layout(), arguments.evaluation, executor);
            std::ostringstream out;
            out << "{\"problem_semantic_id\":\"" << problem.semantic_id()
                << "\",\"case_id\":\"" << core99::t58::case_name(problem.case_id())
                << "\",\"turbines\":" << problem.turbine_count()
                << ",\"fine_wind_states\":" << problem.fine_wind_state_count()
                << ",\"candidate_count\":" << problem.candidate_count()
                << ",\"rotor_diameter_m\":" << problem.rotor_diameter_m()
                << ",\"fidelity\":\""
                << core99::t58::fidelity_name(arguments.evaluation.fidelity)
                << "\",\"evaluation\":" << evaluation_json(value)
                << ",\"layout\":" << layout_json(problem.baseline_layout()) << '}';
            emit(out.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action != "optimize") {
            throw std::invalid_argument("T58 action list-roles/evaluate-baseline/optimize");
        }
        emit(run_json(core99::t58::run(problem, arguments.config)), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T58 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
