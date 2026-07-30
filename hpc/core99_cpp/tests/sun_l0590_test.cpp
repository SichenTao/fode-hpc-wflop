/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0590 training, equations, cases, constraints and replay
tests
Paper: Sun and Yang, 10.1016/j.apenergy.2023.121554.
Public source: no target code/data/weights; tests exercise the declared
equation-backed reconstruction.
Missing fields: author samples/weights, exact GA/calibration/cost curve.
Reconstruction: deterministic proxy training and real-coded GA completion.
Semantic IDs: l0590_shiren_3d_ann_layout_height_v1;
l0590_real_ga_completed_v1; l0590_mlp_3_5_6_1_from_scratch_v1.
Contract: shared/contracts/core99_l0590_sun_ann_height_2023.json.
Claim boundary: semantic/physical/replay tests, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/sun_l0590.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    core99::l0590::TrainingConfig training;
    training.seed = 590590;
    training.workers = 4;
    training.maximum_epochs = 500;
    training.sample_count = 4096;
    training.target_mse = 1.0e-7;
    core99::l0590::WakeSurrogate surrogate;
    const auto trained = surrogate.train(training);
    require(trained.observed_workers >= 2, "training did not use multicore");
    require(
        std::isfinite(trained.test_mse) && trained.test_mse < 0.005,
        "surrogate test MSE outside smoke tolerance"
    );
    require(
        surrogate.teacher_deficit_ratio(5.0 * 77.0, 0.0, 0.0) > 0.0,
        "teacher centerline deficit missing"
    );
    require(
        surrogate.teacher_deficit_ratio(5.0 * 77.0, 500.0, 0.0) == 0.0,
        "teacher wake boundary failed"
    );
    const auto cases = core99::l0590::paper_case_ids();
    require(cases.size() == 8U, "paper case count mismatch");
    const core99::l0590::Problem e1("l0590_e1", surrogate);
    const core99::l0590::Problem c1("l0590_c1", surrogate);
    const auto layout = core99::l0590::aligned_layout();
    const auto energy = e1.evaluate(layout);
    const auto cost = c1.evaluate(layout);
    require(energy.feasible, "aligned paper layout infeasible");
    require(
        std::abs(energy.total_power_kw - cost.total_power_kw) < 1.0e-9,
        "E1/C1 physical alias mismatch"
    );
    require(
        energy.minimum_spacing_m + 1.0e-9 >= 385.0,
        "aligned layout spacing mismatch"
    );
    require(
        energy.total_power_kw > 0.0 && energy.total_power_kw <= 45390.0,
        "aligned power outside turbine scale"
    );
    const core99::l0590::Problem e4("l0590_e4", surrogate);
    core99::l0590::RunConfig serial;
    serial.seed = 590591;
    serial.workers = 1;
    serial.generations = 2;
    const auto first = e4.optimize(serial);
    auto parallel = serial;
    parallel.workers = 4;
    const auto second = e4.optimize(parallel);
    require(
        first.physical_fes == 192U && second.physical_fes == 192U,
        "physical-FES accounting mismatch"
    );
    require(
        first.scientific_hash == second.scientific_hash,
        "one/multicore optimization replay mismatch"
    );
    require(
        second.observed_workers >= 2,
        "population evaluation did not activate multicore"
    );
    require(
        second.best_evaluation.feasible
            && second.best_evaluation.objective + 1.0e-9
                >= second.initial_best.objective,
        "GA regressed or produced an infeasible result"
    );
    return 0;
}
