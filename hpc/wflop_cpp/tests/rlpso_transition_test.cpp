/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: RLPSO paper/source transition discrepancy fixture
Paper title and DOI: Reinforcement Learning-Based Particle Swarm Optimization
for Wind Farm Layout Problems; 10.1016/j.energy.2024.134050
Paper/source basis: paper equations and official RPSO_Wind_Code archive
Public asset: author archive sha256 44e89c033e90f5aaaa9b84c826c95f29d3b8ad73dd363ff68de99418cdfa93a2;
no license, not redistributed
Missing/conflicts: action step and PPO lifecycle differ between paper and source
Reconstruction: deterministic scalar transition fixtures for both identities
Method/problem semantic IDs: rlpso_paper_corrected_training_reconstruction_v1;
rpso2024_source_problem_ws1_ws4_v1
Controlling contract and claim boundary:
shared/contracts/rlpso_reconstruction_execution_contract.json; fixture only
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
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
