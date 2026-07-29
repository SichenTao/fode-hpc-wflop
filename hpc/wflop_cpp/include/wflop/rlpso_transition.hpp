/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: RPSO scalar transition and paper-action primitives
Paper title: Reinforcement Learning-Based Particle Swarm Optimization for Wind Farm Layout Problems
DOI: 10.1016/j.energy.2024.134050
Paper provides: RPSO equations and the 0.001 four-action weight transition
Public author code URL: https://raw.githubusercontent.com/toyamaailab/toyamaailab.github.io/main/resource/RPSO_Wind_Code.zip
Public author code revision or archive hash: sha256:44e89c033e90f5aaaa9b84c826c95f29d3b8ad73dd363ff68de99418cdfa93a2
Public code/assets provide: executed candidate and velocity equations
Known missing information: author-result policy lifecycle
Reconstruction performed here: direct scalar source transcription plus the
  separately identified paper-corrected bounded action transition
Method evidence tier: M3_DECLARED_COMPLETION
Problem evidence tier: not_applicable_shared_infrastructure
Method semantic ID: rpso_transition_and_paper_action_primitives_v1
Problem semantic ID: not_applicable_shared_infrastructure
Controlling contracts: shared/contracts/rlpso_reconstruction_execution_contract.json
Claim boundary: scalar fixtures only; no learned policy or optimization claim
Last evidence audit date: 2026-07-29
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <algorithm>

namespace wflop::rlpso_transition {

inline double source_candidate(
    double r1,
    double r2,
    double personal_best,
    double global_best
) {
    return r2 * personal_best + r1 * global_best;
}

inline double source_velocity(
    double omega,
    double previous_velocity,
    double c1,
    double e1,
    double personal_best,
    double c2,
    double e2,
    double position
) {
    return omega * previous_velocity
        + c1 * e1 * personal_best
        - c2 * e2 * position;
}

inline bool accept_training_candidate(
    double candidate_fitness,
    double global_best_fitness
) {
    return candidate_fitness > global_best_fitness;
}

inline void apply_compact_proxy_action(
    int action,
    double& r1,
    double& r2
) {
    constexpr double step = 0.001;
    if (action == 0) {
        r1 += step;
    } else if (action == 1) {
        r1 -= step;
    } else if (action == 2) {
        r2 += step;
    } else if (action == 3) {
        r2 -= step;
    }
    r1 = std::clamp(r1, 0.0, 1.0);
    r2 = std::clamp(r2, 0.0, 1.0);
}

inline void apply_paper_corrected_action(
    int action,
    double& weight_alpha,
    double& weight_beta
) {
    constexpr double step = 0.001;
    if (action == 0) {
        weight_alpha += step;
    } else if (action == 1) {
        weight_beta -= step;
    } else if (action == 2) {
        weight_alpha -= step;
    } else if (action == 3) {
        weight_beta += step;
    }
    weight_alpha = std::clamp(weight_alpha, 0.0, 1.0);
    weight_beta = std::clamp(weight_beta, 0.0, 1.0);
}

inline double paper_corrected_candidate(
    double weight_alpha,
    double weight_beta,
    double personal_best,
    double global_best
) {
    return weight_alpha * personal_best + weight_beta * global_best;
}

}  // namespace wflop::rlpso_transition
