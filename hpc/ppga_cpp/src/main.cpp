/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: PPGA Nantong-structured declared reconstruction CLI
Paper title: Advanced 3D Wind Farm Layout Optimization Framework via Power-Law Perturbation-Based Genetic Algorithm
DOI: 10.1109/JAS.2025.125351
Public author method source and Nantong problem assets: unavailable as recorded in docs/source-dossiers/T43.json
Available information: paper equations, pseudocode, population 30, threshold 0, crossover 0.8, mutation 0.1, power-law exponent 2.5, and 16 by 27 case structure
Missing information: original elevation and wind arrays, turbine curves, complete wake inputs, author-exact fitness normalization, stagnation history, power-law normalization, repair, elite count, survivor order, random seeds, and reference results
Reconstruction decision: corrected v2 uses the frozen P3 problem, strict Eq.18 gate, independent finite-support per-dimension perturbation, parent-index stagnation before selection, counter-keyed RNG, exact complete-layout FES, and pure CPU execution
Method semantic ID: ppga_nantong_structured_3d_declared_reconstruction_v2
Problem semantic ID: ppga_nantong_structured_3d_declared_proxy_v1
Evidence tiers: M3_DECLARED_COMPLETION on P3_DECLARED_PROXY
Default execution: pure CPU with hardware_concurrency workers
Controlling contract: shared/contracts/ppga_nantong_structured_3d_declared_reconstruction_contract.json
Claim boundary: bounded development reconstruction only; original Nantong identity, paper results, formal results, hybrid, and GPU execution remain blocked
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
                    << " [--backend cpu|auto|hybrid|gpu]\n";
                return 0;
            } else {
                throw std::invalid_argument("unknown argument: " + argument);
            }
        }
        if (cases_path.empty()) {
            throw std::invalid_argument("--cases is required");
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
