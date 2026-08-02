/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T26 source-anchor, training, inference and GTDE smoke
Paper/DOI: Li et al.; 10.1016/j.apenergy.2025.125908.
Public source provenance, Missing information, Reconstruction, semantic IDs,
production backend and controlling Contract: include/core99/li_t26.hpp.
Claim boundary: tests validate the declared academic reconstruction, not an
author CFD, model, random trajectory or numerical replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/li_t26.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <unistd.h>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    try {
        auto layout = core99::t26::regular_layout(torch::kCPU);
        require(layout.sizes() == torch::IntArrayRef({80, 2}),
                "T26 regular 8x10 layout shape differs");
        const auto artifact = std::filesystem::temp_directory_path()
            / ("core99-t26-" + std::to_string(static_cast<long long>(::getpid())) + ".pt");
        core99::t26::TrainingConfig training;
        training.backend = "cpu";
        training.artifact = artifact.string();
        training.iterations = 2;
        training.batch_size = 48;
        training.workers = 2;
        training.seed = 26001;
        training.smoke = true;
        const auto trained = core99::t26::train_pidnn(training);
        require(std::filesystem::exists(artifact), "T26 artifact missing");
        require(trained.iterations == 2, "T26 smoke iteration contract differs");
        require(std::isfinite(trained.total_loss), "T26 training loss invalid");
        require(trained.table_direct_mae < 0.02, "T26 PIDNN table interpolation too coarse");

        core99::t26::OptimizationConfig optimization;
        optimization.backend = "cpu";
        optimization.artifact = artifact.string();
        optimization.generations = 2;
        optimization.population = 8;
        optimization.workers = 2;
        optimization.seed = 26001;
        optimization.smoke = true;
        const auto result = core99::t26::run_gtde(optimization);
        require(result.population == 8 && result.generations == 2,
                "T26 smoke lifecycle differs");
        require(result.physical_fes == 24, "T26 physical FES differs");
        require(std::abs(result.initial_aep_gwh - 1554.20) < 0.02,
                "T26 regular AEP calibration differs");
        require(result.layout_xy_m.size() == 160U, "T26 final layout size differs");
        require(std::isfinite(result.final_fitness), "T26 final fitness invalid");
        std::filesystem::remove(artifact);
        std::cout << "core99_t26_test_pass\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
