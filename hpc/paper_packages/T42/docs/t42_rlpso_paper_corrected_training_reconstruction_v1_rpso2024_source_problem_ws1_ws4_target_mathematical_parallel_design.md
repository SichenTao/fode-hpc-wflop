# H0-H4 design: T42__rlpso_paper_corrected_training_reconstruction_v1__rpso2024_source_problem_ws1_ws4__target

- Paper: `10.1016/j.energy.2024.134050`
- Method: `rlpso_paper_corrected_training_reconstruction_v1`
- Problem: `rpso2024_source_problem_ws1_ws4`
- Protocol: `energy2024_rlpso_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `78618df2d26ebddcf63fa00a324bca7e1c40339dde915cc70c51c37021ec4540`

## H0 state machine

T0_train_or_load_artifact -> S0_initialize -> S1_evaluate -> S2_ppo_action_and_swarm_update -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `T0_train_or_load_artifact`: W=Ns*D, S=ceil(Ns/B)*D, barrier_delimited.
- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_ppo_action_and_swarm_update`: W=B*D+Ns, S=D+Ns, barrier_delimited.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/wflop_cpp/src/algorithms/rlpso.cpp::optimize_rlpso_paper_corrected_training_reconstruction`.

Claim boundary: Official source has no license and conflicts with paper in action step, seed, and PPO lifecycle; both variants remain separate
