/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T62 pure-C++ paper-native CLI
Paper title/DOI: Optimization of Wind Turbine Layout Position in a Wind Farm
Using a Newly-Developed Two-Dimensional Wake Model;
10.1016/j.apenergy.2016.04.098
Public source: none located
Missing and reconstruction: author seeds/layouts/operators are absent;
declared completion is documented in include/core99/gao_t62.hpp
Semantic IDs: t62_gao_case_b_grid_jensen_gaussian_v1;
t62_mpga_declared_reconstruction_v1
Controlling contract: shared/contracts/core99_t62_gao_2016.json
Claim boundary: academic declared reproduction, not author-code replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/gao_t62.hpp"

#include <cstdlib>
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
    std::string site_mode = "grid";
    std::string sites;
    std::string output;
    int turbines = 39;
    std::uint64_t seed = 20260731;
    int workers = 20;
    int demes = 10;
    int individuals = 20;
    int stagnation = 500;
    int max_generations = 5000;
    int migration_period = 20;
    double x_d = 5.0;
    double r_d = 0.0;
    double ct = 0.62;
    double i0 = 0.10;
};

Arguments parse(const int argc, char** argv) {
    Arguments args;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() -> std::string {
            if (++index >= argc) {
                throw std::invalid_argument("missing value for " + flag);
            }
            return argv[index];
        };
        if (flag == "--mode") args.mode = value();
        else if (flag == "--site-mode") args.site_mode = value();
        else if (flag == "--sites") args.sites = value();
        else if (flag == "--output") args.output = value();
        else if (flag == "--turbines") args.turbines = std::stoi(value());
        else if (flag == "--seed") args.seed = std::stoull(value());
        else if (flag == "--workers") args.workers = std::stoi(value());
        else if (flag == "--demes") args.demes = std::stoi(value());
        else if (flag == "--individuals") args.individuals = std::stoi(value());
        else if (flag == "--stagnation") args.stagnation = std::stoi(value());
        else if (flag == "--max-generations") {
            args.max_generations = std::stoi(value());
        } else if (flag == "--migration-period") {
            args.migration_period = std::stoi(value());
        } else if (flag == "--x-d") args.x_d = std::stod(value());
        else if (flag == "--r-d") args.r_d = std::stod(value());
        else if (flag == "--ct") args.ct = std::stod(value());
        else if (flag == "--i0") args.i0 = std::stod(value());
        else throw std::invalid_argument("unknown flag: " + flag);
    }
    return args;
}

std::vector<int> parse_sites(const std::string& text) {
    std::vector<int> result;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (!token.empty()) result.push_back(std::stoi(token));
    }
    return result;
}

std::string layout_json(const std::vector<core99::t62::Point>& layout) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index != 0) out << ',';
        out << '[' << layout[index].x_m << ',' << layout[index].y_m << ']';
    }
    out << ']';
    return out.str();
}

std::string evaluation_json(const core99::t62::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"objective\":" << value.objective
        << ",\"average_power_kw\":" << value.average_power_kw
        << ",\"efficiency\":" << value.efficiency
        << ",\"cost\":" << value.cost
        << ",\"constraint_violation\":" << value.constraint_violation << '}';
    return out.str();
}

void emit(const std::string& payload, const std::string& path) {
    if (path.empty()) {
        std::cout << payload;
        return;
    }
    std::ofstream stream(path);
    if (!stream) throw std::runtime_error("cannot open output: " + path);
    stream << payload;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments args = parse(argc, argv);
        std::ostringstream out;
        out << std::setprecision(17);
        if (args.mode == "wake") {
            out << "{\"mode\":\"wake\",\"x_d\":" << args.x_d
                << ",\"r_d\":" << args.r_d << ",\"ct\":" << args.ct
                << ",\"i0\":" << args.i0 << ",\"speed_ratio\":"
                << core99::t62::improved_wake_speed_ratio(
                    args.x_d, args.r_d, args.ct, args.i0
                ) << "}\n";
        } else {
            if (args.site_mode != "grid"
                && args.site_mode != "continuous") {
                throw std::invalid_argument(
                    "--site-mode must be grid or continuous"
                );
            }
            const auto site_mode = args.site_mode == "grid"
                ? core99::t62::SiteMode::paper_grid
                : core99::t62::SiteMode::continuous_sensitivity;
            const core99::t62::Problem problem(args.turbines, site_mode);
            if (args.mode == "evaluate") {
                const auto sites = parse_sites(args.sites);
                if (sites.size() != static_cast<std::size_t>(args.turbines)) {
                    throw std::invalid_argument(
                        "--sites count must equal --turbines"
                    );
                }
                std::vector<core99::t62::Point> layout;
                for (const int site : sites) {
                    if (site < 0 || site >= 100) {
                        throw std::invalid_argument("site must be in [0,99]");
                    }
                    layout.push_back({
                        100.0 + 200.0 * static_cast<double>(site % 10),
                        100.0 + 200.0 * static_cast<double>(site / 10),
                    });
                }
                out << "{\"mode\":\"evaluation\","
                    << "\"problem_semantic_id\":\""
                    << problem.semantic_id() << "\",\"layout\":"
                    << layout_json(layout) << ",\"evaluation\":"
                    << evaluation_json(problem.evaluate_layout(layout))
                    << "}\n";
            } else if (args.mode == "optimize") {
                core99::t62::MpgaConfig config;
                config.demes = args.demes;
                config.individuals_per_deme = args.individuals;
                config.unchanged_generations = args.stagnation;
                config.maximum_generations = args.max_generations;
                config.migration_period = args.migration_period;
                const auto result = core99::t62::run_mpga(
                    problem, args.seed, args.workers, config
                );
                out << "{\"mode\":\"optimization\","
                    << "\"problem_semantic_id\":\""
                    << result.problem_semantic_id << "\","
                    << "\"method_semantic_id\":\""
                    << result.method_semantic_id << "\","
                    << "\"turbines\":" << result.turbine_count
                    << ",\"seed\":" << result.seed
                    << ",\"generations\":" << result.generations
                    << ",\"unchanged_generations\":"
                    << result.unchanged_generations
                    << ",\"physical_fes\":" << result.physical_fes
                    << ",\"requested_workers\":"
                    << result.requested_workers
                    << ",\"observed_workers\":" << result.observed_workers
                    << ",\"evaluator_seconds\":" << result.evaluator_seconds
                    << ",\"algorithm_seconds\":" << result.algorithm_seconds
                    << ",\"end_to_end_seconds\":"
                    << result.end_to_end_seconds
                    << ",\"best_evaluation\":"
                    << evaluation_json(result.best_evaluation)
                    << ",\"best_layout\":" << layout_json(result.best_layout)
                    << ",\"scientific_hash\":\"" << std::hex
                    << result.scientific_hash << std::dec << "\"}\n";
            } else {
                throw std::invalid_argument("unknown T62 mode");
            }
        }
        emit(out.str(), args.output);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "core99_t62_hpc error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
