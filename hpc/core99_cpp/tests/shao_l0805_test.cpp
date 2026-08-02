/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0805 semantic, numerical and deterministic-HPC tests
Paper/DOI: Shao et al.; 10.1016/J.ENERGY.2025.138820.
Public source, missing assets, conflicts, reconstruction, HPC analysis,
semantic IDs and claim boundary:
hpc/core99_cpp/include/core99/shao_l0805.hpp.
Controlling contract: shared/contracts/core99_l0805_pce_kriging_2025.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/shao_l0805.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    const auto ids = core99::l0805::paper_case_ids();
    require(ids.size() == 4, "L0805 paper-case count differs");
    const int turbines[]{8, 16, 32, 8};
    const int grids[]{9, 13, 17, 9};
    const int initial[]{80, 160, 320, 160};
    const int truth[]{343, 567, 839, 272};
    const int states[]{50, 50, 50, 8};
    for (std::size_t index = 0; index < ids.size(); ++index) {
        const auto spec = core99::l0805::case_spec(ids[index]);
        require(spec.turbines == turbines[index], "L0805 turbine count");
        require(spec.grid_width == grids[index], "L0805 grid width");
        require(spec.initial_layout_samples == initial[index],
                "L0805 initial samples");
        require(spec.target_layout_evaluations == truth[index],
                "L0805 truth calls");
        require(spec.wind_samples_per_layout == states[index],
                "L0805 wind samples");
        const auto layout = core99::l0805::perimeter_layout(spec);
        const core99::l0805::Problem problem(ids[index]);
        const auto evaluation = problem.evaluate(
            layout, 2026080501ULL, spec.high_fidelity_proxy ? 0 : 4);
        require(evaluation.feasible, "L0805 perimeter layout infeasible");
        require(std::isfinite(evaluation.aep_gwh) && evaluation.aep_gwh > 0.0,
                "L0805 paper-case AEP invalid");
        require(evaluation.minimum_spacing_margin_m >= -1.0e-12,
                "L0805 spacing contract differs");
    }

    const double expected = 1.0 / std::sqrt(2.0 * std::numbers::pi);
    require(std::abs(core99::l0805::expected_improvement(10.0, 1.0, 10.0)
                     - expected) < 1.0e-14,
            "L0805 standard EI equation differs");
    require(core99::l0805::expected_improvement(11.0, 0.0, 10.0) == 1.0,
            "L0805 zero-variance EI differs");

    const core99::l0805::Problem scale("l0805_case_iii");
    const auto serial_batch = scale.profile_batch(80, 805U, 1);
    const auto parallel_batch = scale.profile_batch(80, 805U, 20);
    require(serial_batch.scientific_hash == parallel_batch.scientific_hash,
            "L0805 one/all-core batch hash differs");
    require(std::bit_cast<std::uint64_t>(serial_batch.aep_checksum_gwh)
                == std::bit_cast<std::uint64_t>(
                    parallel_batch.aep_checksum_gwh),
            "L0805 one/all-core batch checksum differs");
    require(parallel_batch.observed_workers > 1,
            "L0805 batch did not engage multiple workers");

    core99::l0805::RunConfig one;
    one.workers = 1;
    one.smoke = true;
    one.maximum_ga_generations = 8;
    core99::l0805::RunConfig all = one;
    all.workers = 20;
    const core99::l0805::Problem smoke_one("l0805_case_i");
    const core99::l0805::Problem smoke_all("l0805_case_i");
    const auto serial_run = smoke_one.optimize(one);
    const auto parallel_run = smoke_all.optimize(all);
    require(serial_run.scientific_hash == parallel_run.scientific_hash,
            "L0805 one/all-core optimizer hash differs");
    require(serial_run.best_layout == parallel_run.best_layout,
            "L0805 one/all-core optimizer layout differs");
    require(serial_run.best_history_gwh == parallel_run.best_history_gwh,
            "L0805 one/all-core optimizer history differs");
    require(parallel_run.observed_workers > 1,
            "L0805 optimizer did not engage multiple workers");
    require(parallel_run.best_evaluation.aep_gwh
                >= parallel_run.initial_best.aep_gwh,
            "L0805 optimization reduced best AEP");
    std::cout << "L0805 semantic and deterministic-HPC tests passed\n";
    return 0;
}
