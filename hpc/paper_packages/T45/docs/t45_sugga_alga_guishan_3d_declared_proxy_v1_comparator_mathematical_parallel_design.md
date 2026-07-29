# H0-H4 design: T45__sugga__alga_guishan_3d_declared_proxy_v1__comparator

- Paper: `10.1016/j.swevo.2025.102018`
- Method: `sugga_frozen_surrogate_e0_physical_fes_v1`
- Problem: `alga_guishan_3d_declared_proxy_v1`
- Protocol: `swevo2025_alga_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `79175b7bd546ae4a2012a5290ab74f2045cb62298275887799ea5d892a47566b`

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

Claim boundary: Original Guishan arrays and learned state are unavailable; current planar transfer cannot satisfy the native paper row
