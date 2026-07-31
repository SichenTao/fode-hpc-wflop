/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T19 official-SRMP C++/HPC command line and JSON receipt
Paper DOI: 10.1016/j.energy.2021.120035.
Public source: pinned official SRMP v1.01.
Missing fields, declared Reconstruction completions, GPL target license,
semantic IDs and Claim boundary: include/core99/dhoot_t19.hpp.
Contract: shared/contracts/core99_t19_dhoot_2021.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/dhoot_t19.hpp"

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
    std::string action = "solve";
    core99::t19::ProblemConfig problem;
    core99::t19::SolveConfig solve;
    std::filesystem::path output;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) throw std::invalid_argument("T19 missing " + key);
            return std::string(argv[index]);
        };
        if (key == "--action") result.action = value();
        else if (key == "--family") {
            const std::string item = value();
            if (item == "historical") {
                result.problem.family = core99::t19::ProblemFamily::historical;
            } else if (item == "realistic") {
                result.problem.family = core99::t19::ProblemFamily::realistic;
            } else throw std::invalid_argument("T19 family historical/realistic");
        } else if (key == "--wind") {
            const std::string item = value();
            if (item == "wr1") result.problem.wind = core99::t19::WindRegime::wr1;
            else if (item == "wr36") result.problem.wind = core99::t19::WindRegime::wr36;
            else throw std::invalid_argument("T19 wind wr1/wr36");
        } else if (key == "--cells") {
            result.problem.cell_count = std::stoi(value());
        } else if (key == "--turbines") {
            result.problem.turbine_count = std::stoi(value());
        } else if (key == "--workers") {
            result.solve.workers = std::stoi(value());
        } else if (key == "--iterations") {
            result.solve.maximum_iterations = std::stoi(value());
        } else if (key == "--time-limit") {
            result.solve.time_limit_seconds = std::stod(value());
        } else if (key == "--epsilon") {
            result.solve.convergence_epsilon = std::stod(value());
        } else if (key == "--triplets") {
            result.solve.requested_triplets = std::stoi(value());
        } else if (key == "--one-swap") {
            const std::string item = value();
            if (item == "on") result.solve.one_swap_improvement = true;
            else if (item == "off") result.solve.one_swap_improvement = false;
            else throw std::invalid_argument("T19 one-swap on/off");
        } else if (key == "--output") result.output = value();
        else throw std::invalid_argument("T19 unknown option " + key);
    }
    return result;
}

std::string solve_json(const core99::t19::SolveReceipt& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"schema_version\":1,\"corpus_id\":\"T19\""
        << ",\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"protocol_semantic_id\":\"" << value.protocol_semantic_id
        << "\",\"family\":\"" << core99::t19::family_name(value.problem.family)
        << "\",\"wind_regime\":\"" << core99::t19::wind_name(value.problem.wind)
        << "\",\"cell_count\":" << value.problem.cell_count
        << ",\"turbine_count\":" << value.problem.turbine_count
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"requested_triplets\":" << value.requested_triplets
        << ",\"generated_triplets\":" << value.generated_triplets
        << ",\"raw_cardinality\":" << value.raw_cardinality
        << ",\"repaired_cardinality\":" << value.repaired_cardinality
        << ",\"repair_operations\":" << value.repair_operations
        << ",\"local_swap_operations\":" << value.local_swap_operations
        << ",\"maximum_iterations\":" << value.maximum_iterations
        << ",\"time_limit_seconds\":" << value.time_limit_seconds
        << ",\"beta\":" << value.beta
        << ",\"srmp_lower_bound\":" << value.srmp_lower_bound
        << ",\"raw_augmented_energy\":" << value.raw_augmented_energy
        << ",\"repaired_augmented_energy\":" << value.repaired_augmented_energy
        << ",\"expected_power_kw\":" << value.evaluation.expected_power_kw
        << ",\"aep_gwh\":" << value.evaluation.aep_gwh
        << ",\"qip_wake_objective\":" << value.evaluation.qip_wake_objective
        << ",\"minimum_spacing_m\":" << value.evaluation.minimum_spacing_m
        << ",\"exact_cardinality\":"
        << (value.evaluation.exact_cardinality ? "true" : "false")
        << ",\"spacing_feasible\":"
        << (value.evaluation.spacing_feasible ? "true" : "false")
        << ",\"physical_fes\":" << value.evaluation.physical_fes
        << ",\"layout\":[";
    for (std::size_t index = 0; index < value.layout.size(); ++index) {
        if (index) out << ',';
        out << value.layout[index];
    }
    out << "]"
        << ",\"interaction_assembly_seconds\":"
        << value.interaction_assembly_seconds
        << ",\"triplet_generation_seconds\":"
        << value.triplet_generation_seconds
        << ",\"graph_assembly_seconds\":" << value.graph_assembly_seconds
        << ",\"sequential_trws_seconds\":" << value.sequential_trws_seconds
        << ",\"repair_and_local_search_seconds\":"
        << value.repair_and_local_search_seconds
        << ",\"nonlinear_aep_seconds\":" << value.nonlinear_aep_seconds
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
    if (!stream) throw std::runtime_error("T19 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.action == "describe") {
            emit(
                "{\"schema_version\":1,\"corpus_id\":\"T19\","
                "\"paper_role_count\":112,\"deterministic_role_count\":112,"
                "\"problem_families\":[\"historical\",\"realistic\"],"
                "\"cell_counts\":[100,400,2500],"
                "\"wind_regimes\":[\"wr1\",\"wr36\"],"
                "\"solver\":\"official_srmp_v1.01_trws\","
                "\"physical_fes_definition\":\"one_complete_posterior_nonlinear_aep_evaluation\"}",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        if (arguments.action != "solve") {
            throw std::invalid_argument("T19 action describe/solve");
        }
        const core99::t19::Problem problem(arguments.problem);
        emit(solve_json(problem.solve(arguments.solve)), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T19 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
