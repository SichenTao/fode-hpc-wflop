# H0-H4 design: T37__agpso__agpso_aiga_hgpso_landuse_156_v1__target

- Paper: `10.1016/j.enconman.2022.116174`
- Method: `agpso_paper_staged_parallel_e0_physical_fes_v1`
- Problem: `agpso_aiga_hgpso_landuse_156_v1`
- Protocol: `enconman2022_agpso_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `042cd65e0d491f7ef42ed1eadd319eab4f98d9803dbdd688653d71d096a802d8`

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

Claim boundary: Paper-staged semantics are primary; native 4x3x13 cases are distinct from FODE 50-case transfer and source ordering remains a separate replay identity
