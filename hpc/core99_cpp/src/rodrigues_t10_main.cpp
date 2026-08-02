/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T10 evaluator and four-MOEA command-line runner
Paper/DOI: 10.1016/j.rser.2016.07.021
Public source, missing fields, reconstruction decisions, semantic IDs and
claim boundary: hpc/core99_cpp/include/core99/rodrigues_t10.hpp
Controlling contract: shared/contracts/core99_t10_rodrigues_2016.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/rodrigues_t10.hpp"

#include "fode/executor.hpp"

#include <bit>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string mode = "optimize";
    std::string case_id = "t10_B_4";
    std::string algorithm = "omogomea";
    std::string constraint = "repair";
    std::string output;
    std::uint64_t seed = 201607021ULL;
    std::uint64_t maximum_fes = 1000000ULL;
    int maximum_generations = 100000;
    int workers = 1;
    int batch_size = 2048;
    bool multi_resolution = false;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto next = [&]() {
            if (++index >= argc) throw std::invalid_argument("missing T10 option value");
            return std::string(argv[index]);
        };
        if (key == "--mode") result.mode = next();
        else if (key == "--case") result.case_id = next();
        else if (key == "--algorithm") result.algorithm = next();
        else if (key == "--constraint") result.constraint = next();
        else if (key == "--seed") result.seed = std::stoull(next());
        else if (key == "--maximum-fes") result.maximum_fes = std::stoull(next());
        else if (key == "--maximum-generations") {
            result.maximum_generations = std::stoi(next());
        } else if (key == "--workers") result.workers = std::stoi(next());
        else if (key == "--batch-size") result.batch_size = std::stoi(next());
        else if (key == "--multi-resolution") {
            const std::string value = next();
            result.multi_resolution = value == "1" || value == "true";
        } else if (key == "--output") result.output = next();
        else throw std::invalid_argument("unknown T10 option " + key);
    }
    return result;
}

void evaluation_json(
    std::ostream& output,
    const core99::t10::PaperCase& paper_case,
    const core99::t10::Evaluation& evaluation,
    const std::vector<std::uint64_t>& layout,
    const int requested_workers,
    const int observed_workers,
    const double seconds
) {
    output << "{\n"
        << "  \"mode\":\"evaluate\",\n"
        << "  \"case_id\":\"" << paper_case.case_id << "\",\n"
        << "  \"variables\":" << paper_case.variables << ",\n"
        << "  \"grid_step_diameters\":" << paper_case.grid_step_diameters << ",\n"
        << "  \"normalized_energy\":" << evaluation.normalized_energy << ",\n"
        << "  \"efficiency\":" << evaluation.efficiency << ",\n"
        << "  \"occupied_turbines\":" << evaluation.occupied_turbines << ",\n"
        << "  \"violating_pairs\":" << evaluation.violating_pairs << ",\n"
        << "  \"occupancy_words\":[";
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index != 0U) output << ',';
        output << layout[index];
    }
    output << "],\n"
        << "  \"requested_workers\":" << requested_workers << ",\n"
        << "  \"observed_workers\":" << observed_workers << ",\n"
        << "  \"evaluator_seconds\":" << seconds << "\n"
        << "}\n";
}

void optimization_json(
    std::ostream& output,
    const core99::t10::RunReceipt& receipt
) {
    output << "{\n"
        << "  \"mode\":\"optimize\",\n"
        << "  \"case_id\":\"" << receipt.case_id << "\",\n"
        << "  \"algorithm\":\"" << receipt.algorithm << "\",\n"
        << "  \"constraint_handling\":\"" << receipt.constraint_handling << "\",\n"
        << "  \"problem_semantic_id\":\"" << receipt.problem_semantic_id << "\",\n"
        << "  \"method_semantic_id\":\"" << receipt.method_semantic_id << "\",\n"
        << "  \"seed\":" << receipt.seed << ",\n"
        << "  \"requested_workers\":" << receipt.requested_workers << ",\n"
        << "  \"observed_workers\":" << receipt.observed_workers << ",\n"
        << "  \"physical_fes\":" << receipt.physical_fes << ",\n"
        << "  \"attempted_candidates\":" << receipt.attempted_candidates << ",\n"
        << "  \"rejected_infeasible_without_evaluation\":"
        << receipt.rejected_infeasible_without_evaluation << ",\n"
        << "  \"generations\":" << receipt.generations << ",\n"
        << "  \"final_grid_step_diameters\":"
        << receipt.final_grid_step_diameters << ",\n"
        << "  \"final_population\":" << receipt.final_population << ",\n"
        << "  \"archive_size\":" << receipt.archive_size << ",\n"
        << "  \"hypervolume\":" << receipt.hypervolume << ",\n"
        << "  \"evaluator_seconds\":" << receipt.evaluator_seconds << ",\n"
        << "  \"linkage_seconds\":" << receipt.linkage_seconds << ",\n"
        << "  \"algorithm_seconds\":" << receipt.algorithm_seconds << ",\n"
        << "  \"end_to_end_seconds\":" << receipt.end_to_end_seconds << ",\n"
        << "  \"scientific_hash\":" << receipt.scientific_hash << ",\n"
        << "  \"archive\":[";
    for (std::size_t index = 0; index < receipt.archive.size(); ++index) {
        if (index != 0U) output << ',';
        const auto& point = receipt.archive[index];
        output << "{\"normalized_energy\":" << point.normalized_energy
            << ",\"efficiency\":" << point.efficiency
            << ",\"occupied_turbines\":" << point.occupied_turbines
            << ",\"violating_pairs\":" << point.violating_pairs
            << ",\"occupancy_words\":[";
        for (std::size_t word = 0; word < point.occupancy_words.size(); ++word) {
            if (word != 0U) output << ',';
            output << point.occupancy_words[word];
        }
        output << "]}";
    }
    output << "]\n}\n";
}

std::uint64_t mix_hash(std::uint64_t state, const std::uint64_t value) {
    state ^= value + 0x9e3779b97f4a7c15ULL + (state << 6U) + (state >> 2U);
    return state;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        const core99::t10::Problem problem(arguments.case_id);
        std::ofstream file;
        std::ostream* output = &std::cout;
        if (!arguments.output.empty()) {
            file.open(arguments.output);
            if (!file) throw std::runtime_error("cannot open T10 output");
            output = &file;
        }
        *output << std::setprecision(17);
        if (arguments.mode == "evaluate") {
            fode::PersistentExecutor executor(arguments.workers);
            executor.reset_work_receipt();
            const auto layout = problem.initial_layout(arguments.seed, 0);
            const auto start = std::chrono::steady_clock::now();
            const auto evaluation = problem.evaluate(
                layout,
                core99::t10::parse_constraint(arguments.constraint),
                executor
            );
            const double seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start
            ).count();
            evaluation_json(
                *output, problem.paper_case(), evaluation, layout, arguments.workers,
                std::max(1, executor.work_receipt().distinct_participants),
                seconds
            );
        } else if (arguments.mode == "evaluate-batch") {
            if (arguments.batch_size <= 0) {
                throw std::invalid_argument("T10 batch size must be positive");
            }
            fode::PersistentExecutor executor(arguments.workers);
            std::vector<std::vector<std::uint64_t>> layouts(
                static_cast<std::size_t>(arguments.batch_size)
            );
            executor.parallel_for(0, arguments.batch_size, [&](const int index) {
                layouts[static_cast<std::size_t>(index)] = problem.initial_layout(
                    arguments.seed, static_cast<std::uint64_t>(index)
                );
            });
            executor.reset_work_receipt();
            const auto start = std::chrono::steady_clock::now();
            const auto evaluations = problem.evaluate_population(
                layouts,
                core99::t10::parse_constraint(arguments.constraint),
                executor
            );
            const double seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start
            ).count();
            std::uint64_t science_hash = 0xcbf29ce484222325ULL;
            double energy_sum = 0.0;
            double efficiency_sum = 0.0;
            for (const auto& evaluation : evaluations) {
                energy_sum += evaluation.normalized_energy;
                efficiency_sum += evaluation.efficiency;
                science_hash = mix_hash(science_hash, std::bit_cast<std::uint64_t>(
                    evaluation.normalized_energy
                ));
                science_hash = mix_hash(science_hash, std::bit_cast<std::uint64_t>(
                    evaluation.efficiency
                ));
            }
            *output << "{\n"
                << "  \"mode\":\"evaluate-batch\",\n"
                << "  \"case_id\":\"" << problem.paper_case().case_id << "\",\n"
                << "  \"batch_size\":" << arguments.batch_size << ",\n"
                << "  \"energy_sum\":" << energy_sum << ",\n"
                << "  \"efficiency_sum\":" << efficiency_sum << ",\n"
                << "  \"science_hash\":" << science_hash << ",\n"
                << "  \"requested_workers\":" << arguments.workers << ",\n"
                << "  \"observed_workers\":"
                << std::max(1, executor.work_receipt().distinct_participants) << ",\n"
                << "  \"evaluator_seconds\":" << seconds << "\n"
                << "}\n";
        } else if (arguments.mode == "optimize") {
            core99::t10::RunConfig config;
            config.algorithm = core99::t10::parse_algorithm(arguments.algorithm);
            config.constraint = core99::t10::parse_constraint(arguments.constraint);
            config.seed = arguments.seed;
            config.workers = arguments.workers;
            config.maximum_physical_fes = arguments.maximum_fes;
            config.maximum_generations = arguments.maximum_generations;
            config.multi_resolution = arguments.multi_resolution;
            optimization_json(*output, core99::t10::optimize(problem, config));
        } else {
            throw std::invalid_argument("unknown T10 mode " + arguments.mode);
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T10 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
