/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y13 pure-C++/HiGHS CPU-HPC CLI and JSON receipts
Paper/DOI: Du et al.; 10.1109/TSTE.2025.3609006.
Public source: no target code is public; cited-source details are in
include/core99/du_y13.hpp.
Missing and Reconstruction: all absent assets and declared completion choices
are listed in that header.
Semantic IDs: y13_four_grid_fg36_declared_v1,
y13_l2box_consensus_admm_highs_declared_v1 and
y13_native_four_case_single_run_v1.
Claim boundary: equation-level flexible academic reproduction; full boundary
is recorded in include/core99/du_y13.hpp.
Controlling contract: shared/contracts/core99_y13_du_grid_admm_2026.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/du_y13.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string action = "optimize";
    std::string output;
    core99::y13::CaseId case_id = core99::y13::CaseId::grid_6;
    core99::y13::RunConfig config;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() {
            if (++index >= argc) throw std::invalid_argument("Y13 missing " + flag);
            return std::string(argv[index]);
        };
        if (flag == "--action") result.action = value();
        else if (flag == "--case") {
            const std::string item = value();
            if (item == "6x6") result.case_id = core99::y13::CaseId::grid_6;
            else if (item == "10x10") result.case_id = core99::y13::CaseId::grid_10;
            else if (item == "16x16") result.case_id = core99::y13::CaseId::grid_16;
            else if (item == "20x20") result.case_id = core99::y13::CaseId::grid_20;
            else throw std::invalid_argument("Y13 case 6x6/10x10/16x16/20x20");
        } else if (flag == "--workers") result.config.workers = std::stoi(value());
        else if (flag == "--iterations") {
            result.config.maximum_admm_iterations = std::stoi(value());
        } else if (flag == "--tolerance") {
            result.config.convergence_tolerance = std::stod(value());
        } else if (flag == "--smoke") result.config.smoke = true;
        else if (flag == "--output") result.output = value();
        else throw std::invalid_argument("Y13 unknown flag " + flag);
    }
    return result;
}

std::string evaluation_json(const core99::y13::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"gross_aep_gwh\":" << value.gross_aep_gwh
        << ",\"net_aep_gwh\":" << value.net_aep_gwh
        << ",\"efficiency_percent\":" << value.efficiency_percent
        << ",\"turbines\":" << value.turbines
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"scenario_pair_lookups\":" << value.scenario_pair_lookups
        << ",\"seconds\":" << value.seconds << '}';
    return out.str();
}

std::string run_json(const core99::y13::RunResult& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"schema_version\":1,\"corpus_id\":\"Y13\","
        << "\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"protocol_semantic_id\":\"" << value.protocol_semantic_id
        << "\",\"case_id\":\"" << core99::y13::case_name(value.case_id)
        << "\",\"grid_side\":" << value.grid_side
        << ",\"cells\":" << value.cells
        << ",\"turbines\":" << value.turbines
        << ",\"wind_scenarios\":" << value.wind_scenarios
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"admm_iterations\":" << value.admm_iterations
        << ",\"scenario_subproblem_solves\":"
        << value.scenario_subproblem_solves
        << ",\"complete_layout_evaluations\":"
        << value.complete_layout_evaluations
        << ",\"initial_rho\":" << value.initial_rho
        << ",\"final_rho\":" << value.final_rho
        << ",\"final_rounding_deviation\":"
        << value.final_rounding_deviation
        << ",\"initial_evaluation\":"
        << evaluation_json(value.initial_evaluation)
        << ",\"final_evaluation\":"
        << evaluation_json(value.final_evaluation)
        << ",\"selected_cells\":[";
    for (std::size_t index = 0; index < value.selected_cells.size(); ++index) {
        if (index) out << ',';
        out << value.selected_cells[index];
    }
    out << "],\"iterations\":[";
    for (std::size_t index = 0; index < value.iterations.size(); ++index) {
        if (index) out << ',';
        const auto& row = value.iterations[index];
        out << "{\"iteration\":" << row.iteration
            << ",\"rho\":" << row.rho
            << ",\"primal_residual\":" << row.primal_residual
            << ",\"dual_residual\":" << row.dual_residual
            << ",\"rounding_deviation\":" << row.rounding_deviation
            << ",\"subproblem_seconds\":"
            << row.subproblem_seconds << '}';
    }
    out << "],\"matrix_seconds\":" << value.matrix_seconds
        << ",\"subproblem_seconds\":" << value.subproblem_seconds
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex << value.scientific_hash
        << std::dec << "\"}";
    return out.str();
}

void emit(const std::string& payload, const std::string& path) {
    if (path.empty()) {
        std::cout << payload << '\n';
        return;
    }
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("Y13 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.action == "list-roles") {
            emit("{\"protocol_semantic_id\":\"y13_native_four_case_single_run_v1\","
                 "\"native_roles\":[\"6x6_admm\",\"10x10_admm\","
                 "\"16x16_admm\",\"20x20_admm\"],\"native_repeats\":1}",
                 arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action != "optimize") {
            throw std::invalid_argument("Y13 action list-roles/optimize");
        }
        const core99::y13::Problem problem(
            arguments.case_id, arguments.config.workers);
        emit(run_json(core99::y13::run(problem, arguments.config)),
             arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Y13 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
