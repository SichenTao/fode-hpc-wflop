/* BENCHMARK PACKAGE FACT DECLARATION
Paper DOI: REQUIRED_DOI
Public author source: REQUIRED_URL_OR_EXPLICITLY_NOT_FOUND
Known missing fields: REQUIRED_FINAL_CLI_AND_FORMAL_RECEIPT_FIELDS
Reconstruction action: RUN ONLY REGISTERED SEMANTICS AND FAIL CLOSED ON BACKENDS
Claim boundary: TEMPLATE RUNNER ONLY; NO FORMAL CAMPAIGN OR PERFORMANCE CLAIM
*/
#include "benchmark_template/backend.hpp"
#include "benchmark_template/registry.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    try {
        const std::string backend_name = argc > 1 ? argv[1] : "cpu";
        const auto backend = benchmark_template::resolve_backend_or_throw(
            benchmark_template::parse_backend(backend_name)
        );
        if (backend != benchmark_template::Backend::cpu) {
            return EXIT_FAILURE;
        }
        benchmark_template::Registry registry;
        benchmark_template::register_example_package(registry);
        const auto problem = registry.make_problem("template_problem");
        const auto algorithm = registry.make_algorithm("template_algorithm");
        const auto result = algorithm->optimize(
            *problem,
            {2026073101ULL, 0x9abcULL, 31, 1}
        );
        std::cout << "physical_fes=" << result.physical_fes
                  << " scientific_hash=" << result.scientific_hash << '\n';
        return result.physical_fes == 31 ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
