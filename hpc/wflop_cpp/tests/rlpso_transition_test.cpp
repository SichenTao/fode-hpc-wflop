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
    std::cout << "rlpso_transition_fixture_pass\n";
    return 0;
}
