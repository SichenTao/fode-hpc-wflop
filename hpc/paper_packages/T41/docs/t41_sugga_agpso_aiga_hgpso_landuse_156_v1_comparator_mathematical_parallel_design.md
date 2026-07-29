# H0-H4 design: T41__sugga__agpso_aiga_hgpso_landuse_156_v1__comparator

- Paper: `10.1007/s42235-024-00498-3`
- Method: `sugga_frozen_surrogate_e0_physical_fes_v1`
- Problem: `agpso_aiga_hgpso_landuse_156_v1`
- Protocol: `jbe2024_aiga_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `5b98211b977e9cfdac9b6414b2a0c251184bba7d1a5c094c706566f4492031c5`

## H0 state machine

T0_train_or_load_artifact -> S0_initialize -> S1_evaluate -> S2_surrogate_guided_variation -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `T0_train_or_load_artifact`: W=Ns*D, S=ceil(Ns/B)*D, barrier_delimited.
- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_surrogate_guided_variation`: W=B*D, S=D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/wflop_cpp/src/algorithms.cpp::optimize_ga`.

Claim boundary: No author source was found; paper-derived algorithm now pairs with the explicit 4x3x13 native contract rather than FODE transfer
