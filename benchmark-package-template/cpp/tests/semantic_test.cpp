/* BENCHMARK PACKAGE FACT DECLARATION
Paper DOI: REQUIRED_DOI
Public author source: REQUIRED_URL_OR_EXPLICITLY_NOT_FOUND
Known missing fields: REQUIRED_SEMANTIC_FIXTURE_VALUES
Reconstruction action: TEST DISTINCT REGISTRY IDS, SCALAR ORACLE, AND EXACT FES
Claim boundary: TEMPLATE SEMANTIC TEST ONLY; NO PAPER-RESULT CLAIM
*/
#include "benchmark_template/backend.hpp"
#include "benchmark_template/registry.hpp"

#include <cassert>
#include <cmath>

int main() {
    benchmark_template::Registry registry;
    benchmark_template::register_example_package(registry);
    const auto problem = registry.make_problem("template_problem");
    const auto algorithm = registry.make_algorithm("template_algorithm");

    assert(problem->semantic_id() == "template_scalar_problem_v1");
    assert(algorithm->semantic_id() == "template_deterministic_algorithm_v1");

    const auto scalar = problem->evaluate({0, 1, 2, 3});
    assert(scalar.feasible);
    assert(std::abs(scalar.objective - 14.0) < 1.0e-12);

    const auto result = algorithm->optimize(
        *problem,
        {17, 0x1234ULL, 37, 1}
    );
    assert(result.physical_fes == 37);
    assert(!result.best_layout.empty());
    assert(
        benchmark_template::resolve_backend_or_throw(
            benchmark_template::Backend::automatic
        )
        == benchmark_template::Backend::cpu
    );
    return 0;
}
