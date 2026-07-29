# H0-H4 design: T45__alga_attention_declared_reconstruction_v1__alga_guishan_3d_declared_proxy_v1__target

- Paper: `10.1016/j.swevo.2025.102018`
- Method: `alga_attention_declared_reconstruction_v1`
- Problem: `alga_guishan_3d_declared_proxy_v1`
- Protocol: `swevo2025_alga_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `b2200e8fc6701d18e5e03188a327f54dbbe199d6a6977579e9d5ad02e4a0a4d9`

## H0 state machine

T0_train_or_load_artifact -> S0_initialize -> S1_evaluate -> S2_attention_train_mask_variation -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `T0_train_or_load_artifact`: W=Ns*D, S=ceil(Ns/B)*D, barrier_delimited.
- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_attention_train_mask_variation`: W=B*B*D, S=B*D, barrier_delimited.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::optimize_alga_attention_declared_reconstruction`.

Claim boundary: Original Guishan arrays and learned state are unavailable; current planar transfer cannot satisfy the native paper row
