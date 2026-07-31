/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T64 pure-C++ paper-case CLI
Paper DOI: The Impact of Land Use Constraints in Multi-Objective
Energy-Noise Wind Farm Layout Optimization; 10.1016/j.renene.2015.06.026
Public source: no target source or native arrays were located.
Related public source:
https://gitlab.windenergy.dtu.dk/TOPFARM/PyWake.git at revision
5b07481ec9b3633a74844651648f266ba82a8b32 for an independent ISO check.
Missing/conflicts/reconstruction, semantic IDs, HPC design, and claim boundary:
include/core99/sorkhabi_t64.hpp
Shared project-native implementation: the verified same-lineage T72 physical
evaluator and NSGA-II kernel.
Contract: shared/contracts/core99_t64_sorkhabi_2016.json
Claim boundary: declared academic reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/sorkhabi_t64.hpp"

#include <cmath>
#include <cstdlib>
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
    int availability = 80;
    int turbines = 10;
    int map_variant = 0;
    core99::t64::PenaltyMode penalty =
        core99::t64::PenaltyMode::dynamic_cgen_ngen;
    int workers = 20;
    std::uint64_t physical_fes = 80000;
    std::uint64_t seed = 20260731;
    std::filesystem::path output;
    bool enable_convergence = true;
};

core99::t64::PenaltyMode parse_penalty(const std::string& value) {
    if (value == "static_1e4") {
        return core99::t64::PenaltyMode::static_1e4;
    }
    if (value == "static_4e4") {
        return core99::t64::PenaltyMode::static_4e4;
    }
    if (value == "dynamic_cgen_ngen") {
        return core99::t64::PenaltyMode::dynamic_cgen_ngen;
    }
    if (value == "dynamic_cgen_half_ngen") {
        return core99::t64::PenaltyMode::dynamic_cgen_half_ngen;
    }
    if (value == "death") return core99::t64::PenaltyMode::death;
    throw std::invalid_argument("T64 unknown penalty mode " + value);
}

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) {
                throw std::invalid_argument(
                    "T64 missing value for " + key
                );
            }
            return std::string(argv[index]);
        };
        if (key == "--mode") result.mode = value();
        else if (key == "--land-availability-percent") {
            result.availability = std::stoi(value());
        } else if (key == "--turbines") {
            result.turbines = std::stoi(value());
        } else if (key == "--map-variant") {
            result.map_variant = std::stoi(value());
        } else if (key == "--penalty-mode") {
            result.penalty = parse_penalty(value());
        } else if (key == "--workers") {
            result.workers = std::stoi(value());
        } else if (key == "--physical-fes") {
            result.physical_fes = std::stoull(value());
        } else if (key == "--seed") {
            result.seed = std::stoull(value());
        } else if (key == "--output") {
            result.output = value();
        } else if (key == "--disable-convergence") {
            result.enable_convergence = false;
        } else {
            throw std::invalid_argument("T64 unknown option " + key);
        }
    }
    return result;
}

std::string layout_json(
    const std::vector<core99::t72::Point>& layout
) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index > 0) output << ',';
        output << '[' << layout[index].x_m
            << ',' << layout[index].y_m << ']';
    }
    output << ']';
    return output.str();
}

std::string evaluation_json(
    const core99::t72::Evaluation& value
) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"aep_gwh\":" << value.aep_gwh
        << ",\"maximum_spl_dba\":" << value.maximum_spl_dba
        << ",\"proximity_violation_m\":"
        << value.proximity_violation_m
        << ",\"regulatory_violation_m\":"
        << value.regulatory_violation_m
        << ",\"feasible\":"
        << (value.feasible ? "true" : "false") << '}';
    return output.str();
}

std::string front_json(
    const std::vector<core99::t72::FrontPoint>& front
) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < front.size(); ++index) {
        if (index > 0) output << ',';
        output << "{\"aep_gwh\":" << front[index].aep_gwh
            << ",\"maximum_spl_dba\":"
            << front[index].maximum_spl_dba
            << ",\"layout\":" << layout_json(front[index].layout)
            << '}';
    }
    output << ']';
    return output.str();
}

std::string run_json(const core99::t64::RunResult& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"problem_id\":\"" << value.problem_id
        << "\",\"problem_semantic_id\":\""
        << value.problem_semantic_id
        << "\",\"method_semantic_id\":\""
        << value.method_semantic_id
        << "\",\"protocol_semantic_id\":\""
        << value.protocol_semantic_id
        << "\",\"penalty_mode\":\"" << value.penalty_mode
        << "\",\"seed\":" << value.seed
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"physical_fes\":" << value.physical_fes
        << ",\"generations\":" << value.generations
        << ",\"population_size\":" << value.population_size
        << ",\"converged\":"
        << (value.converged ? "true" : "false")
        << ",\"measured_land_availability\":"
        << value.measured_land_availability
        << ",\"uniformity_parameter\":"
        << value.uniformity_parameter
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex
        << value.scientific_hash << std::dec
        << "\",\"front\":" << front_json(value.front) << '}';
    return output.str();
}

std::vector<core99::t72::Point> reference_layout(
    const core99::t64::Problem& problem
) {
    std::vector<core99::t72::Point> result;
    for (double y = 100.0; y <= 2900.0; y += 100.0) {
        for (double x = 100.0; x <= 2900.0; x += 100.0) {
            const core99::t72::Point candidate{x, y};
            if (
                problem.shared_evaluator()
                    .regulatory_forbidden(candidate)
            ) {
                continue;
            }
            bool spacing = true;
            for (const auto& existing : result) {
                if (
                    std::hypot(
                        candidate.x_m - existing.x_m,
                        candidate.y_m - existing.y_m
                    ) < 385.0
                ) {
                    spacing = false;
                    break;
                }
            }
            if (!spacing) continue;
            result.push_back(candidate);
            if (
                static_cast<int>(result.size())
                == problem.turbine_count()
            ) {
                return result;
            }
        }
    }
    throw std::runtime_error("T64 reference layout unavailable");
}

void emit(
    const std::string& payload,
    const std::filesystem::path& output
) {
    if (output.empty()) {
        std::cout << payload << '\n';
        return;
    }
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    std::ofstream stream(output);
    if (!stream) throw std::runtime_error("T64 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.mode == "list-cases") {
            std::ostringstream output;
            output << "{\"paper_case_roles\":[";
            const auto cases = core99::t64::paper_case_ids();
            for (std::size_t index = 0; index < cases.size(); ++index) {
                if (index > 0) output << ',';
                output << '"' << cases[index] << '"';
            }
            output << "],\"role_count\":" << cases.size()
                << ",\"unique_instance_count\":12}";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        const core99::t64::Problem problem(
            arguments.availability,
            arguments.turbines,
            arguments.map_variant
        );
        if (arguments.mode == "inspect") {
            std::ostringstream output;
            output << std::setprecision(17)
                << "{\"problem_id\":\"" << problem.id()
                << "\",\"problem_semantic_id\":"
                   "\"t64_energy_noise_land13role_declared_reconstruction_v1\""
                << ",\"land_availability_percent\":"
                << problem.land_availability_percent()
                << ",\"turbines\":" << problem.turbine_count()
                << ",\"map_variant\":" << problem.map_variant()
                << ",\"population_size\":" << problem.population_size()
                << ",\"measured_land_availability\":"
                << problem.measured_land_availability()
                << ",\"forbidden_polygons_and_receptors\":"
                << problem.receptors().size()
                << ",\"uniformity_parameter\":"
                << problem.uniformity_parameter()
                << ",\"paper_physical_fes\":80000}";
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode == "evaluate") {
            const auto layout = reference_layout(problem);
            std::ostringstream output;
            output << "{\"problem_id\":\"" << problem.id()
                << "\",\"evaluation\":"
                << evaluation_json(problem.evaluate(layout))
                << ",\"layout\":" << layout_json(layout) << '}';
            emit(output.str(), arguments.output);
            return EXIT_SUCCESS;
        }
        if (arguments.mode != "optimize") {
            throw std::invalid_argument(
                "T64 mode must be list-cases, inspect, evaluate, or optimize"
            );
        }
        core99::t64::RunConfig config;
        config.seed = arguments.seed;
        config.workers = arguments.workers;
        config.physical_fes = arguments.physical_fes;
        config.penalty_mode = arguments.penalty;
        config.enable_convergence = arguments.enable_convergence;
        emit(
            run_json(core99::t64::run(problem, config)),
            arguments.output
        );
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T64 failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
