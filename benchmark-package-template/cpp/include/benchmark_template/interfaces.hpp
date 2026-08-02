/* BENCHMARK PACKAGE FACT DECLARATION
Paper DOI: REQUIRED_DOI
Public author source: REQUIRED_URL_OR_EXPLICITLY_NOT_FOUND
Known missing fields: REQUIRED_LIST_AFTER_COMPLETE_AUTHORITY_SEARCH
Reconstruction action: REQUIRED_MISSING_FIELDS_ONLY_COMPLETION
Claim boundary: TEMPLATE SKELETON ONLY; NO ORIGINAL-METHOD OR RESULT CLAIM
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace benchmark_template {

using Layout = std::vector<std::uint32_t>;

struct Evaluation {
    double objective{};
    bool feasible{};
};

class Problem {
public:
    virtual ~Problem() = default;
    [[nodiscard]] virtual std::string id() const = 0;
    [[nodiscard]] virtual std::string semantic_id() const = 0;
    [[nodiscard]] virtual Evaluation evaluate(const Layout& layout) const = 0;
};

struct RunRequest {
    std::uint64_t seed{};
    std::uint64_t semantic_profile_hash{};
    std::size_t physical_fes{};
    std::size_t workers{1};
};

struct RunResult {
    double best_objective{};
    Layout best_layout;
    std::size_t physical_fes{};
    std::uint64_t scientific_hash{};
};

class Algorithm {
public:
    virtual ~Algorithm() = default;
    [[nodiscard]] virtual std::string id() const = 0;
    [[nodiscard]] virtual std::string semantic_id() const = 0;
    [[nodiscard]] virtual RunResult optimize(
        const Problem& problem,
        const RunRequest& request
    ) const = 0;
};

[[nodiscard]] std::unique_ptr<Algorithm> make_example_algorithm();
[[nodiscard]] std::unique_ptr<Problem> make_example_problem();

}  // namespace benchmark_template
