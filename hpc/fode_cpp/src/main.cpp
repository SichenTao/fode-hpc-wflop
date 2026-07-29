#include "fode/case.hpp"
#include "fode/evaluator.hpp"
#include "fode/optimizer.hpp"

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
    std::string case_id = "WS5tn30";
    std::string output_path;
    std::uint64_t seed = 20260728;
    std::uint64_t physical_fes = 24000;
    int workers = 0;
    bool self_check = false;
    bool all_cases = false;
    bool profile_phases = false;
};

std::uint64_t parse_u64(const std::string& value, const std::string& flag) {
    std::size_t consumed = 0;
    const std::uint64_t parsed = std::stoull(value, &consumed);
    if (consumed != value.size()) {
        throw std::runtime_error("invalid value for " + flag + ": " + value);
    }
    return parsed;
}

Arguments parse_arguments(int argc, char** argv) {
    Arguments result;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value after " + flag);
            }
            return argv[++i];
        };
        if (flag == "--cases") {
            result.cases_path = value();
        } else if (flag == "--case") {
            result.case_id = value();
        } else if (flag == "--output") {
            result.output_path = value();
        } else if (flag == "--seed") {
            result.seed = parse_u64(value(), flag);
        } else if (flag == "--physical-fes") {
            result.physical_fes = parse_u64(value(), flag);
        } else if (flag == "--workers") {
            result.workers = static_cast<int>(parse_u64(value(), flag));
        } else if (flag == "--self-check") {
            result.self_check = true;
        } else if (flag == "--all-cases") {
            result.all_cases = true;
        } else if (flag == "--profile-phases") {
            result.profile_phases = true;
        } else if (flag == "--help" || flag == "-h") {
            std::cout
                << "Usage: fode_cpp_hpc [options]\n"
                << "  --cases PATH          frozen 50-case JSON contract\n"
                << "  --case CASE_ID        one FODE-E0-L case\n"
                << "  --all-cases           run all 50 cases sequentially\n"
                << "  --physical-fes N      exact complete-layout budget\n"
                << "  --seed N              deterministic FODE seed\n"
                << "  --workers N           worker count; 0 means all visible CPUs\n"
                << "  --output PATH         JSON or JSONL result path\n"
                << "  --profile-phases      collect 17-stage diagnostic timing\n"
                << "  --self-check          run bounded native checks\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + flag);
        }
    }
    if (result.self_check && result.all_cases) {
        throw std::runtime_error(
            "--self-check and --all-cases are mutually exclusive"
        );
    }
    return result;
}

std::string json_escape(const std::string& value) {
    std::ostringstream output;
    for (const char ch : value) {
        switch (ch) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default: output << ch; break;
        }
    }
    return output.str();
}

std::string result_json(const fode::RunResult& result) {
    static constexpr const char* phase_names[17] = {
        "setup_and_initial_population",
        "initial_evaluator",
        "ranking_and_snapshot",
        "parameter_sampling",
        "fractional_mutation_and_crossover",
        "repair",
        "population_evaluator",
        "best_update",
        "selection_flags",
        "archive_update",
        "selection_and_memory_adaptation",
        "history_shift",
        "population_reduction",
        "progress_ledger",
        "local_generation_and_repair",
        "local_evaluator",
        "finalization",
    };
    std::ostringstream output;
    output << std::setprecision(17);
    output << "{";
    output << "\"schema_version\":1,";
    output << "\"method_id\":\"FODE_CPP_HPC_FULL\",";
    output << "\"case_id\":\"" << json_escape(result.case_id) << "\",";
    output << "\"seed\":" << result.seed << ",";
    output << "\"physical_fes\":" << result.physical_fes << ",";
    output << "\"generations\":" << result.generations << ",";
    output << "\"initial_population\":" << result.initial_population << ",";
    output << "\"final_population\":" << result.final_population << ",";
    output << "\"requested_workers\":" << result.requested_workers << ",";
    output << "\"observed_workers\":" << result.observed_workers << ",";
    output << "\"best_expected_power_kw\":"
           << result.best_expected_power_kw << ",";
    output << "\"best_layout_1based\":[";
    for (std::size_t i = 0; i < result.best_layout_1based.size(); ++i) {
        if (i != 0) {
            output << ",";
        }
        output << result.best_layout_1based[i];
    }
    output << "],";
    output << "\"timing_seconds\":{";
    output << "\"end_to_end\":" << result.total_seconds << ",";
    output << "\"evaluator\":" << result.evaluator_seconds << ",";
    output << "\"algorithm\":"
           << result.algorithm_seconds;
    output << "},";
    output << "\"profiling_enabled\":"
           << (result.profiling_enabled ? "true" : "false");
    if (result.profiling_enabled) {
        output << ",\"phase_seconds\":{";
        for (std::size_t i = 0; i < result.phase_seconds.size(); ++i) {
            if (i != 0) {
                output << ",";
            }
            output << "\"" << phase_names[i] << "\":"
                   << result.phase_seconds[i];
        }
        output << "}";
    }
    output << "}";
    return output.str();
}

void write_output(const std::string& path, const std::string& content) {
    if (path.empty()) {
        return;
    }
    const std::filesystem::path output_path(path);
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream stream(output_path);
    if (!stream) {
        throw std::runtime_error("cannot write result: " + path);
    }
    stream << content;
    if (content.empty() || content.back() != '\n') {
        stream << '\n';
    }
}

std::vector<double> first_feasible_layout(
    const fode::CaseData& data,
    int batch
) {
    const int grid_dimension = data.rows * data.cols;
    std::vector<char> blocked(
        static_cast<std::size_t>(grid_dimension),
        0
    );
    for (const int cell : data.unavailable_cells_1based) {
        blocked[static_cast<std::size_t>(cell - 1)] = 1;
    }
    std::vector<int> selected;
    for (
        int cell = 1;
        cell <= grid_dimension
            && static_cast<int>(selected.size()) < data.turbine_count;
        ++cell
    ) {
        if (blocked[static_cast<std::size_t>(cell - 1)] == 0) {
            selected.push_back(cell);
        }
    }
    std::vector<double> result(
        static_cast<std::size_t>(batch * data.turbine_count)
    );
    for (int row = 0; row < batch; ++row) {
        for (int d = 0; d < data.turbine_count; ++d) {
            result[static_cast<std::size_t>(
                row * data.turbine_count + d
            )] = static_cast<double>(selected[static_cast<std::size_t>(d)]);
        }
    }
    return result;
}

void run_self_check(const Arguments& arguments, int workers) {
    const std::vector<fode::CaseData> cases =
        fode::load_cases(arguments.cases_path);
    if (cases.size() != 50) {
        throw std::runtime_error("case-count self-check failed");
    }

    int observed_team = 0;
#pragma omp parallel num_threads(workers)
    {
#pragma omp single
        observed_team = omp_get_num_threads();
    }
    if (observed_team != workers) {
        throw std::runtime_error("OpenMP did not create the full affinity team");
    }

    struct MatlabAnchor {
        const char* case_id;
        double row_cluster_fitness_kw;
    };
    const std::vector<MatlabAnchor> anchors = {
        {"WS1tn10", 4155.7130222215774},
        {"WS5tn30", 8159.0190328046474},
        {"WS10tn80", 20042.555195279721},
    };
    double maximum_matlab_absolute_error_kw = 0.0;
    for (const MatlabAnchor& anchor : anchors) {
        const std::string case_id = anchor.case_id;
        const fode::CaseData data =
            fode::load_case(arguments.cases_path, case_id);
        const std::vector<double> layout =
            first_feasible_layout(data, 2);
        const fode::Evaluation first =
            fode::evaluate_population_hpc(layout, 2, data, workers);
        const fode::Evaluation second =
            fode::evaluate_population_hpc(layout, 2, data, workers);
        if (first.fitness != second.fitness
            || first.observed_workers != workers
            || second.observed_workers != workers) {
            throw std::runtime_error(
                "thread-invariant evaluator self-check failed for " + case_id
            );
        }
        for (const double fitness : first.fitness) {
            if (!std::isfinite(fitness) || fitness <= 0.0) {
                throw std::runtime_error(
                    "invalid evaluator result for " + case_id
                );
            }
        }
        const double error = std::abs(
            first.fitness[0] - anchor.row_cluster_fitness_kw
        );
        maximum_matlab_absolute_error_kw = std::max(
            maximum_matlab_absolute_error_kw,
            error
        );
        const double tolerance =
            0.01 + 1.0e-6 * std::abs(anchor.row_cluster_fitness_kw);
        if (error > tolerance) {
            throw std::runtime_error(
                "MATLAB evaluator anchor failed for " + case_id
            );
        }
    }

    const fode::CaseData smoke_case =
        fode::load_case(arguments.cases_path, "WS5tn30");
    fode::RunConfig config;
    config.seed = arguments.seed;
    config.physical_fes_budget = 300;
    config.workers = workers;
    const fode::RunResult first =
        fode::optimize_fode_hpc(smoke_case, config);
    const fode::RunResult second =
        fode::optimize_fode_hpc(smoke_case, config);
    if (first.physical_fes != 300 || second.physical_fes != 300
        || first.best_expected_power_kw != second.best_expected_power_kw
        || first.best_layout_1based != second.best_layout_1based
        || first.observed_workers != workers
        || second.observed_workers != workers) {
        throw std::runtime_error("complete FODE deterministic smoke failed");
    }

    std::cout
        << "{\"status\":\"PASS\",\"case_count\":50,"
        << "\"affinity_workers\":" << workers << ","
        << "\"observed_workers\":" << observed_team << ","
        << "\"matlab_anchor_count\":3,"
        << "\"maximum_matlab_absolute_error_kw\":"
        << std::setprecision(17) << maximum_matlab_absolute_error_kw << ","
        << "\"smoke_physical_fes\":300,"
        << "\"smoke_best_expected_power_kw\":"
        << std::setprecision(17) << first.best_expected_power_kw
        << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse_arguments(argc, argv);
        omp_set_dynamic(0);
        const int workers =
            arguments.workers > 0 ? arguments.workers : omp_get_num_procs();
        if (workers <= 0) {
            throw std::runtime_error("OpenMP reports no available processors");
        }
        omp_set_num_threads(workers);

        if (arguments.self_check) {
            run_self_check(arguments, workers);
            return 0;
        }

        fode::RunConfig config;
        config.seed = arguments.seed;
        config.physical_fes_budget = arguments.physical_fes;
        config.workers = workers;
        config.profile_phases = arguments.profile_phases;

        if (arguments.all_cases) {
            const std::vector<fode::CaseData> cases =
                fode::load_cases(arguments.cases_path);
            std::ostringstream jsonl;
            for (const fode::CaseData& data : cases) {
                const fode::RunResult result =
                    fode::optimize_fode_hpc(data, config);
                const std::string record = result_json(result);
                std::cout << record << '\n';
                jsonl << record << '\n';
            }
            write_output(arguments.output_path, jsonl.str());
            return 0;
        }

        const fode::CaseData data =
            fode::load_case(arguments.cases_path, arguments.case_id);
        const fode::RunResult result =
            fode::optimize_fode_hpc(data, config);
        const std::string record = result_json(result);
        std::cout << record << '\n';
        write_output(arguments.output_path, record);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fode_cpp_hpc: " << error.what() << '\n';
        return 1;
    }
}
