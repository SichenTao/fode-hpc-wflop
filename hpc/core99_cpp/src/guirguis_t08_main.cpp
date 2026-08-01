/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T08 command-line runner for evaluator, gradient and
multi-start interior-point production roles
Paper/DOI: 10.1016/j.apenergy.2016.06.101
Public source: no paper-linked author implementation was found.
Missing fields, declared reconstruction, semantic IDs, HPC design and Claim boundary:
hpc/core99_cpp/include/core99/guirguis_t08.hpp
Controlling contract: shared/contracts/core99_t08_guirguis_2016.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "core99/guirguis_t08.hpp"

#include "fode/executor.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string mode = "optimize";
    std::string case_id = "t08_benchmark_c2_n20";
    std::string start_policy = "usl";
    std::string output;
    int starts = 1;
    int workers = 1;
    int maximum_evaluations = 1500;
    int barrier_phases = 6;
    std::uint64_t seed = 201606101ULL;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto next = [&]() -> std::string {
            if (++index >= argc) throw std::invalid_argument("missing T08 option value");
            return argv[index];
        };
        if (key == "--mode") result.mode = next();
        else if (key == "--case") result.case_id = next();
        else if (key == "--start-policy") result.start_policy = next();
        else if (key == "--starts") result.starts = std::stoi(next());
        else if (key == "--workers") result.workers = std::stoi(next());
        else if (key == "--maximum-evaluations") {
            result.maximum_evaluations = std::stoi(next());
        } else if (key == "--barrier-phases") {
            result.barrier_phases = std::stoi(next());
        } else if (key == "--seed") result.seed = std::stoull(next());
        else if (key == "--output") result.output = next();
        else throw std::invalid_argument("unknown T08 option " + key);
    }
    return result;
}

core99::t08::StartPolicy start_policy(const std::string& value) {
    if (value == "usl") return core99::t08::StartPolicy::uniform_staggered;
    if (value == "lhs" || value == "rsl") {
        return core99::t08::StartPolicy::latin_hypercube_feasible;
    }
    throw std::invalid_argument("unknown T08 start policy " + value);
}

void point_array(
    std::ostream& output,
    const std::vector<core99::t08::Point>& layout
) {
    output << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index != 0U) output << ',';
        output << '[' << layout[index].x_m << ',' << layout[index].y_m << ']';
    }
    output << ']';
}

void evaluation_json(
    std::ostream& output,
    const core99::t08::PaperCase& paper_case,
    const core99::t08::Evaluation& evaluation,
    const core99::t08::ConstraintReceipt& constraints,
    const std::vector<core99::t08::Point>& layout
) {
    double largest_gradient = 0.0;
    for (const double value : evaluation.gradient_percent_per_m) {
        largest_gradient = std::max(largest_gradient, std::abs(value));
    }
    output << "{\n"
        << "  \"mode\":\"evaluate\",\n"
        << "  \"case_id\":\"" << paper_case.case_id << "\",\n"
        << "  \"problem_semantic_id\":\"" << paper_case.problem_semantic_id << "\",\n"
        << "  \"turbine_count\":" << paper_case.turbine_count << ",\n"
        << "  \"wind_state_count\":" << paper_case.wind_states.size() << ",\n"
        << "  \"efficiency_percent\":" << evaluation.efficiency_percent << ",\n"
        << "  \"maximum_abs_gradient_percent_per_m\":" << largest_gradient << ",\n"
        << "  \"maximum_constraint_violation\":" << constraints.maximum_violation << ",\n"
        << "  \"minimum_normalized_margin\":" << constraints.minimum_normalized_margin << ",\n"
        << "  \"requested_workers\":" << evaluation.requested_workers << ",\n"
        << "  \"observed_workers\":" << evaluation.observed_workers << ",\n"
        << "  \"evaluator_seconds\":" << evaluation.seconds << ",\n"
        << "  \"constraint_seconds\":" << constraints.seconds << ",\n"
        << "  \"layout\":";
    point_array(output, layout);
    output << "\n}\n";
}

void optimization_json(
    std::ostream& output,
    const core99::t08::OptimizationReceipt& receipt
) {
    output << "{\n"
        << "  \"mode\":\"optimize\",\n"
        << "  \"case_id\":\"" << receipt.case_id << "\",\n"
        << "  \"problem_semantic_id\":\"" << receipt.problem_semantic_id << "\",\n"
        << "  \"method_semantic_id\":\"" << receipt.method_semantic_id << "\",\n"
        << "  \"start_policy\":\"" << receipt.start_policy << "\",\n"
        << "  \"starts\":" << receipt.starts << ",\n"
        << "  \"seed\":" << receipt.seed << ",\n"
        << "  \"requested_workers\":" << receipt.requested_workers << ",\n"
        << "  \"observed_workers\":" << receipt.observed_workers << ",\n"
        << "  \"physical_layout_evaluations\":"
        << receipt.physical_layout_evaluations << ",\n"
        << "  \"best_efficiency_percent\":" << receipt.best_efficiency_percent << ",\n"
        << "  \"maximum_constraint_violation\":"
        << receipt.maximum_constraint_violation << ",\n"
        << "  \"minimum_spacing_m\":" << receipt.minimum_spacing_m << ",\n"
        << "  \"initialization_seconds\":" << receipt.initialization_seconds << ",\n"
        << "  \"evaluator_cpu_seconds\":" << receipt.evaluator_seconds << ",\n"
        << "  \"constraint_cpu_seconds\":" << receipt.constraint_seconds << ",\n"
        << "  \"optimizer_cpu_seconds\":" << receipt.optimizer_seconds << ",\n"
        << "  \"end_to_end_seconds\":" << receipt.end_to_end_seconds << ",\n"
        << "  \"scientific_hash\":" << receipt.scientific_hash << ",\n"
        << "  \"best_layout\":";
    point_array(output, receipt.best_layout);
    output << ",\n  \"start_receipts\":[";
    for (std::size_t index = 0; index < receipt.start_receipts.size(); ++index) {
        if (index != 0U) output << ',';
        const auto& start = receipt.start_receipts[index];
        output << "{\"start_index\":" << start.start_index
            << ",\"initial_efficiency_percent\":"
            << start.initial_efficiency_percent
            << ",\"final_efficiency_percent\":"
            << start.final_efficiency_percent
            << ",\"maximum_constraint_violation\":"
            << start.maximum_constraint_violation
            << ",\"minimum_spacing_m\":" << start.minimum_spacing_m
            << ",\"objective_gradient_evaluations\":"
            << start.objective_gradient_evaluations
            << ",\"accepted_steps\":" << start.accepted_steps
            << ",\"barrier_phases_completed\":"
            << start.barrier_phases_completed
            << ",\"termination\":\"" << start.termination << "\"}";
    }
    output << "]\n}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        const core99::t08::Problem problem(arguments.case_id);
        std::ofstream file;
        std::ostream* output = &std::cout;
        if (!arguments.output.empty()) {
            file.open(arguments.output);
            if (!file) throw std::runtime_error("cannot open T08 output");
            output = &file;
        }
        *output << std::setprecision(17);
        if (arguments.mode == "evaluate") {
            fode::PersistentExecutor executor(arguments.workers);
            const auto layout = problem.initial_layout(
                start_policy(arguments.start_policy), arguments.seed, 0
            );
            const auto evaluation = problem.evaluate(layout, true, executor);
            const auto constraints = problem.barrier(layout, 0.0, false, executor);
            evaluation_json(*output, problem.paper_case(), evaluation,
                            constraints, layout);
        } else if (arguments.mode == "optimize") {
            core99::t08::OptimizationConfig config;
            config.start_policy = start_policy(arguments.start_policy);
            config.starts = arguments.starts;
            config.seed = arguments.seed;
            config.workers = arguments.workers;
            config.maximum_evaluations_per_start = arguments.maximum_evaluations;
            config.barrier_phases = arguments.barrier_phases;
            optimization_json(*output, core99::t08::optimize(problem, config));
        } else {
            throw std::invalid_argument("unknown T08 mode " + arguments.mode);
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T08 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
