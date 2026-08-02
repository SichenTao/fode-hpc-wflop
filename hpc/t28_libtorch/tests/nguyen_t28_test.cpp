/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T28 CPU semantic smoke test
Paper DOI: 10.5194/wes-10-1661-2025
Public source/data: WINDFLOWER v1.0.0, DOI 10.5281/zenodo.13946931.
Missing/conflicts/resolution/HPC/claim boundary:
include/core99/nguyen_t28.hpp.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/nguyen_t28.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 2) throw std::runtime_error("T28 data directory required");
    core99::t28::Configuration config;
    config.data_directory = argv[1];
    config.backend = "cpu";
    config.iterations = 0;
    config.forecasts = 1;
    config.samples_per_iteration = 20;
    config.workers = 4;
    config.evaluation_limit = 128;
    const auto result = core99::t28::run(config);
    if (!std::isfinite(result.objective_value) || result.aep_gwh <= 0.0) {
        throw std::runtime_error("T28 objective is invalid");
    }
    if (result.x.size() != 72 || result.minimum_spacing_m <= 0.0) {
        throw std::runtime_error("T28 Northwind semantics invalid");
    }
    std::cout << "core99_t28_smoke_pass aep_gwh=" << result.aep_gwh << "\n";
    return 0;
}
