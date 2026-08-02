# H0-H4 design: T40__cgpso__cgpso_complex_large_16_v1__target

- Paper: `10.1109/jas.2023.123387`
- Method: `cgpso_paper_staged_parallel_e0_physical_fes_v1`
- Problem: `cgpso_complex_large_16_v1`
- Protocol: `jas2023_cgpso_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `49b4078249f158777a3c940a98323187b10d68288d4c677833d5e5a1109c8628`

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

Claim boundary: Paper-native 21x21 four-complex-wind x N40/60/80/100 contract is distinct from FODE; barrier and immediate-update variants remain separate
