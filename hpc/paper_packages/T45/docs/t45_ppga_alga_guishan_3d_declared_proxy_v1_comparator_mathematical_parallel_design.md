# H0-H4 design: T45__ppga__alga_guishan_3d_declared_proxy_v1__comparator

- Paper: `10.1016/j.swevo.2025.102018`
- Method: `ppga_paper_comparator_reconstruction_v1`
- Problem: `alga_guishan_3d_declared_proxy_v1`
- Protocol: `swevo2025_alga_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `5ab7df9c04f1fb144eaa1a93b6de94086ab145819db8b0ad8e3400d7d1e2c30d`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_power_law_variation_repair -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_power_law_variation_repair`: W=B*D, S=D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/wflop_cpp/src/algorithms.cpp::optimize_ppga`.

Claim boundary: Original Guishan arrays and learned state are unavailable; current planar transfer cannot satisfy the native paper row
