/* BENCHMARK PACKAGE FACT DECLARATION
Paper DOI: REQUIRED_DOI
Public author source: REQUIRED_URL_OR_EXPLICITLY_NOT_FOUND
Known missing fields: REQUIRED_REGISTRATION_IDENTITY_FIELDS
Reconstruction action: REGISTER DISTINCT METHOD AND PROBLEM SEMANTIC IDENTITIES
Claim boundary: REGISTRATION SKELETON ONLY; DISTINCT SEMANTICS NEVER POOL
*/
#pragma once

#include "benchmark_template/interfaces.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace benchmark_template {

class Registry {
public:
    using AlgorithmFactory = std::function<std::unique_ptr<Algorithm>()>;
    using ProblemFactory = std::function<std::unique_ptr<Problem>()>;

    void register_algorithm(std::string id, AlgorithmFactory factory);
    void register_problem(std::string id, ProblemFactory factory);

    [[nodiscard]] std::unique_ptr<Algorithm> make_algorithm(
        const std::string& id
    ) const;
    [[nodiscard]] std::unique_ptr<Problem> make_problem(
        const std::string& id
    ) const;

private:
    std::unordered_map<std::string, AlgorithmFactory> algorithms_;
    std::unordered_map<std::string, ProblemFactory> problems_;
};

void register_example_package(Registry& registry);

}  // namespace benchmark_template
