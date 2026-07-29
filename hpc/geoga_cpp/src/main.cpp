/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: GeoGA Anholt-structured declared pure-CPU CLI
Method semantic ID: geoga_declared_reconstruction_v1
Execution profile ID: geoga_anholt_structured_p3_execution_v1
Problem semantic ID: geoga_anholt_structured_declared_proxy_v1
Evidence tiers: admitted M3 method on P3 declared proxy
Default execution: pure CPU with all hardware threads visible to the job
Claim boundary: emits development receipts only and cannot invoke or modify the historical GGA-asset proxy path
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "geoga/evolution.hpp"
#include "geoga/problem.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::string require_value(int& index, int argc, char** argv) {
    if (index + 1 >= argc) {
        throw std::invalid_argument(
            "missing value for " + std::string(argv[index])
        );
    }
    ++index;
    return argv[index];
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string case_path;
        geoga::EvolutionConfig config;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--case") {
                case_path = require_value(index, argc, argv);
            } else if (argument == "--seed") {
                config.seed = std::stoull(require_value(index, argc, argv));
            } else if (argument == "--physical-fes") {
                config.physical_fes =
                    std::stoull(require_value(index, argc, argv));
            } else if (argument == "--workers") {
                config.workers =
                    std::stoi(require_value(index, argc, argv));
            } else if (argument == "--backend") {
                config.backend = require_value(index, argc, argv);
            } else if (argument == "--help") {
                std::cout
                    << "usage: geoga_anholt_hpc --case FILE [--seed N]"
                    << " [--physical-fes N] [--workers N]"
                    << " [--backend cpu|auto|hybrid|gpu]\n";
                return 0;
            } else {
                throw std::invalid_argument("unknown argument: " + argument);
            }
        }
        if (case_path.empty()) {
            throw std::invalid_argument("--case is required");
        }
        if (
            config.backend != "cpu"
            && config.backend != "auto"
            && config.backend != "hybrid"
            && config.backend != "gpu"
        ) {
            throw std::invalid_argument(
                "--backend must be cpu, auto, hybrid, or gpu"
            );
        }
        if (config.backend != "cpu") {
            throw std::invalid_argument(
                "backend " + config.backend
                + " is recognized but unavailable; no hidden CPU fallback "
                  "was performed"
            );
        }
        const geoga::Problem problem = geoga::load_problem(case_path);
        const geoga::EvolutionResult result = geoga::run(config, problem);
        std::cout << geoga::result_to_json(result) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "geoga_anholt_hpc: " << error.what() << '\n';
        return 1;
    }
}
