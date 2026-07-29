#include "wflop/rlpso_transition.hpp"

#include <cmath>
#include <iostream>

int main() {
    const double candidate = wflop::rlpso_transition::source_candidate(
        0.2, 0.8, 10.0, 30.0
    );
    if (candidate != 14.0) {
        return 1;
    }
    const double velocity = wflop::rlpso_transition::source_velocity(
        0.9, 2.0, 1.49618, 0.25, 10.0, 1.49618, 0.75, 4.0
    );
    const double expected =
        0.9 * 2.0 + 1.49618 * 0.25 * 10.0
        - 1.49618 * 0.75 * 4.0;
    if (std::abs(velocity - expected) > 1.0e-15) {
        return 2;
    }
    if (!wflop::rlpso_transition::accept_training_candidate(2.0, 1.0)
        || wflop::rlpso_transition::accept_training_candidate(1.0, 1.0)) {
        return 3;
    }
    double r1 = 0.5;
    double r2 = 0.5;
    wflop::rlpso_transition::apply_compact_proxy_action(0, r1, r2);
    if (std::abs(r1 - 0.501) > 1.0e-15 || r2 != 0.5) {
        return 4;
    }
    wflop::rlpso_transition::apply_compact_proxy_action(3, r1, r2);
    if (std::abs(r1 - 0.501) > 1.0e-15
        || std::abs(r2 - 0.499) > 1.0e-15) {
        return 5;
    }
    double weight_alpha = 0.5;
    double weight_beta = 0.5;
    wflop::rlpso_transition::apply_paper_corrected_action(
        1, weight_alpha, weight_beta
    );
    wflop::rlpso_transition::apply_paper_corrected_action(
        2, weight_alpha, weight_beta
    );
    if (std::abs(weight_alpha - 0.499) > 1.0e-15
        || std::abs(weight_beta - 0.499) > 1.0e-15) {
        return 6;
    }
    const double paper_candidate =
        wflop::rlpso_transition::paper_corrected_candidate(
            0.2, 0.8, 10.0, 30.0
        );
    if (paper_candidate != 26.0) {
        return 7;
    }
    std::cout << "rlpso_transition_fixture_pass\n";
    return 0;
}
