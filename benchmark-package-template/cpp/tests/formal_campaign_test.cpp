/* BENCHMARK PACKAGE FACT DECLARATION
Paper DOI: REQUIRED_DOI
Public author source: REQUIRED_URL_OR_EXPLICITLY_NOT_FOUND
Known missing fields: REQUIRED_FORMAL_CASE_SEEDS_BUDGET_AND_HOST
Reconstruction action: TEST EXACT WORK AND FAIL-CLOSED BACKENDS BEFORE CAMPAIGN
Claim boundary: TEMPLATE ADMISSION TEST ONLY; NO CAMPAIGN HAS BEEN RUN
*/
#include "benchmark_template/backend.hpp"
#include "benchmark_template/registry.hpp"

#include <cassert>
#include <cstdint>
#include <stdexcept>

int main() {
    for (const auto backend : {
             benchmark_template::Backend::hybrid,
             benchmark_template::Backend::gpu,
         }) {
        bool failed_closed = false;
        try {
            static_cast<void>(
                benchmark_template::resolve_backend_or_throw(backend)
            );
        } catch (const std::runtime_error&) {
            failed_closed = true;
        }
        assert(failed_closed);
    }

    benchmark_template::Registry registry;
    benchmark_template::register_example_package(registry);
    const auto problem = registry.make_problem("template_problem");
    const auto algorithm = registry.make_algorithm("template_algorithm");

    for (std::uint64_t repeat = 0; repeat < 25; ++repeat) {
        const auto result = algorithm->optimize(
            *problem,
            {2026073101ULL + repeat, 0x9abcULL, 31, 20}
        );
        assert(result.physical_fes == 31);
    }
    return 0;
}
