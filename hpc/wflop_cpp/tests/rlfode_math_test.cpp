/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: FQFODE equation, reward-timing, and FODE-core characterization fixtures
Paper title: A Reinforcement Learning-Assisted Fractional-Order Differential Evolution for Solving Wind Farm Layout Optimization Problems
DOI: 10.3390/math13182935
Public asset/source: no author code or Q-table artifact found; evidence dossier docs/source-dossiers/S04.json
Missing information: author Q tables, training seeds, and complete reported training protocol
Reconstruction: fqfode_seeded_training_declared_reconstruction_v1 with seeded training from initialization
Method semantic ID: fqfode_seeded_training_declared_reconstruction_v1
Problem semantic ID: fode_wflop_e0_legacy_v1
Fixture scope: validates declared M3 semantics and fixed-a equivalence only; it does not validate author Q-tables or reported optimization results
Controlling contract: shared/contracts/fqfode_seeded_training_reconstruction_contract.json
Claim boundary: equation and reward-timing fixture only; no author Q-table or reported-result reproduction claim
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "wflop/rlfode_reconstruction.hpp"

#include "fode/case.hpp"
#include "fode/optimizer.hpp"

#include <array>
#include <cmath>
#include <iostream>

namespace {

class FixedController final : public fode::FractionalOrderController {
public:
    double begin_generation(std::uint64_t, double) override {
        return 0.8;
    }

    void finish(double) override {
    }
};

class RecordingController final : public fode::FractionalOrderController {
public:
    double begin_generation(std::uint64_t, double) override {
        return 0.8;
    }

    double begin_generation_with_fes(
        std::uint64_t,
        double,
        std::uint64_t physical_fes
    ) override {
        completed_fes.push_back(physical_fes);
        return 0.8;
    }

    void finish(double) override {
    }

    std::vector<std::uint64_t> completed_fes;
};

}  // namespace

int main(int argc, char** argv) {
    using namespace wflop::rlfode_reconstruction;

    const auto& profiles = sensitivity_profile_descriptors();
    if (profiles.size() != 5
        || parse_sensitivity_profile("baseline")
            != wflop::FqfodeSensitivityProfile::baseline
        || sensitivity_profile_descriptor(
            wflop::FqfodeSensitivityProfile::baseline
        ).effective_semantics_id
            != std::string(
                "fqfode_seeded_training_declared_reconstruction_v1"
            )) {
        return 10;
    }

    if (state_index(0.8) != 80
        || action_delta(0) != -0.01
        || action_delta(1) != 0.0
        || action_delta(2) != 0.01) {
        return 1;
    }
    if (std::abs(additive_fractional_transition(0.8, 2) - 0.81)
        > 1.0e-15
        || additive_fractional_transition(0.9, 2) != 0.9
        || additive_fractional_transition(0.1, 0) != 0.1) {
        return 2;
    }
    if (std::abs(
            multiplicative_fractional_transition(0.8, 2) - 0.808
        ) > 1.0e-15
        || fes_normalized_stage(67, 67, 407) != 0
        || fes_normalized_stage(134, 67, 407) != 0
        || fes_normalized_stage(237, 67, 407) != 2
        || fes_normalized_stage(406, 67, 407) != 3) {
        return 11;
    }

    const std::array<double, 4> history{2.0, 3.0, 4.0, 5.0};
    const double a = 0.8;
    const double expected =
        a * 1.0
        + 0.5 * a * (1.0 - a) * history[0]
        + (1.0 / 6.0) * a * (1.0 - a) * (2.0 - a) * history[1]
        + (1.0 / 24.0) * a * (1.0 - a) * (2.0 - a)
            * (3.0 - a) * history[2]
        + (1.0 / 120.0) * a * (1.0 - a) * (2.0 - a)
            * (3.0 - a) * (4.0 - a) * history[3];
    if (std::abs(
            fractional_history_value(a, 1.0, history, 4) - expected
        ) > 1.0e-15) {
        return 3;
    }
    if (fractional_history_value(a, 1.0, history, 0) != 1.0) {
        return 6;
    }

    QTable table{};
    table[static_cast<std::size_t>(81 * kActionCount + 2)] = 2.0;
    q_update(table, 80, 0, 1.5, 81);
    const double q_expected =
        kLearningRate * (1.5 + kDiscountFactor * 2.0);
    if (std::abs(table[static_cast<std::size_t>(80 * kActionCount)]
                 - q_expected)
        > 1.0e-15) {
        return 4;
    }

    StageQTables tables{};
    const std::string first_hash = qtable_hash(tables);
    tables[0][0] = 1.0;
    if (first_hash == qtable_hash(tables)) {
        return 5;
    }
    if (!validate_policy_update_sequence_fixture()) {
        return 7;
    }

    if (argc != 2) {
        return 8;
    }
    const auto data = fode::load_case(argv[1], "WS2tn50");
    fode::RunConfig config;
    config.seed = 20260729;
    config.physical_fes_budget = 480;
    config.workers = 1;
    const auto baseline = fode::optimize_fode_hpc(data, config);
    FixedController fixed;
    const auto controlled = fode::optimize_fode_hpc_controlled(
        data,
        config,
        fixed
    );
    if (baseline.best_expected_power_kw
            != controlled.best_expected_power_kw
        || baseline.best_layout_1based != controlled.best_layout_1based
        || baseline.physical_fes != controlled.physical_fes
        || baseline.generations != controlled.generations
        || baseline.initial_population != controlled.initial_population
        || baseline.final_population != controlled.final_population) {
        return 9;
    }
    RecordingController recording;
    const auto recorded = fode::optimize_fode_hpc_controlled(
        data,
        config,
        recording
    );
    if (recorded.generations != recording.completed_fes.size()
        || recording.completed_fes.size() < 3
        || recording.completed_fes[0]
            != static_cast<std::uint64_t>(recorded.initial_population)) {
        return 12;
    }
    bool nonuniform_generation_work = false;
    for (std::size_t index = 2; index < recording.completed_fes.size();
         ++index) {
        const auto previous =
            recording.completed_fes[index - 1]
            - recording.completed_fes[index - 2];
        const auto current =
            recording.completed_fes[index]
            - recording.completed_fes[index - 1];
        nonuniform_generation_work =
            nonuniform_generation_work || current != previous;
    }
    if (!nonuniform_generation_work) {
        return 13;
    }
    std::cout << "rlfode_math_fixture_pass\n";
    return 0;
}
