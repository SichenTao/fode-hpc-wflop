/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0373 pure-C++ CPU-HPC command line
Paper DOI: 10.1016/j.renene.2021.10.032
Public source: arXiv 2107.11620 source archive.
Cited public dependency: FLORISSE_M, MIT, commit
36cb0a0295d2a1e05640fdbbcb9bb361ac8d592e.
Public-source search, missing information, conflicts, corrections, declared
reconstruction, semantic IDs, backend, contract and claim boundary:
include/core99/chen_l0373.hpp
Claim boundary: flexible academic reconstruction, not author target code,
exact MATLAB/FLORISSE-M trajectory, private arrays or numerical replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/chen_l0373.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string action = "optimize";
    core99::l0373::ProfileId profile =
        core99::l0373::ProfileId::turbines16_directions36;
    core99::l0373::RunConfig config;
    std::filesystem::path output;
};

core99::l0373::ProfileId parse_profile(const std::string& value) {
    for (const auto profile : core99::l0373::paper_profiles()) {
        if (core99::l0373::to_string(profile) == value) return profile;
    }
    throw std::invalid_argument("L0373 unknown profile " + value);
}

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        auto value = [&]() {
            if (++index >= argc) throw std::invalid_argument("L0373 missing " + key);
            return std::string(argv[index]);
        };
        if (key == "--action") result.action = value();
        else if (key == "--profile") result.profile = parse_profile(value());
        else if (key == "--seed") result.config.seed = std::stoull(value());
        else if (key == "--workers") result.config.workers = std::stoi(value());
        else if (key == "--pso-trials") {
            result.config.pso_trials = std::stoi(value());
        } else if (key == "--pso-population") {
            result.config.pso_population = std::stoi(value());
        } else if (key == "--pso-iterations") {
            result.config.pso_iterations = std::stoi(value());
        } else if (key == "--control-passes") {
            result.config.control_passes = std::stoi(value());
        } else if (key == "--dbhm-iterations") {
            result.config.dbhm_iterations = std::stoi(value());
        } else if (key == "--output") result.output = value();
        else throw std::invalid_argument("L0373 unknown option " + key);
    }
    return result;
}

std::string points_json(const std::vector<core99::l0373::Point>& points) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (index) output << ',';
        output << '[' << points[index].x_m << ',' << points[index].y_m << ']';
    }
    return output.str() + ']';
}

std::string controls_json(
    const std::vector<core99::l0373::Controls>& schedule
) {
    std::ostringstream output;
    output << std::setprecision(17) << '[';
    for (std::size_t wind = 0; wind < schedule.size(); ++wind) {
        if (wind) output << ',';
        output << "{\"yaw_degrees\":[";
        for (std::size_t turbine = 0;
             turbine < schedule[wind].yaw_degrees.size(); ++turbine) {
            if (turbine) output << ',';
            output << schedule[wind].yaw_degrees[turbine];
        }
        output << "],\"axial_induction\":[";
        for (std::size_t turbine = 0;
             turbine < schedule[wind].axial_induction.size(); ++turbine) {
            if (turbine) output << ',';
            output << schedule[wind].axial_induction[turbine];
        }
        output << "]}";
    }
    return output.str() + ']';
}

std::string evaluation_json(const core99::l0373::Evaluation& value) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"feasible\":" << (value.feasible ? "true" : "false")
        << ",\"aep_gwh\":" << value.aep_gwh
        << ",\"expected_power_mw\":" << value.expected_power_mw
        << ",\"no_wake_power_mw\":" << value.no_wake_power_mw
        << ",\"efficiency_percent\":" << value.efficiency_percent
        << ",\"minimum_distance_m\":" << value.minimum_distance_m
        << ",\"spacing_violation_squared_m2\":"
        << value.spacing_violation_squared_m2 << '}';
    return output.str();
}

std::string metadata_json(const core99::l0373::Problem& problem) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"profile\":\"" << core99::l0373::to_string(problem.profile())
        << "\",\"profile_id\":\"" << problem.id() << "\""
        << ",\"turbines\":" << problem.turbine_count()
        << ",\"wind_scenarios\":" << problem.winds().size()
        << ",\"width_m\":" << problem.width_m()
        << ",\"height_m\":" << problem.height_m()
        << ",\"minimum_spacing_m\":" << problem.minimum_spacing_m()
        << ",\"paper_aep_anchors_gwh\":";
    switch (problem.profile()) {
        case core99::l0373::ProfileId::illustrative_unrestricted:
            output << "[43.2,51.0,52.1]";
            break;
        case core99::l0373::ProfileId::illustrative_4d:
            output << "[51.85,52.05]";
            break;
        case core99::l0373::ProfileId::turbines16_directions36:
            output << "[366.52,373.96,376.24,386.49,402.96]";
            break;
        case core99::l0373::ProfileId::turbines16_directions360:
            output << "[374.34,385.40,378.34,386.39,391.97]";
            break;
        case core99::l0373::ProfileId::turbines80_directions12:
            output << "[1971.1,1984.3,2041.0,2058.4,2078.0]";
            break;
        case core99::l0373::ProfileId::turbines80_directions180:
            output << "[1981.1,1998.1,1999.9,2015.2,2022.9]";
            break;
    }
    output << ",\"paper_middle_position_anchors_m\":";
    switch (problem.profile()) {
        case core99::l0373::ProfileId::illustrative_unrestricted:
            output << "[800,470]";
            break;
        case core99::l0373::ProfileId::illustrative_4d:
            output << "[596,504]";
            break;
        default:
            output << "null";
            break;
    }
    output << ",\"paper_computation_time_anchors_seconds\":";
    switch (problem.profile()) {
        case core99::l0373::ProfileId::turbines16_directions36:
            output << "{\"isolated_pso\":14443.98,\"joint_scp\":16006.16,"
                "\"joint_pso\":94064.74,\"joint_dbhm\":12969.60,"
                "\"dbhm_iterations\":11}";
            break;
        case core99::l0373::ProfileId::turbines80_directions12:
            output << "{\"isolated_pso\":300629,\"joint_scp\":280220,"
                "\"joint_pso_lower_bound\":864000,\"joint_dbhm\":314188}";
            break;
        default:
            output << "null";
            break;
    }
    return output.str() + '}';
}

std::string result_json(
    const core99::l0373::Problem& problem,
    const core99::l0373::RunResult& result
) {
    std::ostringstream output;
    output << std::setprecision(17)
        << "{\"metadata\":" << metadata_json(problem)
        << ",\"profile_id\":\"" << result.profile_id << "\""
        << ",\"method_semantic_id\":\"" << result.method_semantic_id << "\""
        << ",\"problem_semantic_id\":\"" << result.problem_semantic_id << "\""
        << ",\"protocol_semantic_id\":\"" << result.protocol_semantic_id << "\""
        << ",\"seed\":" << result.seed
        << ",\"requested_workers\":" << result.requested_workers
        << ",\"observed_workers\":" << result.observed_workers
        << ",\"parallel_regions\":" << result.parallel_regions
        << ",\"complete_layout_evaluations\":"
        << result.complete_layout_evaluations
        << ",\"single_wind_state_evaluations\":"
        << result.single_wind_state_evaluations
        << ",\"pso_trials\":" << result.pso_trials
        << ",\"pso_population\":" << result.pso_population
        << ",\"pso_iterations\":" << result.pso_iterations
        << ",\"dbhm_iterations_completed\":"
        << result.dbhm_iterations_completed
        << ",\"final_consensus_violation_m\":"
        << result.final_consensus_violation_m
        << ",\"isolated_layout_stage_seconds\":"
        << result.isolated_layout_stage_seconds
        << ",\"control_stage_seconds\":" << result.control_stage_seconds
        << ",\"dbhm_stage_seconds\":" << result.dbhm_stage_seconds
        << ",\"end_to_end_seconds\":" << result.end_to_end_seconds
        << ",\"scientific_hash\":" << result.scientific_hash
        << ",\"cases\":[";
    for (std::size_t index = 0; index < result.cases.size(); ++index) {
        if (index) output << ',';
        const auto& role = result.cases[index];
        output << "{\"role\":\"" << role.role << "\""
            << ",\"evaluation\":" << evaluation_json(role.evaluation)
            << ",\"layout\":" << points_json(role.layout)
            << ",\"controls_by_wind\":"
            << controls_json(role.controls_by_wind) << '}';
    }
    return output.str() + "]}";
}

void emit(const std::string& text, const std::filesystem::path& output) {
    if (output.empty()) {
        std::cout << text << '\n';
        return;
    }
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    const std::filesystem::path temporary = output.string() + ".tmp";
    std::ofstream stream(temporary);
    if (!stream) throw std::runtime_error("cannot open L0373 output");
    stream << text << '\n';
    stream.close();
    std::filesystem::rename(temporary, output);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        if (arguments.action == "list-profiles") {
            std::ostringstream output;
            output << '[';
            const auto profiles = core99::l0373::paper_profiles();
            for (std::size_t index = 0; index < profiles.size(); ++index) {
                if (index) output << ',';
                const core99::l0373::Problem problem(profiles[index]);
                output << metadata_json(problem);
            }
            emit(output.str() + ']', arguments.output);
            return 0;
        }
        const core99::l0373::Problem problem(arguments.profile);
        if (arguments.action == "metadata") {
            emit(metadata_json(problem), arguments.output);
        } else if (arguments.action == "optimize") {
            emit(
                result_json(problem, core99::l0373::run(problem, arguments.config)),
                arguments.output
            );
        } else {
            throw std::invalid_argument("L0373 unknown action " + arguments.action);
        }
    } catch (const std::exception& error) {
        std::cerr << "L0373 error: " << error.what() << '\n';
        return 2;
    }
    return 0;
}
