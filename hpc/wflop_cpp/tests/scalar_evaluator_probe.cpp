/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: scalar/discrete evaluator oracle probe
Paper title and DOI: thirteen scalar WFLOP packages; see
docs/scalar_problem_package_registry.tsv
Paper/source basis: independent fixed-layout validation interface
Public asset: project-authored test fixture
Missing/conflicts: none; the probe adds no scientific equations
Reconstruction: evaluates one explicit layout through the shared C++ kernel
Method/problem semantic IDs: not_applicable_test_probe; registry_defined
Controlling contract and claim boundary:
docs/scalar_problem_package_registry.tsv; test evidence only
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "fode/case.hpp"
#include "fode/evaluator.hpp"
#include "fode/executor.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc != 5) {
            throw std::runtime_error(
                "usage: scalar_evaluator_probe CASES CASE LAYOUT WORKERS"
            );
        }
        const fode::CaseData data = fode::load_case(argv[1], argv[2]);
        std::vector<double> layout;
        std::istringstream stream(argv[3]);
        std::string token;
        while (std::getline(stream, token, ',')) {
            layout.push_back(static_cast<double>(std::stoi(token)));
        }
        if (layout.size() != static_cast<std::size_t>(data.turbine_count)) {
            throw std::runtime_error("layout has the wrong turbine count");
        }
        const int workers = std::stoi(argv[4]);
        fode::PersistentExecutor executor(workers);
        const fode::Evaluation evaluated = fode::evaluate_population_hpc(
            layout,
            1,
            data,
            executor,
            fode::EvaluationDetail::TotalOnly
        );
        std::cout << std::setprecision(17) << evaluated.fitness[0] << "\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
