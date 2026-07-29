/* BENCHMARK PACKAGE FACT DECLARATION
Paper DOI: REQUIRED_DOI
Public author source: REQUIRED_URL_OR_EXPLICITLY_NOT_FOUND
Known missing fields: REQUIRED_DECISION_SENSITIVITY_PROFILES
Reconstruction action: KEEP EACH SENSITIVITY UNDER A DISTINCT SEMANTIC HASH
Claim boundary: TEMPLATE SENSITIVITY TEST ONLY; NO ROBUSTNESS CLAIM
*/
#include "benchmark_template/registry.hpp"

#include <cassert>

int main() {
    benchmark_template::Registry registry;
    benchmark_template::register_example_package(registry);
    const auto problem = registry.make_problem("template_problem");
    const auto algorithm = registry.make_algorithm("template_algorithm");

    const auto baseline = algorithm->optimize(
        *problem,
        {31, 0x1000ULL, 41, 1}
    );
    const auto repeated = algorithm->optimize(
        *problem,
        {31, 0x1000ULL, 41, 1}
    );
    const auto distinct_semantic = algorithm->optimize(
        *problem,
        {31, 0x2000ULL, 41, 1}
    );

    assert(baseline.scientific_hash == repeated.scientific_hash);
    assert(baseline.best_layout == repeated.best_layout);
    assert(
        baseline.scientific_hash != distinct_semantic.scientific_hash
        || baseline.best_layout != distinct_semantic.best_layout
    );
    return 0;
}
