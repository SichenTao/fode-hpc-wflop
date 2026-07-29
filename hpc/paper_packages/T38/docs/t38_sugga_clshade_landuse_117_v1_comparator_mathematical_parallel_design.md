# H0-H4 design: T38__sugga__clshade_landuse_117_v1__comparator

- Paper: `10.1016/j.asoc.2023.110306`
- Method: `sugga_frozen_surrogate_e0_physical_fes_v1`
- Problem: `clshade_landuse_117_v1`
- Protocol: `asoc2023_clshade_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `d3fec0d412ecca1f4ddeef831dc66b0955d474bfaddb4a5b16c7e2ed9cc70363`

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

Claim boundary: Paper-native D1-D3 x N15/20/25 x L0-L12 contract uses 20,000 complete layout evaluations and cannot reuse the FODE transfer
