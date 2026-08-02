/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T83 pure-C++ paper-case and CPU-HPC command line
Paper DOI: 10.1016/j.apenergy.2022.118830
Public source, missing information, conflicts, completion, semantics, HPC and
claim boundary: include/core99/cazzaro_t83.hpp
Claim boundary: academic flexible reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/cazzaro_t83.hpp"

#include <cstdlib>
#include <cctype>
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
    std::filesystem::path data_root;
    std::filesystem::path output;
    char seed_role = 'A';
    core99::t83::RunConfig config;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) throw std::invalid_argument("T83 missing " + key);
            return std::string(argv[index]);
        };
        if (key == "--mode") result.mode = value();
        else if (key == "--data-root") result.data_root = value();
        else if (key == "--case") {
            const std::string role = value();
            if (role.size() != 1U) throw std::invalid_argument("T83 case A-H");
            result.seed_role = static_cast<char>(std::toupper(role[0]));
        } else if (key == "--workers") result.config.workers = std::stoi(value());
        else if (key == "--seed") result.config.seed = std::stoull(value());
        else if (key == "--micro-seconds") {
            result.config.micro_time_seconds = std::stod(value());
        } else if (key == "--micro-cycles") {
            result.config.fixed_micro_cycles = std::stoull(value());
        } else if (key == "--macro-cell-axis") {
            result.config.macro_cell_axis_override = std::stoi(value());
        } else if (key == "--output") result.output = value();
        else throw std::invalid_argument("T83 unknown option " + key);
    }
    return result;
}

std::string escape(const std::string& value) {
    std::string result;
    for (const char character : value) {
        if (character == '\\' || character == '"') result.push_back('\\');
        result.push_back(character);
    }
    return result;
}

std::string definition_json(const core99::t83::CaseDefinition& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"seed_role\":\"" << value.seed_role
        << "\",\"latitude_deg\":" << value.latitude_deg
        << ",\"longitude_deg\":" << value.longitude_deg
        << ",\"region\":\"" << escape(value.region)
        << "\",\"proxy_site\":\"" << value.proxy_site
        << "\",\"paper_shape_npv_meur\":" << value.paper_shape_npv_meur
        << ",\"paper_pta\":" << value.paper_pta
        << ",\"paper_density_mw_km2\":" << value.paper_density_mw_km2
        << ",\"paper_rectangle_npv_meur\":"
        << value.paper_rectangle_npv_meur << '}';
    return out.str();
}

std::string evaluation_json(const core99::t83::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"aep_mwh\":" << value.aep_mwh
        << ",\"foundation_cost_meur\":" << value.foundation_cost_meur
        << ",\"interarray_cable_cost_meur\":"
        << value.interarray_cable_cost_meur
        << ",\"export_cable_cost_meur\":" << value.export_cable_cost_meur
        << ",\"fixed_cost_offset_meur\":" << value.fixed_cost_offset_meur
        << ",\"area_km2\":" << value.area_km2
        << ",\"perimeter_km\":" << value.perimeter_km
        << ",\"perimeter_to_sqrt_area\":"
        << value.perimeter_to_sqrt_area
        << ",\"density_mw_km2\":" << value.density_mw_km2
        << ",\"minimum_spacing_m\":" << value.minimum_spacing_m
        << ",\"spacing_violation_m\":" << value.spacing_violation_m
        << ",\"area_violation_km2\":" << value.area_violation_km2
        << ",\"pta_violation\":" << value.pta_violation
        << ",\"density_violation_mw_km2\":"
        << value.density_violation_mw_km2
        << ",\"npv_meur\":" << value.npv_meur
        << ",\"feasible\":" << (value.feasible ? "true" : "false") << '}';
    return out.str();
}

std::string positions_json(const std::vector<int>& values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) out << ',';
        out << values[index];
    }
    return out.str() + ']';
}

std::string result_json(const core99::t83::RunResult& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"case_id\":\"" << value.case_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"protocol_semantic_id\":\"" << value.protocol_semantic_id
        << "\",\"paper_case\":" << definition_json(value.paper_case)
        << ",\"seed\":" << value.seed
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"source_candidate_positions\":"
        << value.source_candidate_positions
        << ",\"hpc_candidate_positions\":" << value.hpc_candidate_positions
        << ",\"turbines\":" << value.turbines
        << ",\"macro_rectangles_evaluated\":"
        << value.macro_rectangles_evaluated
        << ",\"pair_matrix_evaluations\":" << value.pair_matrix_evaluations
        << ",\"meso_candidate_evaluations\":"
        << value.meso_candidate_evaluations
        << ",\"shape_micro_candidate_evaluations\":"
        << value.shape_micro_candidate_evaluations
        << ",\"rectangle_micro_candidate_evaluations\":"
        << value.rectangle_micro_candidate_evaluations
        << ",\"shape_micro_cycles\":" << value.shape_micro_cycles
        << ",\"rectangle_micro_cycles\":" << value.rectangle_micro_cycles
        << ",\"macro_rectangle\":" << evaluation_json(value.macro_rectangle)
        << ",\"meso_shape\":" << evaluation_json(value.meso_shape)
        << ",\"optimized_shape\":" << evaluation_json(value.optimized_shape)
        << ",\"optimized_rectangle\":"
        << evaluation_json(value.optimized_rectangle)
        << ",\"meso_positions\":" << positions_json(value.meso_positions)
        << ",\"optimized_shape_positions\":"
        << positions_json(value.optimized_shape_positions)
        << ",\"optimized_rectangle_positions\":"
        << positions_json(value.optimized_rectangle_positions)
        << ",\"source_preprocessing_seconds\":"
        << value.source_preprocessing_seconds
        << ",\"candidate_preprocessing_seconds\":"
        << value.candidate_preprocessing_seconds
        << ",\"pair_matrix_seconds\":" << value.pair_matrix_seconds
        << ",\"macro_seconds\":" << value.macro_seconds
        << ",\"meso_seconds\":" << value.meso_seconds
        << ",\"shape_micro_seconds\":" << value.shape_micro_seconds
        << ",\"rectangle_micro_seconds\":" << value.rectangle_micro_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"source_matrix_fingerprint\":\"" << std::hex
        << value.source_matrix_fingerprint
        << "\",\"scientific_hash\":\"" << value.scientific_hash
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
    if (!stream) throw std::runtime_error("T83 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            std::ostringstream out;
            out << "{\"paper_cases\":[";
            const auto cases = core99::t83::paper_cases();
            for (std::size_t index = 0; index < cases.size(); ++index) {
                if (index) out << ',';
                out << definition_json(cases[index]);
            }
            out << "],\"paper_case_count\":8,\"formal_design_runs\":16}";
            emit(out.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.data_root.empty()) {
            throw std::invalid_argument("T83 --data-root is required");
        }
        const core99::t83::Problem problem(
            arguments.data_root, arguments.seed_role, arguments.config.workers
        );
        if (arguments.mode == "inspect") {
            std::ostringstream out;
            out << "{\"paper_case\":" << definition_json(problem.paper_case())
                << ",\"source_candidate_positions\":"
                << problem.source_candidate_positions()
                << ",\"hpc_candidate_positions\":"
                << problem.candidate_positions()
                << ",\"source_matrix_fingerprint\":\"" << std::hex
                << problem.source_matrix_fingerprint() << std::dec << "\"}";
            emit(out.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument("T83 mode list-cases, inspect, optimize");
        }
        emit(result_json(core99::t83::run(problem, arguments.config)),
             arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T83 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
