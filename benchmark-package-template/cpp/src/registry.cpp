/* BENCHMARK PACKAGE FACT DECLARATION
Paper DOI: REQUIRED_DOI
Public author source: REQUIRED_URL_OR_EXPLICITLY_NOT_FOUND
Known missing fields: REQUIRED_FINAL_METHOD_AND_PROBLEM_REGISTRATION_IDS
Reconstruction action: REJECT DUPLICATE OR UNKNOWN REGISTRY IDENTITIES
Claim boundary: TEMPLATE REGISTRY ONLY; NO ORIGINAL REPRODUCTION CLAIM
*/
#include "benchmark_template/registry.hpp"

#include <stdexcept>
#include <utility>

namespace benchmark_template {

void Registry::register_algorithm(
    std::string id,
    AlgorithmFactory factory
) {
    if (id.empty() || !factory || algorithms_.contains(id)) {
        throw std::invalid_argument("invalid or duplicate algorithm ID");
    }
    algorithms_.emplace(std::move(id), std::move(factory));
}

void Registry::register_problem(std::string id, ProblemFactory factory) {
    if (id.empty() || !factory || problems_.contains(id)) {
        throw std::invalid_argument("invalid or duplicate problem ID");
    }
    problems_.emplace(std::move(id), std::move(factory));
}

std::unique_ptr<Algorithm> Registry::make_algorithm(
    const std::string& id
) const {
    const auto found = algorithms_.find(id);
    if (found == algorithms_.end()) {
        throw std::out_of_range("unregistered algorithm ID: " + id);
    }
    return found->second();
}

std::unique_ptr<Problem> Registry::make_problem(
    const std::string& id
) const {
    const auto found = problems_.find(id);
    if (found == problems_.end()) {
        throw std::out_of_range("unregistered problem ID: " + id);
    }
    return found->second();
}

void register_example_package(Registry& registry) {
    registry.register_algorithm(
        "template_algorithm",
        [] { return make_example_algorithm(); }
    );
    registry.register_problem(
        "template_problem",
        [] { return make_example_problem(); }
    );
}

}  // namespace benchmark_template
