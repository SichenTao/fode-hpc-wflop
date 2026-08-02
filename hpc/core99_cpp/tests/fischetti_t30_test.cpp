/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T30 deterministic objective and fixed-work smoke test
Paper DOI: 10.1007/s10732-015-9283-4
Public source: none found; open author thesis 20.500.12608/17839.
Missing/conflicts/reconstruction/HPC/claim boundary:
include/core99/fischetti_t30.hpp.
Semantic IDs and Contract: shared/contracts/core99_t30_fischetti_proxy_2016.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/fischetti_t30.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {
    core99::t30::Problem one(180, 0, 1);
    core99::t30::Problem four(180, 0, 4);
    for (int i = 0; i < 180; i += 17) {
        for (int j = i + 1; j < 180; j += 19) {
            if (one.pair_loss_mw(i, j) != four.pair_loss_mw(i, j)) {
                throw std::runtime_error("T30 worker-count matrix mismatch");
            }
        }
    }
    core99::t30::Configuration config;
    config.sites = 180;
    config.instance = 0;
    config.workers = 4;
    config.fixed_moves = 8;
    config.time_limit_seconds = 1.0;
    config.seed = 30;
    const auto result = core99::t30::run(four, config);
    if (
        !std::isfinite(result.best_objective_mw)
        || result.best_objective_mw + 1e-12 < result.initial_objective_mw
        || result.minimum_spacing_m < 400.0
        || result.selected.empty()
    ) {
        throw std::runtime_error("T30 smoke semantics failed");
    }
    std::cout
        << "core99_t30_smoke_pass sites=" << result.sites
        << " turbines=" << result.turbines
        << " objective=" << result.best_objective_mw << "\n";
    return 0;
}
