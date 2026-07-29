/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: PPGA Nantong-structured declared reconstruction CLI
Method semantic ID: ppga_nantong_structured_3d_declared_reconstruction_v1
Problem semantic ID: ppga_nantong_structured_3d_declared_proxy_v1
Evidence tiers: M3_DECLARED_COMPLETION on P3_DECLARED_PROXY
Default execution: pure CPU with hardware_concurrency workers
Claim boundary: development reconstruction receipt, not original Nantong reproduction
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "ppga/evolution.hpp"
#include "ppga/problem.hpp"

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
        std::string cases_path;
        std::string case_id = "PPGA_NantongStructured_WS1_tn20";
        ppga::EvolutionConfig config;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--cases") {
                cases_path = require_value(index, argc, argv);
            } else if (argument == "--case") {
                case_id = require_value(index, argc, argv);
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
                    << "usage: ppga_nantong_hpc --cases FILE [--case ID]"
                    << " [--seed N] [--physical-fes N] [--workers N]"
                    << " [--backend cpu]\n";
                return 0;
            } else {
                throw std::invalid_argument("unknown argument: " + argument);
            }
        }
        if (cases_path.empty()) {
            throw std::invalid_argument("--cases is required");
        }
        const ppga::Problem problem =
            ppga::load_problem(cases_path, case_id);
        const ppga::EvolutionResult result = ppga::run(config, problem);
        std::cout << ppga::result_to_json(result) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ppga_nantong_hpc: " << error.what() << '\n';
        return 1;
    }
}
