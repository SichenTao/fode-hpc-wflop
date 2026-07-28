#include "wflop/algorithms.hpp"

#include "fode/case.hpp"
#include "fode/evaluator.hpp"
#include "fode/executor.hpp"

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

namespace {

struct Arguments {
    std::string cases_path = "shared/contracts/benchmark_cases.json";
    std::string models_path = "shared/models/sugga_cpp";
    std::string algorithm = "fode";
    std::string case_id = "WS5tn30";
    std::string output_path;
    std::uint64_t seed = 20260728;
    std::uint64_t physical_fes = 24000;
    int workers = 20;
    bool all_cases = false;
    bool all_algorithms = false;
    bool list_algorithms = false;
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
        } else if (flag == "--algorithm") {
            result.algorithm = next();
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
        } else if (flag == "--all-cases") {
            result.all_cases = true;
        } else if (flag == "--all-algorithms") {
            result.all_algorithms = true;
        } else if (flag == "--list-algorithms") {
            result.list_algorithms = true;
        } else if (flag == "--self-check") {
            result.self_check = true;
        } else if (flag == "--check-ise-rename") {
            result.check_ise_rename = true;
        } else if (flag == "--help" || flag == "-h") {
            std::cout
                << "Usage: wflop_cpp_hpc [options]\n"
                << "  --algorithm ID       one of fode, aga, sugga, ise, agpso, cgpso, lshade, clshade\n"
                << "  --all-algorithms     run all eight algorithms sequentially\n"
                << "  --case ID            one frozen FODE-E0-L case\n"
                << "  --all-cases          run all 50 cases sequentially\n"
                << "  --physical-fes N     complete-layout evaluation budget per run\n"
                << "  --seed N             deterministic algorithm seed\n"
                << "  --workers N          persistent C++ thread-team size; formal Waffle value is 20\n"
                << "  --models PATH        frozen C++ SUGGA model directory\n"
                << "  --output PATH        write JSON or JSONL\n"
                << "  --self-check         bounded eight-algorithm semantic smoke\n"
                << "  --list-algorithms    print canonical algorithm IDs\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + flag);
        }
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
    output << "\"case_id\":\"" << escape_json(result.case_id) << "\",";
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
    output << "\"algorithm\":" << result.algorithm_seconds << "}";
    if (!result.pso_update_semantics.empty()) {
        output << ",\"pso_update_semantics\":\""
               << escape_json(result.pso_update_semantics) << "\"";
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
    std::ofstream stream(output_path);
    if (!stream) {
        throw std::runtime_error("cannot write output: " + path);
    }
    stream << contents;
    if (contents.empty() || contents.back() != '\n') {
        stream << '\n';
    }
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
    if (result.observed_workers != expected_workers) {
        throw std::runtime_error(
            result.algorithm_id + " did not use the requested persistent team"
        );
    }
    if (result.best_layout_1based.empty()
        || result.algorithm_provenance.empty()
        || result.effective_semantics_id.empty()
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
        wflop::RunConfig config;
        config.algorithm_id = algorithm;
        config.seed = 20260728;
        config.physical_fes_budget = 480;
        config.workers = arguments.workers;
        config.sugga_model_root = arguments.models_path;
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
        if (arguments.check_ise_rename) {
            const auto data = fode::load_case(arguments.cases_path, "WS1tn10");
            wflop::RunConfig config;
            config.algorithm_id = "lse";
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
        const std::vector<std::string> algorithms = arguments.all_algorithms
            ? wflop::algorithm_ids()
            : std::vector<std::string>{arguments.algorithm};
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
                config.seed = arguments.seed;
                config.physical_fes_budget = arguments.physical_fes;
                config.workers = arguments.workers;
                config.sugga_model_root = arguments.models_path;
                const auto result = wflop::optimize(data, config);
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
