# H0-H4 design: T43__ppga__ppga_nantong_structured_3d_declared_proxy_v1__target

- Paper: `10.1109/jas.2025.125351`
- Method: `ppga_nantong_structured_3d_declared_reconstruction_v2`
- Problem: `ppga_nantong_structured_3d_declared_proxy_v1`
- Protocol: `jas2025_ppga_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `2ed195aecd5d9bdca15746b662fc8f956163ccb38cf84769fb9bccfa25de72ae`

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

Primary source symbol: `hpc/ppga_cpp/src/evolution.cpp::run`.

Claim boundary: P3 16-by-27 structured terrain proxy preserves paper-visible dimensions; original arrays remain unavailable
