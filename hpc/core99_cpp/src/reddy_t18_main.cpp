/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T18 pure-C++ CPU-HPC command line and JSON receipt
Paper/DOI: Reddy 2020; 10.1016/j.apenergy.2020.115090.
Public assets, missing fields, conflicts, corrections, declared completions,
semantic IDs and claim boundary: include/core99/reddy_t18.hpp.
Controlling contract: shared/contracts/core99_t18_reddy_2020.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/reddy_t18.hpp"

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
    std::string action = "run";
    core99::t18::RunConfig config;
    std::filesystem::path output;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) throw std::invalid_argument("T18 missing " + key);
            return std::string(argv[index]);
        };
        if (key == "--action") result.action = value();
        else if (key == "--seed") result.config.seed = std::stoull(value());
        else if (key == "--workers") result.config.workers = std::stoi(value());
        else if (key == "--population") result.config.population = std::stoi(value());
        else if (key == "--generations") result.config.generations = std::stoi(value());
        else if (key == "--stagnation-generations") {
            result.config.stagnation_generations = std::stoi(value());
        } else if (key == "--disk-quadrature-points") {
            result.config.disk_quadrature_points = std::stoi(value());
        } else if (key == "--validation-disk-quadrature-points") {
            result.config.validation_disk_quadrature_points = std::stoi(value());
        } else if (key == "--terrain-profile") {
            const std::string item = value();
            if (item == "paper_local_rbf") {
                result.config.terrain_profile =
                    core99::t18::TerrainProfile::paper_local_rbf;
            } else if (item == "source_example_idw") {
                result.config.terrain_profile =
                    core99::t18::TerrainProfile::source_example_idw;
            } else {
                throw std::invalid_argument("T18 unknown terrain profile");
            }
        } else if (key == "--disk-sampling") {
            const std::string item = value();
            if (item == "paper_area_correct") {
                result.config.disk_sampling =
                    core99::t18::DiskSampling::paper_area_correct;
            } else if (item == "source_uniform_radius") {
                result.config.disk_sampling =
                    core99::t18::DiskSampling::source_uniform_radius;
            } else {
                throw std::invalid_argument("T18 unknown disk sampling");
            }
        } else if (key == "--validation-disk-sampling") {
            const std::string item = value();
            if (item == "paper_area_correct") {
                result.config.validation_disk_sampling =
                    core99::t18::DiskSampling::paper_area_correct;
            } else if (item == "source_uniform_radius") {
                result.config.validation_disk_sampling =
                    core99::t18::DiskSampling::source_uniform_radius;
            } else {
                throw std::invalid_argument("T18 unknown validation disk sampling");
            }
        } else if (key == "--output") result.output = value();
        else throw std::invalid_argument("T18 unknown option " + key);
    }
    return result;
}

std::string evaluation_json(const core99::t18::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"feasible\":" << (value.feasible ? "true" : "false")
        << ",\"constraint_violation\":" << value.constraint_violation
        << ",\"annual_energy_mwh\":" << value.annual_energy_mwh
        << ",\"normalized_aep\":" << value.normalized_aep
        << ",\"farm_power_mw\":" << value.farm_power_mw
        << ",\"farm_efficiency\":" << value.farm_efficiency
        << ",\"land_used_km2\":" << value.land_used_km2
        << ",\"farm_cost_usd\":" << value.farm_cost_usd
        << ",\"coe_usd_kwh\":" << value.coe_usd_kwh << '}';
    return out.str();
}

std::string layout_json(const std::vector<core99::t18::Turbine>& layout) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index) out << ',';
        const auto& turbine = layout[index];
        out << "{\"x_m\":" << turbine.x_m
            << ",\"y_m\":" << turbine.y_m
            << ",\"diameter_m\":" << turbine.diameter_m
            << ",\"height_m\":" << turbine.height_m << '}';
    }
    return out.str() + ']';
}

std::string validation_json(
    const std::vector<core99::t18::ValidationRecord>& validation,
    const core99::t18::DiskSampling sampling
) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"disk_sampling\":\"" << core99::t18::to_string(sampling)
        << "\",\"validation\":[";
    for (std::size_t index = 0; index < validation.size(); ++index) {
        if (index) out << ',';
        const auto& item = validation[index];
        out << "{\"role\":\"" << item.role
            << "\",\"wake\":\"" << core99::t18::to_string(item.wake)
            << "\",\"merge\":\"" << core99::t18::to_string(item.merge)
            << "\",\"predicted_velocity_mps\":" << item.predicted_velocity_mps
            << ",\"experimental_velocity_mps\":" << item.experimental_velocity_mps
            << ",\"relative_error_percent\":" << item.relative_error_percent << '}';
    }
    return out.str() + "]}";
}

std::string result_json(const core99::t18::RunResult& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"case_id\":\"" << value.case_id
        << "\",\"method_semantic_id\":\"" << value.method_semantic_id
        << "\",\"problem_semantic_id\":\"" << value.problem_semantic_id
        << "\",\"protocol_semantic_id\":\"" << value.protocol_semantic_id
        << "\",\"seed\":" << value.seed
        << ",\"requested_workers\":" << value.requested_workers
        << ",\"observed_workers\":" << value.observed_workers
        << ",\"parallel_regions\":" << value.parallel_regions
        << ",\"population\":" << value.population
        << ",\"generations\":" << value.generations
        << ",\"stagnation_generations\":" << value.stagnation_generations
        << ",\"disk_quadrature_points\":" << value.disk_quadrature_points
        << ",\"validation_disk_quadrature_points\":"
        << value.validation_disk_quadrature_points
        << ",\"terrain_profile\":\"" << core99::t18::to_string(value.terrain_profile)
        << "\",\"disk_sampling\":\"" << core99::t18::to_string(value.disk_sampling)
        << "\",\"validation_disk_sampling\":\""
        << core99::t18::to_string(value.validation_disk_sampling)
        << "\",\"objective_evaluations\":" << value.objective_evaluations
        << ",\"wind_scenario_layout_evaluations\":"
        << value.wind_scenario_layout_evaluations
        << ",\"wake_pair_checks\":" << value.wake_pair_checks
        << ",\"disk_quadrature_samples\":" << value.disk_quadrature_samples
        << ",\"terrain_precompute_seconds\":" << value.terrain_precompute_seconds
        << ",\"validation_seconds\":" << value.validation_seconds
        << ",\"evaluator_seconds\":" << value.evaluator_seconds
        << ",\"algorithm_seconds\":" << value.algorithm_seconds
        << ",\"end_to_end_seconds\":" << value.end_to_end_seconds
        << ",\"scientific_hash\":\"" << std::hex << value.scientific_hash
        << std::dec << "\",\"validation\":[";
    for (std::size_t index = 0; index < value.validation.size(); ++index) {
        if (index) out << ',';
        const auto& item = value.validation[index];
        out << "{\"role\":\"" << item.role
            << "\",\"wake\":\"" << core99::t18::to_string(item.wake)
            << "\",\"merge\":\"" << core99::t18::to_string(item.merge)
            << "\",\"predicted_velocity_mps\":" << item.predicted_velocity_mps
            << ",\"experimental_velocity_mps\":" << item.experimental_velocity_mps
            << ",\"relative_error_percent\":" << item.relative_error_percent << '}';
    }
    out << "],\"roles\":[";
    for (std::size_t index = 0; index < value.roles.size(); ++index) {
        if (index) out << ',';
        const auto& role = value.roles[index];
        out << "{\"role\":\"" << role.role
            << "\",\"wake\":\"" << core99::t18::to_string(role.wake)
            << "\",\"design_case\":\"" << core99::t18::to_string(role.design_case)
            << "\",\"reference\":" << (role.reference ? "true" : "false")
            << ",\"starting_kernel\":" << role.starting_kernel
            << ",\"kernel_sequence\":[";
        for (std::size_t item = 0; item < role.kernel_sequence.size(); ++item) {
            if (item) out << ',';
            out << role.kernel_sequence[item];
        }
        out << "],\"switch_generations\":[";
        for (std::size_t item = 0; item < role.switch_generations.size(); ++item) {
            if (item) out << ',';
            out << role.switch_generations[item];
        }
        out << "],\"evaluation\":" << evaluation_json(role.evaluation)
            << ",\"layout\":" << layout_json(role.layout) << '}';
    }
    return out.str() + "]}";
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
    if (!stream) throw std::runtime_error("T18 cannot write output");
    stream << payload << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const auto arguments = parse(argc, argv);
        if (arguments.action == "describe") {
            emit(
                "{\"case_id\":\"awec25\",\"turbine_count\":25,"
                "\"validation_role_count\":48,\"optimization_role_count\":6,"
                "\"paper_role_count\":54,\"wind_direction_count\":16,"
                "\"wind_speed_bin_count\":7,\"wake_models\":6,"
                "\"merge_schemes\":4,\"optimized_wake_models\":"
                "[\"frandsen\",\"bp\"],\"design_cases\":"
                "[\"case1_layout\",\"case2_layout_turbine\"]}",
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        const core99::t18::Problem problem;
        if (arguments.action == "validate") {
            emit(
                validation_json(
                    problem.validate_wind_tunnel(
                        arguments.config.disk_sampling,
                        arguments.config.disk_quadrature_points
                    ),
                    arguments.config.disk_sampling
                ),
                arguments.output
            );
            return EXIT_SUCCESS;
        }
        if (arguments.action != "run") {
            throw std::invalid_argument("T18 action describe/validate/run");
        }
        emit(
            result_json(core99::t18::run(problem, arguments.config)),
            arguments.output
        );
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "T18 error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
