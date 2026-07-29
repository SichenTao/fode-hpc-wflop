/* BENCHMARK PACKAGE FACT DECLARATION
Paper DOI: REQUIRED_DOI
Public author source: REQUIRED_URL_OR_EXPLICITLY_NOT_FOUND
Known missing fields: REQUIRED_HOST_AND_PARALLEL_STAGE_RECEIPT
Reconstruction action: REQUIRE WORKER-INVARIANT SCIENTIFIC OUTPUTS BEFORE TIMING
Claim boundary: TEMPLATE EQUIVALENCE TEST ONLY; NO SPEEDUP CLAIM
*/
#include "benchmark_template/registry.hpp"

#include <cassert>

int main() {
    benchmark_template::Registry registry;
    benchmark_template::register_example_package(registry);
    const auto problem = registry.make_problem("template_problem");
    const auto algorithm = registry.make_algorithm("template_algorithm");

    const auto one_worker = algorithm->optimize(
        *problem,
        {47, 0x5678ULL, 53, 1}
    );
    const auto all_workers = algorithm->optimize(
        *problem,
        {47, 0x5678ULL, 53, 20}
    );

    assert(one_worker.physical_fes == all_workers.physical_fes);
    assert(one_worker.best_objective == all_workers.best_objective);
    assert(one_worker.best_layout == all_workers.best_layout);
    assert(one_worker.scientific_hash == all_workers.scientific_hash);
    return 0;
}
