/* BENCHMARK PACKAGE FACT DECLARATION
Paper DOI: REQUIRED_DOI
Public author source: REQUIRED_URL_OR_EXPLICITLY_NOT_FOUND
Known missing fields: REQUIRED_PROBLEM_ASSETS_AND_MODEL_PARAMETERS
Reconstruction action: REPLACE THIS SCALAR FIXTURE WITH THE R1-ADMITTED PROBLEM
Claim boundary: P4-LIKE SCALAR SKELETON ONLY; NO WINDFARM QUALITY CLAIM
*/
#include "benchmark_template/interfaces.hpp"

#include <unordered_set>

namespace benchmark_template {
namespace {

class ExampleProblem final : public Problem {
public:
    std::string id() const override {
        return "template_problem";
    }

    std::string semantic_id() const override {
        return "template_scalar_problem_v1";
    }

    Evaluation evaluate(const Layout& layout) const override {
        std::unordered_set<std::uint32_t> unique;
        double objective = 0.0;
        for (const auto value : layout) {
            unique.insert(value);
            objective += static_cast<double>(value * value);
        }
        return {objective, unique.size() == layout.size() && !layout.empty()};
    }
};

}  // namespace

std::unique_ptr<Problem> make_example_problem() {
    return std::make_unique<ExampleProblem>();
}

}  // namespace benchmark_template
