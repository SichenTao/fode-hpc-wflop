/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: RPSO public-source scalar transition primitives
Paper title: Reinforcement Learning-Based Particle Swarm Optimization for Wind Farm Layout Problems
DOI: 10.1016/j.energy.2024.134050
Paper provides: RPSO equations
Public author code URL: https://raw.githubusercontent.com/toyamaailab/toyamaailab.github.io/main/resource/RPSO_Wind_Code.zip
Public author code revision or archive hash: sha256:44e89c033e90f5aaaa9b84c826c95f29d3b8ad73dd363ff68de99418cdfa93a2
Public code/assets provide: executed candidate and velocity equations
Known missing information: author-result policy lifecycle
Reconstruction performed here: direct scalar transcription for independently tested reuse
Method evidence tier: M0_AUTHOR_SOURCE
Problem evidence tier: not_applicable_shared_infrastructure
Method semantic ID: rpso_public_source_transition_primitives_v1
Problem semantic ID: not_applicable_shared_infrastructure
Controlling contracts: shared/contracts/rlpso_reconstruction_execution_contract.json
Claim boundary: scalar transition oracle only
Last evidence audit date: 2026-07-29
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

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

}  // namespace wflop::rlpso_transition
