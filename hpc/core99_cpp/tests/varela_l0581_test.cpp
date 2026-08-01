/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0581 semantic, numerical and deterministic-HPC tests.
Paper/DOI, public source, missing assets, conflicts, reconstruction,
semantic IDs, HPC design and claim boundary:
hpc/core99_cpp/include/core99/varela_l0581.hpp.
Controlling contract: shared/contracts/core99_l0581_sparse_gradient_2023.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/varela_l0581.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    const auto sizes = core99::l0581::paper_accuracy_sizes();
    require(sizes == std::vector<int>({38, 63, 95, 133, 177, 228, 285, 349}),
            "L0581 paper accuracy sizes differ");
    for (const int turbines : sizes) {
        const auto spec = core99::l0581::farm_spec(turbines);
        require(spec.turbines == turbines, "L0581 farm size differs");
        require(static_cast<int>(core99::l0581::round_layout(turbines).size())
                    == turbines,
                "L0581 round layout size differs");
        require(spec.actual_minimum_initial_spacing_d > 4.9,
                "L0581 source-lineage ring spacing too small");
        require(spec.actual_minimum_initial_spacing_d < 5.1,
                "L0581 paper/source geometry conflict disappeared");
    }

    const auto layout = core99::l0581::round_layout(38);
    const auto dense_one = core99::l0581::calculate_gradient(
        layout, 270.0, core99::l0581::GradientMode::dense, 0.0, 1);
    const auto dense_all = core99::l0581::calculate_gradient(
        layout, 270.0, core99::l0581::GradientMode::dense, 0.0, 20);
    require(dense_one.gradient == dense_all.gradient,
            "L0581 dense one/all-core gradient differs");
    require(std::bit_cast<std::uint64_t>(dense_one.normalized_aep)
                == std::bit_cast<std::uint64_t>(dense_all.normalized_aep),
            "L0581 dense one/all-core objective differs");
    require(dense_one.colors == 76 && dense_all.colors == 76,
            "L0581 dense color count differs");
    require(dense_all.observed_workers > 1,
            "L0581 dense all-core execution did not participate");
    for (const double value : dense_all.gradient) {
        require(std::isfinite(value), "L0581 dense gradient non-finite");
    }

    const auto accuracy = core99::l0581::compare_accuracy(38, 1.0e-12, 20);
    require(accuracy.sparse_colors > 0
                && accuracy.sparse_colors < accuracy.dense_colors,
            "L0581 sparse coloring did not compress");
    require(std::isfinite(accuracy.maximum_scaled_error)
                && accuracy.maximum_scaled_error < 1.0,
            "L0581 sparse gradient error invalid");

    core99::l0581::OptimizationConfig sparse;
    sparse.mode = core99::l0581::GradientMode::sparse;
    sparse.seed = 2023058101ULL;
    sparse.workers = 20;
    sparse.maximum_iterations = 2;
    sparse.smoke = true;
    const auto optimized = core99::l0581::optimize(sparse);
    require(optimized.observed_workers > 1,
            "L0581 optimizer did not engage multiple workers");
    require(optimized.final_wake_loss_percent
                <= optimized.initial_wake_loss_percent + 1.0e-10,
            "L0581 optimization increased wake loss");
    require(optimized.final_layout.size() == 95,
            "L0581 optimization layout size differs");
    std::cout << "L0581 semantic and deterministic-HPC tests passed\n";
    return 0;
}
