/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T72 pure-C++ CPU-HPC command-line and campaign driver
Paper/DOI: Constrained Multi-Objective Wind Farm Layout Optimization:
Novel Constraint Handling Approach Based on Constraint Programming;
10.1016/j.renene.2018.03.053
Public source: no author code or native maps were located; related MIT PyWake
ISO-noise source is recorded in hpc/core99_cpp/include/core99/sorkhabi_t72.hpp
Missing/conflicts/completion:
hpc/core99_cpp/include/core99/sorkhabi_t72.hpp
Reconstruction: one command exposes each native problem, the 20-seed by
two-penalty paper repeat matrix, fixed-layout equation probes, and CP-repair
probes without Python in the production path
Method/problem semantic IDs: t72_chcp_nsga2_declared_reconstruction_v1;
t72_energy_noise_voronoi9_declared_reconstruction_v1
Controlling contract: shared/contracts/core99_t72_sorkhabi_2018.json
HPC design: one run uses every requested worker for repair/evaluation and
dominance work; multi-run campaigns partition the same allocation across
independent runs without nested oversubscription
Claim boundary: academic declared flexible reproduction, not author code,
IBM CP state, native maps, or exact numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/sorkhabi_t72.hpp"

#include "fode/executor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Arguments {
    int land_availability_percent = 80;
    int turbine_count = 10;
    int workers = 20;
    int runs = 1;
    std::uint64_t physical_fes = 80000;
    std::uint64_t seed = 20260731;
    double maximum_repair_distance_bin2 = 1000.0;
    double penalty_coefficient = 10000.0;
    bool paper_run_matrix = false;
    bool repair_probe = false;
    std::string layout_csv;
    std::string output;
};

Arguments parse(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() -> std::string {
            if (++index >= argc) {
                throw std::invalid_argument("missing value for " + flag);
            }
            return argv[index];
        };
        if (flag == "--land-availability-percent") {
            result.land_availability_percent = std::stoi(value());
        } else if (flag == "--turbines") {
            result.turbine_count = std::stoi(value());
        } else if (flag == "--workers") {
            result.workers = std::stoi(value());
        } else if (flag == "--runs") {
            result.runs = std::stoi(value());
        } else if (flag == "--physical-fes") {
            result.physical_fes = std::stoull(value());
        } else if (flag == "--seed") {
            result.seed = std::stoull(value());
        } else if (flag == "--maximum-repair-distance-bin2") {
            result.maximum_repair_distance_bin2 = std::stod(value());
        } else if (flag == "--penalty-coefficient") {
            result.penalty_coefficient = std::stod(value());
        } else if (flag == "--layout-csv") {
            result.layout_csv = value();
        } else if (flag == "--output") {
            result.output = value();
        } else if (flag == "--paper-run-matrix") {
            result.paper_run_matrix = true;
        } else if (flag == "--repair-probe") {
            result.repair_probe = true;
        } else {
            throw std::invalid_argument("unknown T72 flag: " + flag);
        }
    }
    if (
        result.workers <= 0
        || result.runs <= 0
        || result.physical_fes == 0
        || result.maximum_repair_distance_bin2 < 0.0
        || result.penalty_coefficient <= 0.0
        || (result.paper_run_matrix && result.runs != 40)
    ) {
        throw std::invalid_argument("invalid T72 command-line configuration");
    }
    return result;
}

std::vector<core99::t72::Point> parse_layout(
    const std::string& csv,
    int turbines
) {
    std::vector<double> values;
    std::istringstream input(csv);
    std::string token;
    while (std::getline(input, token, ',')) {
        values.push_back(std::stod(token));
    }
    if (values.size() != static_cast<std::size_t>(2 * turbines)) {
        throw std::invalid_argument("T72 layout CSV cardinality mismatch");
    }
    std::vector<core99::t72::Point> result(
        static_cast<std::size_t>(turbines)
    );
    for (int index = 0; index < turbines; ++index) {
        result[static_cast<std::size_t>(index)] = {
            values[static_cast<std::size_t>(2 * index)],
            values[static_cast<std::size_t>(2 * index + 1)],
        };
    }
    return result;
}

std::string point_json(const core99::t72::Point& point) {
    std::ostringstream out;
    out << std::setprecision(17)
        << '[' << point.x_m << ',' << point.y_m << ']';
    return out.str();
}

std::string layout_json(
    const std::vector<core99::t72::Point>& layout
) {
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        out << point_json(layout[index]);
    }
    out << ']';
    return out.str();
}

std::string evaluation_json(const core99::t72::Evaluation& evaluation) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{"
        << "\"aep_gwh\":" << evaluation.aep_gwh << ','
        << "\"maximum_spl_dba\":"
        << evaluation.maximum_spl_dba << ','
        << "\"proximity_violation_m\":"
        << evaluation.proximity_violation_m << ','
        << "\"regulatory_violation_m\":"
        << evaluation.regulatory_violation_m << ','
        << "\"feasible\":"
        << (evaluation.feasible ? "true" : "false")
        << '}';
    return out.str();
}

std::string front_json(
    const std::vector<core99::t72::FrontPoint>& front
) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < front.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        out << "{"
            << "\"aep_gwh\":" << front[index].aep_gwh << ','
            << "\"maximum_spl_dba\":"
            << front[index].maximum_spl_dba << ','
            << "\"layout\":" << layout_json(front[index].layout)
            << '}';
    }
    out << ']';
    return out.str();
}

std::string run_json(const core99::t72::RunResult& run) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{"
        << "\"problem_id\":\"" << run.problem_id << "\","
        << "\"problem_semantic_id\":\""
        << run.problem_semantic_id << "\","
        << "\"method_semantic_id\":\""
        << run.method_semantic_id << "\","
        << "\"seed\":" << run.seed << ','
        << "\"requested_workers\":" << run.requested_workers << ','
        << "\"observed_workers\":" << run.observed_workers << ','
        << "\"physical_fes\":" << run.physical_fes << ','
        << "\"generations\":" << run.generations << ','
        << "\"population_size\":" << run.population_size << ','
        << "\"maximum_repair_distance_bin2\":"
        << run.maximum_repair_distance_bin2 << ','
        << "\"penalty_coefficient\":"
        << run.penalty_coefficient << ','
        << "\"repair_attempts\":" << run.repair_attempts << ','
        << "\"repair_successes\":" << run.repair_successes << ','
        << "\"repair_timeouts\":" << run.repair_timeouts << ','
        << "\"repair_node_limit_hits\":"
        << run.repair_node_limit_hits << ','
        << "\"repair_search_nodes\":"
        << run.repair_search_nodes << ','
        << "\"repair_seconds\":" << run.repair_seconds << ','
        << "\"evaluator_seconds\":" << run.evaluator_seconds << ','
        << "\"algorithm_seconds\":" << run.algorithm_seconds << ','
        << "\"end_to_end_seconds\":" << run.end_to_end_seconds << ','
        << "\"converged\":"
        << (run.converged ? "true" : "false") << ','
        << "\"measured_land_availability\":"
        << run.measured_land_availability << ','
        << "\"scientific_hash\":\""
        << std::hex << run.scientific_hash << std::dec << "\","
        << "\"front\":" << front_json(run.front)
        << '}';
    return out.str();
}

std::string probe(
    const core99::t72::Problem& problem,
    const Arguments& arguments
) {
    std::vector<core99::t72::Point> layout = parse_layout(
        arguments.layout_csv,
        arguments.turbine_count
    );
    const auto before = problem.evaluate(layout);
    core99::t72::RepairReceipt repair;
    if (arguments.repair_probe) {
        repair = problem.repair(
            layout,
            arguments.maximum_repair_distance_bin2
        );
    }
    const auto after = problem.evaluate(layout);
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\n"
        << "  \"mode\":\""
        << (arguments.repair_probe ? "repair_probe" : "equation_probe")
        << "\",\n"
        << "  \"problem_id\":\"" << problem.id() << "\",\n"
        << "  \"problem_semantic_id\":"
           "\"t72_energy_noise_voronoi9_declared_reconstruction_v1\",\n"
        << "  \"land_availability_percent\":"
        << problem.land_availability_percent() << ",\n"
        << "  \"measured_land_availability\":"
        << problem.measured_land_availability() << ",\n"
        << "  \"receptors\":" << problem.receptors().size() << ",\n"
        << "  \"before\":" << evaluation_json(before) << ",\n"
        << "  \"repair\":{"
        << "\"attempted\":" << (repair.attempted ? "true" : "false")
        << ",\"repaired\":" << (repair.repaired ? "true" : "false")
        << ",\"timed_out\":" << (repair.timed_out ? "true" : "false")
        << ",\"node_limit_hit\":"
        << (repair.node_limit_hit ? "true" : "false")
        << ",\"search_nodes\":" << repair.search_nodes
        << ",\"infeasible_turbines\":" << repair.infeasible_turbines
        << ",\"squared_displacement_bin2\":"
        << repair.squared_displacement_bin2
        << "},\n"
        << "  \"after\":" << evaluation_json(after) << ",\n"
        << "  \"layout\":" << layout_json(layout) << "\n"
        << "}\n";
    return out.str();
}

std::string optimize(
    const core99::t72::Problem& problem,
    const Arguments& arguments,
    double preprocessing_seconds
) {
    std::vector<core99::t72::RunResult> results(
        static_cast<std::size_t>(arguments.runs)
    );
    const int run_parallelism =
        std::min(arguments.workers, arguments.runs);
    const int workers_per_run = std::max(
        1,
        arguments.workers / run_parallelism
    );
    auto execute = [&](int index) {
        core99::t72::RunConfig config;
        config.workers = workers_per_run;
        config.physical_fes = arguments.physical_fes;
        config.maximum_repair_distance_bin2 =
            arguments.maximum_repair_distance_bin2;
        if (arguments.paper_run_matrix) {
            config.seed =
                arguments.seed + static_cast<std::uint64_t>(index % 20);
            config.penalty_coefficient =
                index < 20 ? 10000.0 : 40000.0;
        } else {
            config.seed =
                arguments.seed + static_cast<std::uint64_t>(index);
            config.penalty_coefficient =
                arguments.penalty_coefficient;
        }
        results[static_cast<std::size_t>(index)] =
            core99::t72::run(problem, config);
    };
    const auto started = Clock::now();
    int campaign_observed_workers = 1;
    if (run_parallelism == 1) {
        for (int index = 0; index < arguments.runs; ++index) {
            execute(index);
        }
        campaign_observed_workers = results.front().observed_workers;
    } else {
        fode::PersistentExecutor executor(run_parallelism);
        executor.reset_work_receipt();
        executor.parallel_for(0, arguments.runs, execute);
        campaign_observed_workers =
            executor.work_receipt().distinct_participants;
    }
    const double campaign_seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\n"
        << "  \"mode\":\"paper_target_campaign\",\n"
        << "  \"corpus_id\":\"T72\",\n"
        << "  \"problem_id\":\"" << problem.id() << "\",\n"
        << "  \"problem_semantic_id\":"
           "\"t72_energy_noise_voronoi9_declared_reconstruction_v1\",\n"
        << "  \"method_semantic_id\":"
           "\"t72_chcp_nsga2_declared_reconstruction_v1\",\n"
        << "  \"runs\":" << arguments.runs << ",\n"
        << "  \"paper_run_matrix\":"
        << (arguments.paper_run_matrix ? "true" : "false") << ",\n"
        << "  \"requested_workers\":" << arguments.workers << ",\n"
        << "  \"run_parallelism\":" << run_parallelism << ",\n"
        << "  \"workers_per_run\":" << workers_per_run << ",\n"
        << "  \"campaign_observed_workers\":"
        << campaign_observed_workers << ",\n"
        << "  \"preprocessing_seconds\":"
        << preprocessing_seconds << ",\n"
        << "  \"campaign_seconds\":" << campaign_seconds << ",\n"
        << "  \"run_receipts\":[\n";
    for (std::size_t index = 0; index < results.size(); ++index) {
        if (index != 0) {
            out << ",\n";
        }
        out << "    " << run_json(results[index]);
    }
    out << "\n  ]\n}\n";
    return out.str();
}

void emit(const std::string& payload, const std::string& output) {
    if (output.empty()) {
        std::cout << payload;
        return;
    }
    std::ofstream stream(output);
    if (!stream) {
        throw std::runtime_error("cannot open T72 output: " + output);
    }
    stream << payload;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse(argc, argv);
        const auto preprocessing_started = Clock::now();
        const core99::t72::Problem problem(
            arguments.land_availability_percent,
            arguments.turbine_count
        );
        const double preprocessing_seconds =
            std::chrono::duration<double>(
                Clock::now() - preprocessing_started
            ).count();
        const std::string payload = arguments.layout_csv.empty()
            ? optimize(problem, arguments, preprocessing_seconds)
            : probe(problem, arguments);
        emit(payload, arguments.output);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "T72 failure: " << error.what() << '\n';
        return 2;
    }
}
