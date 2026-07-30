/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T14 deterministic decoder/problem unit tests
Paper DOI: 10.5194/wes-4-663-2019
Public source and reconstruction boundary: include/core99/stanley_t14.hpp
Method/problem semantic IDs: t14_boundary_grid_parameterization_v1;
t14_stanley_2019_seven_unique_cases_v1
Controlling contract: shared/contracts/core99_t14_stanley_2019.json
Claim boundary: structural and deterministic test, not author SNOPT replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/stanley_t14.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

int main() {
    const auto cases = core99::t14::paper_cases();
    if (cases.size() != 7 || core99::t14::algorithm_ids().size() != 3) {
        std::cerr << "T14 paper matrix mismatch\n";
        return EXIT_FAILURE;
    }
    for (const auto& paper_case : cases) {
        const core99::t14::Problem problem(paper_case);
        for (const auto& algorithm : core99::t14::algorithm_ids()) {
            const auto representation = core99::t14::representation_from_id(
                algorithm
            );
            const auto first = core99::t14::decode_reference_layout(
                problem,
                representation,
                20260731
            );
            const auto second = core99::t14::decode_reference_layout(
                problem,
                representation,
                20260731
            );
            if (first.size() != 100 || second.size() != first.size()) {
                std::cerr << "T14 layout size mismatch\n";
                return EXIT_FAILURE;
            }
            for (std::size_t index = 0; index < first.size(); ++index) {
                if (
                    first[index].x != second[index].x
                    || first[index].y != second[index].y
                ) {
                    std::cerr << "T14 decoder is not deterministic\n";
                    return EXIT_FAILURE;
                }
            }
            const double aep = problem.evaluate_optimization(first);
            if (!std::isfinite(aep) || aep <= 0.0) {
                std::cerr << "T14 AEP is invalid\n";
                return EXIT_FAILURE;
            }
            if (!std::isfinite(problem.constraint_violation(first))) {
                std::cerr << "T14 violation is invalid\n";
                return EXIT_FAILURE;
            }
        }
    }
    return EXIT_SUCCESS;
}
