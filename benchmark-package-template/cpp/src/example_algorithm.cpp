/* BENCHMARK PACKAGE FACT DECLARATION
Paper DOI: REQUIRED_DOI
Public author source: REQUIRED_URL_OR_EXPLICITLY_NOT_FOUND
Known missing fields: REQUIRED_METHOD_TRANSITIONS_PARAMETERS_AND_TIE_BREAKS
Reconstruction action: REPLACE THIS DETERMINISTIC FIXTURE WITH THE R2-ADMITTED METHOD
Claim boundary: M4-LIKE ALGORITHM SKELETON ONLY; NO OPTIMIZATION-QUALITY CLAIM
*/
#include "benchmark_template/interfaces.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace benchmark_template {
namespace {

std::uint64_t splitmix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::uint64_t random_event(
    const RunRequest& request,
    const std::size_t evaluation_index,
    const std::size_t gene_index
) {
    auto key = request.seed ^ request.semantic_profile_hash;
    key ^= splitmix64(static_cast<std::uint64_t>(evaluation_index));
    key ^= splitmix64(static_cast<std::uint64_t>(gene_index) << 32U);
    return splitmix64(key);
}

std::uint64_t hash_layout(const Layout& layout) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto value : layout) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return hash;
}

class ExampleAlgorithm final : public Algorithm {
public:
    std::string id() const override {
        return "template_algorithm";
    }

    std::string semantic_id() const override {
        return "template_deterministic_algorithm_v1";
    }

    RunResult optimize(
        const Problem& problem,
        const RunRequest& request
    ) const override {
        if (request.physical_fes == 0 || request.workers == 0) {
            throw std::invalid_argument("positive FES and worker count required");
        }

        RunResult result{
            std::numeric_limits<double>::infinity(),
            {},
            0,
            0,
        };
        for (std::size_t evaluation = 0;
             evaluation < request.physical_fes;
             ++evaluation) {
            Layout layout(4);
            for (std::size_t gene = 0; gene < layout.size(); ++gene) {
                layout[gene] = static_cast<std::uint32_t>(
                    (random_event(request, evaluation, gene) % 64U)
                    + static_cast<std::uint64_t>(gene) * 64U
                );
            }
            const auto value = problem.evaluate(layout);
            ++result.physical_fes;
            if (
                value.feasible
                && (
                    value.objective < result.best_objective
                    || (
                        value.objective == result.best_objective
                        && layout < result.best_layout
                    )
                )
            ) {
                result.best_objective = value.objective;
                result.best_layout = std::move(layout);
            }
        }
        result.scientific_hash = hash_layout(result.best_layout)
            ^ std::bit_cast<std::uint64_t>(result.best_objective)
            ^ static_cast<std::uint64_t>(result.physical_fes);
        return result;
    }
};

}  // namespace

std::unique_ptr<Algorithm> make_example_algorithm() {
    return std::make_unique<ExampleAlgorithm>();
}

}  // namespace benchmark_template
