/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: universal switchable formal CLI and CPU backend router
Paper title and DOI: not_applicable_shared_infrastructure
Paper/source basis: paper-paired platform contracts
Public asset: project-native implementation
Missing/conflicts: hybrid and GPU modes have no admitted kernels and fail closed
Reconstruction: not applicable
Method/problem semantic IDs: registry_defined; registry_defined
Controlling contract and claim boundary:
docs/paper_package_completion.tsv; CLI execution does not upgrade evidence tier
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "wflop/algorithms.hpp"
#include "wflop/rlfode_reconstruction.hpp"

#include "fode/case.hpp"
#include "fode/evaluator.hpp"
#include "fode/executor.hpp"

#ifdef WFLOP_PLAN004_LIBTORCH
#include "wflop_learning/models.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <omp.h>

namespace {

struct Arguments {
    std::string cases_path = "shared/contracts/benchmark_cases.json";
    std::string models_path = "shared/models/sugga_cpp";
    std::string rlfode_models_path = "shared/models/fqfode_seeded";
    wflop::FqfodeSensitivityProfile fqfode_sensitivity_profile =
        wflop::FqfodeSensitivityProfile::baseline;
    std::string algorithm = "fode";
    std::vector<std::string> algorithms;
    std::string problem = "fode_e0_common";
    std::string compute_backend = "cpu";
    std::string paper_protocol = "unregistered_cli_protocol";
    std::string training_artifact = "not_applicable";
    std::string explain_algorithm;
    std::string explain_problem;
    std::string case_id = "WS5tn30";
    std::string output_path;
    std::uint64_t seed = 20260728;
    std::uint64_t physical_fes = 24000;
    int workers = 0;
    int torch_intraop_threads = 1;
    int torch_interop_threads = 1;
    int alga_attention_hidden_width = 1;
    bool all_cases = false;
    bool all_algorithms = false;
    bool list_algorithms = false;
    bool list_problems = false;
    bool list_compute_backends = false;
    bool list_training = false;
    bool explain_compatibility = false;
    bool self_check = false;
    bool check_ise_rename = false;
};

std::uint64_t parse_u64(const std::string& text, const std::string& flag) {
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed);
    if (consumed != text.size()) {
        throw std::runtime_error("invalid value for " + flag + ": " + text);
    }
    return value;
}

std::vector<std::string> parse_algorithm_list(const std::string& text) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t delimiter = text.find(',', start);
        const std::size_t end = delimiter == std::string::npos
            ? text.size()
            : delimiter;
        const std::string id = text.substr(start, end - start);
        if (id.empty()) {
            throw std::runtime_error(
                "empty algorithm identifier in --algorithms"
            );
        }
        result.push_back(id);
        if (delimiter == std::string::npos) {
            break;
        }
        start = delimiter + 1;
    }
    if (result.empty()) {
        throw std::runtime_error("--algorithms requires at least one ID");
    }
    return result;
}

Arguments parse_arguments(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto next = [&]() {
            if (index + 1 >= argc) {
                throw std::runtime_error("missing value after " + flag);
            }
            return std::string(argv[++index]);
        };
        if (flag == "--cases") {
            result.cases_path = next();
        } else if (flag == "--models") {
            result.models_path = next();
        } else if (flag == "--rlfode-models") {
            result.rlfode_models_path = next();
        } else if (flag == "--fqfode-sensitivity-profile") {
            result.fqfode_sensitivity_profile =
                wflop::rlfode_reconstruction::parse_sensitivity_profile(
                    next()
                );
        } else if (flag == "--algorithm") {
            result.algorithm = next();
        } else if (flag == "--algorithms") {
            result.algorithms = parse_algorithm_list(next());
        } else if (flag == "--problem") {
            result.problem = next();
        } else if (flag == "--compute-backend") {
            result.compute_backend = next();
        } else if (flag == "--backend") {
            result.compute_backend = next();
        } else if (flag == "--paper-protocol") {
            result.paper_protocol = next();
        } else if (flag == "--training-artifact") {
            result.training_artifact = next();
        } else if (flag == "--explain-compatibility") {
            result.explain_algorithm = next();
            result.explain_problem = next();
            result.explain_compatibility = true;
        } else if (flag == "--case") {
            result.case_id = next();
        } else if (flag == "--output") {
            result.output_path = next();
        } else if (flag == "--seed") {
            result.seed = parse_u64(next(), flag);
        } else if (flag == "--physical-fes") {
            result.physical_fes = parse_u64(next(), flag);
        } else if (flag == "--workers") {
            result.workers = static_cast<int>(parse_u64(next(), flag));
        } else if (flag == "--torch-intraop-threads") {
            result.torch_intraop_threads =
                static_cast<int>(parse_u64(next(), flag));
        } else if (flag == "--torch-interop-threads") {
            result.torch_interop_threads =
                static_cast<int>(parse_u64(next(), flag));
        } else if (flag == "--alga-attention-width") {
            result.alga_attention_hidden_width =
                static_cast<int>(parse_u64(next(), flag));
        } else if (flag == "--all-cases") {
            result.all_cases = true;
        } else if (flag == "--all-algorithms") {
            result.all_algorithms = true;
        } else if (flag == "--list-algorithms") {
            result.list_algorithms = true;
        } else if (flag == "--list-problems") {
            result.list_problems = true;
        } else if (flag == "--list-compute-backends") {
            result.list_compute_backends = true;
        } else if (flag == "--list-training") {
            result.list_training = true;
        } else if (flag == "--self-check") {
            result.self_check = true;
        } else if (flag == "--check-ise-rename") {
            result.check_ise_rename = true;
        } else if (flag == "--help" || flag == "-h") {
            std::cout
                << "Usage: wflop_cpp_hpc [options]\n"
                << "  --algorithm ID       one registered algorithm identifier\n"
                << "  --algorithms A,B,... run an explicit ordered algorithm set\n"
                << "  --problem ID         registered problem family; default fode_e0_common\n"
                << "  --backend B          cpu, auto, hybrid, or gpu\n"
                << "  --compute-backend B  compatibility alias for --backend\n"
                << "  --paper-protocol ID  frozen paper-native protocol identity\n"
                << "  --training-artifact P artifact path, ID, or train\n"
                << "  --explain-compatibility A P  explain algorithm/problem admission\n"
                << "  --all-algorithms     run all registered algorithms sequentially\n"
                << "  --cases PATH         selected problem manifest\n"
                << "  --case ID            one case from the selected manifest\n"
                << "  --all-cases          run every case in the selected manifest\n"
                << "  --physical-fes N     complete-layout evaluation budget per run\n"
                << "  --seed N             deterministic algorithm seed\n"
                << "  --workers N          persistent C++ worker count; 0 means all visible CPUs\n"
                << "  --torch-intraop-threads N"
                   " LibTorch CPU intra-op threads; default 1\n"
                << "  --torch-interop-threads N"
                   " LibTorch CPU inter-op threads; default 1\n"
                << "  --alga-attention-width N"
                   " 1 baseline or 2 sensitivity profile\n"
                << "  --models PATH        frozen C++ SUGGA model directory\n"
                << "  --rlfode-models PATH frozen FQFODE Q-table directory\n"
                << "  --fqfode-sensitivity-profile ID"
                   " baseline, multiplicative-action,"
                   " fes-normalized-stage,"
                   " wrap-after-generation-200, or"
                   " independent-stage-pretraining\n"
                << "  --output PATH        write JSON or JSONL\n"
                << "  --self-check         bounded registered-algorithm semantic smoke\n"
                << "  --list-algorithms    print canonical algorithm IDs\n"
                << "  --list-problems      print canonical problem IDs\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + flag);
        }
    }
    if (result.all_algorithms && !result.algorithms.empty()) {
        throw std::runtime_error(
            "--all-algorithms and --algorithms are mutually exclusive"
        );
    }
    static_cast<void>(wflop::backend_descriptor(result.compute_backend));
    if (result.workers == 0) {
        result.workers = omp_get_num_procs();
    }
    if (result.workers <= 0) {
        throw std::runtime_error("no CPU workers are visible");
    }
    if (
        result.torch_intraop_threads <= 0
        || result.torch_interop_threads <= 0
    ) {
        throw std::runtime_error(
            "Torch intra-op and inter-op thread counts must be positive"
        );
    }
    return result;
}

std::string escape_json(const std::string& value) {
    std::ostringstream stream;
    for (const char ch : value) {
        if (ch == '"' || ch == '\\') {
            stream << '\\';
        }
        stream << ch;
    }
    return stream.str();
}

std::string to_json(const wflop::RunResult& result) {
    std::ostringstream output;
    output << std::setprecision(17);
    output << "{";
    output << "\"schema_version\":1,";
    output << "\"algorithm_id\":\"" << escape_json(result.algorithm_id) << "\",";
    output << "\"method_id\":\"" << escape_json(result.method_id) << "\",";
    output << "\"algorithm_provenance\":\""
           << escape_json(result.algorithm_provenance) << "\",";
    output << "\"effective_semantics_id\":\""
           << escape_json(result.effective_semantics_id) << "\",";
    output << "\"problem_id\":\"" << escape_json(result.problem_id) << "\",";
    output << "\"problem_semantics_id\":\""
           << escape_json(result.problem_semantics_id) << "\",";
    output << "\"paper_protocol_id\":\""
           << escape_json(result.paper_protocol_id) << "\",";
    output << "\"training_artifact_id\":\""
           << escape_json(result.training_artifact_id) << "\",";
    output << "\"backend_id\":\"" << escape_json(result.backend_id) << "\",";
    output << "\"case_id\":\"" << escape_json(result.case_id) << "\",";
    output << "\"seed\":" << result.seed << ",";
    output << "\"physical_fes\":" << result.physical_fes << ",";
    output << "\"training_physical_fes\":"
           << result.training_physical_fes << ",";
    output << "\"offline_training_physical_fes\":"
           << result.offline_training_physical_fes << ",";
    output << "\"inference_physical_fes\":"
           << result.inference_physical_fes << ",";
    output << "\"policy_interactions\":" << result.policy_interactions << ",";
    output << "\"policy_updates\":" << result.policy_updates << ",";
    output << "\"policy_stage_interactions\":[";
    for (std::size_t index = 0;
         index < result.policy_stage_interactions.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << result.policy_stage_interactions[index];
    }
    output << "],";
    output << "\"policy_stage_updates\":[";
    for (std::size_t index = 0;
         index < result.policy_stage_updates.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << result.policy_stage_updates[index];
    }
    output << "],";
    output << "\"alga_attention_hidden_width\":"
           << result.alga_attention_hidden_width << ",";
    output << "\"generations\":" << result.generations << ",";
    output << "\"initial_population\":" << result.initial_population << ",";
    output << "\"final_population\":" << result.final_population << ",";
    output << "\"requested_workers\":" << result.requested_workers << ",";
    output << "\"observed_workers\":" << result.observed_workers << ",";
    output << "\"thread_topology\":{"
           << "\"outer_workers\":" << result.observed_workers << ","
           << "\"torch_intraop_threads\":"
           << result.torch_intraop_threads << ","
           << "\"torch_interop_threads\":"
           << result.torch_interop_threads << "},";
    output << "\"best_expected_power_kw\":"
           << result.best_expected_power_kw << ",";
    output << "\"best_layout_1based\":[";
    for (std::size_t index = 0;
         index < result.best_layout_1based.size();
         ++index) {
        if (index != 0) {
            output << ",";
        }
        output << result.best_layout_1based[index];
    }
    output << "],";
    output << "\"timing_seconds\":{";
    output << "\"end_to_end\":" << result.total_seconds << ",";
    output << "\"evaluator\":" << result.evaluator_seconds << ",";
    output << "\"algorithm\":" << result.algorithm_seconds << ",";
    output << "\"policy_training\":" << result.policy_training_seconds << ",";
    output << "\"policy_update\":" << result.policy_update_seconds << "}";
    if (!result.pso_update_semantics.empty()) {
        output << ",\"pso_update_semantics\":\""
               << escape_json(result.pso_update_semantics) << "\"";
    }
    if (!result.pretrained_artifact_hash.empty()) {
        output << ",\"pretrained_artifact_hash\":\""
               << escape_json(result.pretrained_artifact_hash) << "\"";
    }
    if (!result.learned_state_hash.empty()) {
        output << ",\"learned_state_hash\":\""
               << escape_json(result.learned_state_hash) << "\"";
    }
    output << ",\"learning_artifact_consumed\":"
           << (result.learning_artifact_consumed ? "true" : "false");
    if (!result.learning_decision_hash.empty()) {
        output << ",\"learning_decision_hash\":\""
               << escape_json(result.learning_decision_hash) << "\"";
    }
    if (!result.terminal_partial_work.empty()) {
        output << ",\"terminal_partial_work\":\""
               << escape_json(result.terminal_partial_work) << "\"";
    }
    output << "}";
    return output.str();
}

void write_output(const std::string& path, const std::string& contents) {
    if (path.empty()) {
        return;
    }
    const std::filesystem::path output_path(path);
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::filesystem::path temporary = output_path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::trunc);
        if (!stream) {
            throw std::runtime_error(
                "cannot write temporary output: " + temporary.string()
            );
        }
        stream << contents;
        if (contents.empty() || contents.back() != '\n') {
            stream << '\n';
        }
        stream.flush();
        if (!stream) {
            throw std::runtime_error(
                "cannot complete temporary output: " + temporary.string()
            );
        }
    }
    std::filesystem::rename(temporary, output_path);
}

void validate_result(
    const wflop::RunResult& result,
    const fode::CaseData& data,
    std::uint64_t expected_fes,
    int expected_workers
) {
    if (result.physical_fes != expected_fes) {
        throw std::runtime_error(
            result.algorithm_id + " did not stop at the exact physical FES"
        );
    }
    if (result.training_physical_fes + result.inference_physical_fes
        != result.physical_fes) {
        throw std::runtime_error(
            result.algorithm_id + " returned an inconsistent FES ledger"
        );
    }
    if (result.observed_workers != expected_workers) {
        throw std::runtime_error(
            result.algorithm_id + " did not use the requested persistent team"
        );
    }
    if (result.best_layout_1based.empty()
        || result.algorithm_provenance.empty()
        || result.effective_semantics_id.empty()
        || result.problem_id.empty()
        || result.problem_semantics_id.empty()
        || result.best_layout_1based.size()
            != static_cast<std::size_t>(data.turbine_count)
        || !std::isfinite(result.best_expected_power_kw)
        || result.best_expected_power_kw <= 0.0
        || !std::isfinite(result.total_seconds)
        || !std::isfinite(result.evaluator_seconds)
        || !std::isfinite(result.algorithm_seconds)
        || result.total_seconds < 0.0
        || result.evaluator_seconds < 0.0
        || result.algorithm_seconds < 0.0) {
        throw std::runtime_error(
            result.algorithm_id + " returned an invalid best state"
        );
    }
    std::vector<char> unavailable(
        static_cast<std::size_t>(data.rows * data.cols),
        0
    );
    for (const int cell : data.unavailable_cells_1based) {
        unavailable[static_cast<std::size_t>(cell - 1)] = 1;
    }
    int previous = 0;
    for (const int cell : result.best_layout_1based) {
        if (cell <= previous
            || cell < 1
            || cell > data.rows * data.cols
            || unavailable[static_cast<std::size_t>(cell - 1)] != 0) {
            throw std::runtime_error(
                result.algorithm_id + " returned an infeasible layout"
            );
        }
        previous = cell;
    }
    if ((result.algorithm_id == "agpso"
         || result.algorithm_id == "cgpso")
        && result.pso_update_semantics != "paper_staged_parallel") {
        throw std::runtime_error(
            result.algorithm_id + " returned an unregistered PSO semantics"
        );
    }
    if (result.algorithm_id == "hgpso"
        && result.pso_update_semantics
            != "paper_hierarchical_staged_parallel") {
        throw std::runtime_error(
            "hgpso returned an unregistered hierarchical PSO semantics"
        );
    }
}

int run_self_check(const Arguments& arguments) {
    const auto data = fode::load_case(arguments.cases_path, "WS1tn10");
    std::vector<char> blocked(
        static_cast<std::size_t>(data.rows * data.cols),
        0
    );
    for (const int cell : data.unavailable_cells_1based) {
        blocked[static_cast<std::size_t>(cell - 1)] = 1;
    }
    std::vector<double> fixed_layout;
    for (int cell = 1;
         cell <= data.rows * data.cols
            && fixed_layout.size()
                < static_cast<std::size_t>(data.turbine_count);
         ++cell) {
        if (blocked[static_cast<std::size_t>(cell - 1)] == 0) {
            fixed_layout.push_back(static_cast<double>(cell));
        }
    }
    {
        fode::PersistentExecutor evaluator_executor(arguments.workers);
        const auto total_only = fode::evaluate_population_hpc(
            fixed_layout,
            1,
            data,
            evaluator_executor,
            fode::EvaluationDetail::TotalOnly
        );
        const auto detailed = fode::evaluate_population_hpc(
            fixed_layout,
            1,
            data,
            evaluator_executor,
            fode::EvaluationDetail::TotalAndPerTurbine
        );
        if (total_only.fitness != detailed.fitness
            || detailed.accumulated_turbine_power_kw.size()
                != static_cast<std::size_t>(data.turbine_count)
            || detailed.turbine_position_order_1based.size()
                != static_cast<std::size_t>(data.turbine_count)) {
            throw std::runtime_error(
                "shared evaluator detail modes disagree on total power"
            );
        }
    }
    struct MatlabAnchor {
        const char* case_id;
        double expected_power_kw;
    };
    const std::vector<MatlabAnchor> anchors{
        {"WS1tn10", 4155.7130222215774},
        {"WS5tn30", 8159.0190328046474},
        {"WS10tn80", 20042.555195279721}
    };
    for (const auto& anchor : anchors) {
        const auto anchor_case =
            fode::load_case(arguments.cases_path, anchor.case_id);
        std::vector<char> anchor_blocked(
            static_cast<std::size_t>(anchor_case.rows * anchor_case.cols),
            0
        );
        for (const int cell : anchor_case.unavailable_cells_1based) {
            anchor_blocked[static_cast<std::size_t>(cell - 1)] = 1;
        }
        std::vector<double> layout;
        for (int cell = 1;
             cell <= anchor_case.rows * anchor_case.cols
                && layout.size()
                    < static_cast<std::size_t>(anchor_case.turbine_count);
             ++cell) {
            if (anchor_blocked[static_cast<std::size_t>(cell - 1)] == 0) {
                layout.push_back(static_cast<double>(cell));
            }
        }
        fode::PersistentExecutor executor(arguments.workers);
        const auto evaluated = fode::evaluate_population_hpc(
            layout,
            1,
            anchor_case,
            executor,
            fode::EvaluationDetail::TotalAndPerTurbine
        );
        const double error =
            std::abs(evaluated.fitness[0] - anchor.expected_power_kw);
        const double tolerance =
            0.01 + 1.0e-6 * std::abs(anchor.expected_power_kw);
        if (error > tolerance) {
            throw std::runtime_error(
                std::string("MATLAB evaluator anchor failed for ")
                + anchor.case_id
            );
        }
    }
    for (const std::string& algorithm : wflop::algorithm_ids()) {
        if (!wflop::algorithm_supports_problem(
                algorithm, arguments.problem
            )) {
            continue;
        }
        wflop::RunConfig config;
        config.algorithm_id = algorithm;
        config.problem_id = arguments.problem;
        config.compute_backend = arguments.compute_backend;
        config.seed = 20260728;
        config.physical_fes_budget = 480;
        config.workers = arguments.workers;
        config.sugga_model_root = arguments.models_path;
        config.rlfode_model_root = arguments.rlfode_models_path;
        config.fqfode_sensitivity_profile =
            arguments.fqfode_sensitivity_profile;
        config.alga_attention_hidden_width =
            arguments.alga_attention_hidden_width;
        const auto first = wflop::optimize(data, config);
        const auto second = wflop::optimize(data, config);
        validate_result(
            first,
            data,
            config.physical_fes_budget,
            config.workers
        );
        validate_result(
            second,
            data,
            config.physical_fes_budget,
            config.workers
        );
        if (first.best_expected_power_kw != second.best_expected_power_kw
            || first.best_layout_1based != second.best_layout_1based) {
            throw std::runtime_error(
                algorithm + " failed deterministic semantic replay"
            );
        }
        std::cout << algorithm << "\tpass\t"
                  << first.best_expected_power_kw << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse_arguments(argc, argv);
        if (arguments.list_algorithms) {
            for (const std::string& algorithm : wflop::algorithm_ids()) {
                std::cout << algorithm << "\n";
            }
            return 0;
        }
        if (arguments.list_problems) {
            for (const auto& problem : wflop::problem_descriptors()) {
                std::cout << problem.id << "\n";
            }
            return 0;
        }
        if (arguments.list_compute_backends) {
            for (const auto& backend : wflop::backend_descriptors()) {
                std::cout << backend.id << "\t"
                          << (backend.executable ? "supported" : "fail_closed")
                          << "\t" << backend.capability << "\n";
            }
            return 0;
        }
        if (arguments.list_training) {
            for (const auto& training : wflop::training_descriptors()) {
                std::cout << training.id << "\t" << training.algorithm_id
                          << "\t" << training.lifecycle << "\n";
            }
            return 0;
        }
        if (arguments.explain_compatibility) {
            const auto decision = wflop::explain_compatibility(
                arguments.explain_algorithm,
                arguments.explain_problem
            );
            std::cout
                << "{\"algorithm_id\":\""
                << escape_json(decision.algorithm_id)
                << "\",\"problem_id\":\""
                << escape_json(decision.problem_id)
                << "\",\"compatible\":"
                << (decision.compatible ? "true" : "false")
                << ",\"reason\":\"" << escape_json(decision.reason)
                << "\"}\n";
            return 0;
        }
        const auto& backend =
            wflop::backend_descriptor(arguments.compute_backend);
        if (!backend.executable) {
            throw std::runtime_error(
                "compute backend '" + arguments.compute_backend
                + "' is recognized but unavailable in this CPU build; "
                  "no hidden fallback was performed"
            );
        }
        if (arguments.check_ise_rename) {
            const auto data = fode::load_case(arguments.cases_path, "WS1tn10");
            wflop::RunConfig config;
            config.algorithm_id = "lse";
            config.problem_id = arguments.problem;
            config.physical_fes_budget = 1;
            config.workers = 1;
            try {
                static_cast<void>(wflop::optimize(data, config));
            } catch (const std::invalid_argument& error) {
                if (std::string(error.what()).find("renamed to ISE")
                    != std::string::npos) {
                    std::cout << "ise_rename_guard_pass\n";
                    return 0;
                }
                throw;
            }
            throw std::runtime_error("legacy lse runtime alias was accepted");
        }
        if (arguments.algorithm == "lse") {
            throw std::runtime_error(
                "algorithm 'lse' was renamed to ISE; use --algorithm ise"
            );
        }
        if (arguments.self_check) {
            return run_self_check(arguments);
        }
        const std::vector<std::string> algorithms =
            arguments.all_algorithms
                ? wflop::algorithm_ids()
                : (
                    arguments.algorithms.empty()
                        ? std::vector<std::string>{arguments.algorithm}
                        : arguments.algorithms
                );
#ifdef WFLOP_PLAN004_LIBTORCH
        wflop_learning::TorchThreadTopology torch_topology{};
        if (
            arguments.training_artifact != "not_applicable"
            && arguments.training_artifact != "train"
        ) {
            torch_topology =
                wflop_learning::configure_torch_thread_topology(
                    arguments.torch_intraop_threads,
                    arguments.torch_interop_threads
                );
        }
#else
        if (
            arguments.training_artifact != "not_applicable"
            && arguments.training_artifact != "train"
        ) {
            throw std::runtime_error(
                "learning artifact requires WFLOP_ENABLE_TORCH"
            );
        }
#endif
        const std::vector<fode::CaseData> cases = arguments.all_cases
            ? fode::load_cases(arguments.cases_path)
            : std::vector<fode::CaseData>{
                fode::load_case(arguments.cases_path, arguments.case_id)
            };
        std::ostringstream records;
        bool first_record = true;
        for (const std::string& algorithm : algorithms) {
            for (const auto& data : cases) {
                wflop::RunConfig config;
                config.algorithm_id = algorithm;
                config.problem_id = arguments.problem;
                config.compute_backend = arguments.compute_backend;
                config.paper_protocol_id = arguments.paper_protocol;
                config.training_artifact_id = arguments.training_artifact;
                if (
                    arguments.training_artifact != "not_applicable"
                    && arguments.training_artifact != "train"
                ) {
                    config.learning_artifact_path =
                        arguments.training_artifact;
                }
                config.seed = arguments.seed;
                config.physical_fes_budget = arguments.physical_fes;
                config.workers = arguments.workers;
                config.torch_intraop_threads =
                    arguments.torch_intraop_threads;
                config.torch_interop_threads =
                    arguments.torch_interop_threads;
                config.sugga_model_root = arguments.models_path;
                config.rlfode_model_root = arguments.rlfode_models_path;
                config.fqfode_sensitivity_profile =
                    arguments.fqfode_sensitivity_profile;
                config.alga_attention_hidden_width =
                    arguments.alga_attention_hidden_width;
                auto result = wflop::optimize(data, config);
#ifdef WFLOP_PLAN004_LIBTORCH
                if (
                    arguments.training_artifact != "not_applicable"
                    && arguments.training_artifact != "train"
                ) {
                    result.torch_intraop_threads =
                        torch_topology.intraop_threads;
                    result.torch_interop_threads =
                        torch_topology.interop_threads;
                }
#endif
                result.paper_protocol_id = config.paper_protocol_id;
                result.training_artifact_id = config.training_artifact_id;
                result.backend_id = wflop::backend_descriptor(
                    config.compute_backend
                ).id;
                validate_result(
                    result,
                    data,
                    arguments.physical_fes,
                    arguments.workers
                );
                if (!first_record) {
                    records << "\n";
                }
                first_record = false;
                records << to_json(result);
                std::cerr
                    << algorithm << " " << data.case_id
                    << " FES=" << result.physical_fes
                    << " best_kW=" << result.best_expected_power_kw
                    << " seconds=" << result.total_seconds << "\n";
            }
        }
        const std::string output = records.str();
        if (arguments.output_path.empty()) {
            std::cout << output << "\n";
        } else {
            write_output(arguments.output_path, output);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
