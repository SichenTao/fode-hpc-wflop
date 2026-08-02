# H0-H4 design: T42__cgpso__rpso2024_source_problem_ws1_ws4__comparator

- Paper: `10.1016/j.energy.2024.134050`
- Method: `cgpso_paper_staged_parallel_e0_physical_fes_v1`
- Problem: `rpso2024_source_problem_ws1_ws4`
- Protocol: `energy2024_rlpso_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `854e89f1448e62a28f150cf5966f73c0e4d976e5139c99e41c1a9812df0ff16b`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_velocity_position_update -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_velocity_position_update`: W=B*D, S=D, barrier_delimited.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/wflop_cpp/src/algorithms.cpp::optimize_pso`.

Claim boundary: Official source has no license and conflicts with paper in action step, seed, and PPO lifecycle; both variants remain separate
