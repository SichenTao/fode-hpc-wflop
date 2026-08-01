/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0805 pure-C++ CPU-HPC CLI and JSON receipts
Paper/DOI: Shao et al.; 10.1016/J.ENERGY.2025.138820.
Public source, missing assets, conflicts, reconstruction, HPC analysis,
semantic IDs and claim boundary:
hpc/core99_cpp/include/core99/shao_l0805.hpp.
Controlling contract: shared/contracts/core99_l0805_pce_kriging_2025.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/shao_l0805.hpp"

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
    std::string case_id = "l0805_case_i";
    std::string output;
    int workers = 20;
    int layouts = 320;
    int pce_degree = 4;
    int initial_samples = -1;
    int truth_calls = -1;
    int ga_generations = 1000;
    std::uint64_t seed = 2026080501ULL;
    bool smoke = false;
};

std::string normalize_case(std::string value) {
    if (value == "I" || value == "i") return "l0805_case_i";
    if (value == "II" || value == "ii") return "l0805_case_ii";
    if (value == "III" || value == "iii") return "l0805_case_iii";
    if (value == "IV" || value == "iv") return "l0805_case_iv";
    return value;
}

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument("L0805 missing " + flag);
            }
            return std::string(argv[index]);
        };
        if (flag == "--action") result.action = value();
        else if (flag == "--case") result.case_id = normalize_case(value());
        else if (flag == "--output") result.output = value();
        else if (flag == "--workers") result.workers = std::stoi(value());
        else if (flag == "--layouts") result.layouts = std::stoi(value());
        else if (flag == "--pce-degree") result.pce_degree = std::stoi(value());
        else if (flag == "--initial-samples") {
            result.initial_samples = std::stoi(value());
        } else if (flag == "--truth-calls") {
            result.truth_calls = std::stoi(value());
        } else if (flag == "--ga-generations") {
            result.ga_generations = std::stoi(value());
        } else if (flag == "--seed") {
            result.seed = std::stoull(value());
        } else if (flag == "--smoke") result.smoke = true;
        else throw std::invalid_argument("L0805 unknown flag " + flag);
    }
    return result;
}

std::string layout_json(const core99::l0805::Layout& layout) {
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index) out << ',';
        out << layout[index];
    }
    out << ']';
    return out.str();
}

std::string evaluation_json(const core99::l0805::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"aep_gwh\":" << value.aep_gwh
        << ",\"mean_power_mw\":" << value.mean_power_mw
        << ",\"minimum_spacing_margin_m\":"
        << value.minimum_spacing_margin_m
        << ",\"feasible\":" << (value.feasible ? "true" : "false")
        << ",\"pce_degree\":" << value.pce_degree
        << ",\"physical_wake_simulations\":"
        << value.physical_wake_simulations << '}';
    return out.str();
}

std::string spec_json(const core99::l0805::CaseSpec& spec) {
    std::ostringstream out;
    out << "{\"case_id\":\"" << spec.case_id
        << "\",\"problem_semantic_id\":\"" << spec.problem_semantic_id
        << "\",\"turbines\":" << spec.turbines
        << ",\"grid_width\":" << spec.grid_width
        << ",\"initial_layout_samples\":" << spec.initial_layout_samples
        << ",\"target_layout_evaluations\":"
        << spec.target_layout_evaluations
        << ",\"wind_samples_per_layout\":"
        << spec.wind_samples_per_layout
        << ",\"formal_repeats\":" << spec.formal_repeats
        << ",\"high_fidelity_proxy\":"
        << (spec.high_fidelity_proxy ? "true" : "false") << '}';
    return out.str();
}

std::string run_json(const core99::l0805::RunResult& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"schema_version\":1,\"corpus_id\":\"L0805\","
        << "\"case_id\":\"" << value.case_id
        << "\",\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"protocol_semantic_id\":\"" << value.protocol_semantic_id
        << "\",\"seed\":" << value.seed
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"selected_pce_degree\":" << value.selected_pce_degree
        << ",\"initial_samples\":" << value.initial_samples
        << ",\"truth_calls\":" << value.truth_calls
        << ",\"msp_infills\":" << value.msp_infills
        << ",\"ei_infills\":" << value.ei_infills
        << ",\"surrogate_fes\":" << value.surrogate_fes
        << ",\"physical_wake_simulations\":"
        << value.physical_wake_simulations
        << ",\"best_layout\":" << layout_json(value.best_layout)
        << ",\"initial_best\":" << evaluation_json(value.initial_best)
        << ",\"best_evaluation\":"
        << evaluation_json(value.best_evaluation)
        << ",\"best_history_gwh\":[";
    for (std::size_t index = 0; index < value.best_history_gwh.size(); ++index) {
        if (index) out << ',';
        out << value.best_history_gwh[index];
    }
    out << "],\"selected_kernel_theta\":" << value.selected_kernel_theta
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
        << ",\"pce_seconds\":" << value.pce_seconds
        << ",\"surrogate_training_seconds\":"
        << value.surrogate_training_seconds
        << ",\"surrogate_inference_seconds\":"
        << value.surrogate_inference_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex
        << value.scientific_hash << std::dec << "\"}";
    return out.str();
}

void emit(const std::string& payload, const std::string& output) {
    if (output.empty()) {
        std::cout << payload << '\n';
        return;
    }
    std::ofstream stream(output);
    if (!stream) throw std::runtime_error("L0805 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.action == "list-roles") {
            std::ostringstream out;
            out << "{\"protocol_semantic_id\":"
                << "\"l0805_native_30x3_plus_single_iv_v1\","
                << "\"required_target_runs\":91,\"cases\":[";
            const auto ids = core99::l0805::paper_case_ids();
            for (std::size_t index = 0; index < ids.size(); ++index) {
                if (index) out << ',';
                out << spec_json(core99::l0805::case_spec(ids[index]));
            }
            out << "]}";
            emit(out.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        const core99::l0805::Problem problem(arguments.case_id);
        if (arguments.action == "describe") {
            emit(spec_json(problem.spec()), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action == "evaluate") {
            const auto layout = core99::l0805::perimeter_layout(problem.spec());
            std::ostringstream out;
            out << "{\"schema_version\":1,\"corpus_id\":\"L0805\","
                << "\"case_id\":\"" << problem.spec().case_id
                << "\",\"layout\":" << layout_json(layout)
                << ",\"evaluation\":" << evaluation_json(problem.evaluate(
                    layout, arguments.seed, arguments.pce_degree)) << '}';
            emit(out.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action == "profile-batch") {
            const auto receipt = problem.profile_batch(
                arguments.layouts, arguments.seed, arguments.workers);
            std::ostringstream out;
            out << std::setprecision(17)
                << "{\"schema_version\":1,\"corpus_id\":\"L0805\","
                << "\"case_id\":\"" << receipt.case_id
                << "\",\"layouts\":" << receipt.layouts
                << ",\"requested_workers\":" << receipt.requested_workers
                << ",\"observed_workers\":" << receipt.observed_workers
                << ",\"physical_wake_simulations\":"
                << receipt.physical_wake_simulations
                << ",\"aep_checksum_gwh\":" << receipt.aep_checksum_gwh
                << ",\"seconds\":" << receipt.seconds
                << ",\"scientific_hash\":\"" << std::hex
                << receipt.scientific_hash << std::dec << "\"}";
            emit(out.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.action != "optimize") {
            throw std::invalid_argument(
                "L0805 action list-roles/describe/evaluate/profile-batch/optimize"
            );
        }
        core99::l0805::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.initial_samples = arguments.initial_samples;
        config.target_truth_calls = arguments.truth_calls;
        config.maximum_ga_generations = arguments.ga_generations;
        config.smoke = arguments.smoke;
        emit(run_json(problem.optimize(config)), arguments.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
