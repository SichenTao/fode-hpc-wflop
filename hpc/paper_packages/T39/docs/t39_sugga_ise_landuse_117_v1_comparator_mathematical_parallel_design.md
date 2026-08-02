# H0-H4 design: T39__sugga__ise_landuse_117_v1__comparator

- Paper: `10.1016/j.engappai.2023.106198`
- Method: `sugga_frozen_surrogate_e0_physical_fes_v1`
- Problem: `ise_landuse_117_v1`
- Protocol: `engappai2023_ise_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `b96a3b85248eb2b4ab419da2c0b43d0ccfeeb09101f7c03290c5f3cbf2fc4bc4`

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

Claim boundary: Paper-native P1-P3 x N15/20/25 x L0-L12 uses 154 m cells and 20,000 evaluations; FODE transfer results are not reusable
