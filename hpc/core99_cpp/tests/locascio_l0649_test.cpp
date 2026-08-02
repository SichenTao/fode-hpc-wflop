/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0649 source-oracle, gradient and deterministic-HPC tests
Paper/DOI: LoCascio et al.; 10.1002/WE.2954.
Public source: unlicensed paper-linked revision dcb729f is used only for the
initial-layout AEP and analytical-gradient oracle values recorded below.
Missing and Reconstruction: SNOPT is replaced by projected L-BFGS; details and
the random-grid paper/source conflict are in include/core99/locascio_l0649.hpp.
Semantic IDs: l0649_flowers_aep_analytic_gradient_projected_lbfgs_v1,
l0649_wr7_nine_turbine_14d_square_v1 and
l0649_native_single_optimization_plus_n500_h6_v1.
Claim boundary: source-oracled flexible academic reproduction; full boundary
is recorded in include/core99/locascio_l0649.hpp.
Controlling contract: shared/contracts/core99_l0649_flowers_aep_2024.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/locascio_l0649.hpp"

#include "fode/executor.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool same_bits(const double left, const double right) {
    return std::memcmp(&left, &right, sizeof(double)) == 0;
}

}  // namespace

int main() {
    const core99::l0649::FlowersModel model(20, 10);
    require(model.initial_layout().size() == 9,
            "L0649 initial turbine count differs");
    require(std::abs(model.rotor_diameter_m() - 126.0) < 1.0e-15,
            "L0649 rotor diameter differs");
    require(std::abs(model.boundary_side_m() - 1764.0) < 1.0e-15,
            "L0649 14D boundary differs");
    fode::PersistentExecutor one(1);
    fode::PersistentExecutor all(20);
    const auto serial = model.evaluate(model.initial_layout(), true, one);
    const auto parallel = model.evaluate(model.initial_layout(), true, all);
    require(same_bits(serial.aep_wh, parallel.aep_wh),
            "L0649 one/all-core AEP differs");
    require(serial.gradient_wh_per_m == parallel.gradient_wh_per_m,
            "L0649 one/all-core gradient differs");
    require(parallel.observed_workers > 1,
            "L0649 evaluator did not use multiple workers");
    require(std::abs(serial.aep_wh - 120870064988.85013)
                / 120870064988.85013 < 2.0e-14,
            "L0649 author-source AEP oracle differs");
    constexpr std::array<double, 3> oracle_x{
        -1750849.144333376,
        31330.931077198227,
        1794893.2829660932,
    };
    for (std::size_t index = 0; index < oracle_x.size(); ++index) {
        const std::size_t turbine = index * 4;
        const double observed = serial.gradient_wh_per_m[turbine].x_m;
        require(std::abs(observed - oracle_x[index])
                    / std::max(1.0, std::abs(oracle_x[index])) < 2.0e-12,
                "L0649 author-source gradient oracle differs");
    }

    auto displaced = model.initial_layout();
    constexpr double step = 1.0e-3;
    displaced[4].x_m += step;
    const double plus = model.evaluate(displaced, false, one).aep_wh;
    displaced[4].x_m -= 2.0 * step;
    const double minus = model.evaluate(displaced, false, one).aep_wh;
    const double finite_difference = (plus - minus) / (2.0 * step);
    require(std::abs(finite_difference
                     - serial.gradient_wh_per_m[4].x_m)
                / std::max(1.0, std::abs(finite_difference)) < 1.0e-5,
            "L0649 analytical gradient finite-difference check differs");

    const auto scale_layout = core99::l0649::make_paper_scale_layout(500);
    require(scale_layout.size() == 500,
            "L0649 paper-scale layout count differs");
    constexpr double minimum_spacing_m = 3.0 * 126.0;
    for (std::size_t left = 0; left < scale_layout.size(); ++left) {
        for (std::size_t right = left + 1; right < scale_layout.size(); ++right) {
            const double dx = scale_layout[left].x_m - scale_layout[right].x_m;
            const double dy = scale_layout[left].y_m - scale_layout[right].y_m;
            require(std::hypot(dx, dy) + 1.0e-12 >= minimum_spacing_m,
                    "L0649 corrected scale-grid spacing differs");
        }
    }
    const auto scale = model.evaluate(scale_layout, false, all);
    require(std::isfinite(scale.aep_wh) && scale.aep_wh > 0.0,
            "L0649 N500 evaluation is invalid");
    require(scale.observed_workers > 1,
            "L0649 N500 evaluator did not use multiple workers");

    core99::l0649::RunConfig one_config;
    one_config.workers = 1;
    one_config.smoke = true;
    core99::l0649::RunConfig all_config = one_config;
    all_config.workers = 20;
    const core99::l0649::FlowersModel one_model(1, 10);
    const core99::l0649::FlowersModel all_model(20, 10);
    const auto one_run = core99::l0649::run(one_model, one_config);
    const auto all_run = core99::l0649::run(all_model, all_config);
    require(one_run.scientific_hash == all_run.scientific_hash,
            "L0649 one/all-core optimizer hash differs");
    require(one_run.final_layout == all_run.final_layout,
            "L0649 one/all-core optimizer layout differs");
    require(same_bits(one_run.final_evaluation.aep_wh,
                      all_run.final_evaluation.aep_wh),
            "L0649 one/all-core optimized AEP differs");
    require(all_run.observed_workers > 1,
            "L0649 optimizer did not engage multiple workers");
    require(all_run.final_evaluation.aep_wh
                >= all_run.initial_evaluation.aep_wh,
            "L0649 smoke optimization reduced AEP");
    std::cout << "L0649 semantic and deterministic-HPC tests passed\n";
    return 0;
}
