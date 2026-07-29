# H0-H4 design: T41__cgpso__agpso_aiga_hgpso_landuse_156_v1__comparator

- Paper: `10.1007/s42235-024-00498-3`
- Method: `cgpso_paper_staged_parallel_e0_physical_fes_v1`
- Problem: `agpso_aiga_hgpso_landuse_156_v1`
- Protocol: `jbe2024_aiga_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `402a5345418b96e7edc8eb8d53737601f038bbb66223a50f8e1ce5519fdc9e28`

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

Claim boundary: No author source was found; paper-derived algorithm now pairs with the explicit 4x3x13 native contract rather than FODE transfer
