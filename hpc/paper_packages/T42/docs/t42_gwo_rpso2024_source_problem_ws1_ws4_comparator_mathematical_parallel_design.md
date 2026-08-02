# H0-H4 design: T42__gwo__rpso2024_source_problem_ws1_ws4__comparator

- Paper: `10.1016/j.energy.2024.134050`
- Method: `gwo_paper_comparator_reconstruction_v1`
- Problem: `rpso2024_source_problem_ws1_ws4`
- Protocol: `energy2024_rlpso_native_25_v1`
- Status: `planned_missing_native_comparator`
- JSON SHA-256: `ce3d4b64faef7f2ecb7f00142feaeb130cfebd3e13995e8a385c9221a137e2a0`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_three_leader_position_update -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_three_leader_position_update`: W=B*D, S=D, barrier_delimited.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `planned_unimplemented_native_comparator`.

Claim boundary: Official source has no license and conflicts with paper in action step, seed, and PPO lifecycle; both variants remain separate
