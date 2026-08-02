# H0-H4 design: Y06__gga__gga2026_layout_cable__target

- Paper: `10.1016/j.apenergy.2026.127895`
- Method: `gga_source_replay_v1`
- Problem: `gga2026_layout_cable`
- Protocol: `apenergy2026_gga_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `102a9e0b9416eb4776dfd305ef24c9ebb67924a30b9d4d20a15a5ac07defbb29`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_geometry_variation_and_route -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_geometry_variation_and_route`: W=B*(D+D*D), S=D*D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/gga_cpp/src/main.cpp::optimize`.

Claim boundary: Public GitHub is pinned but has no standard license; behavior and problem assets are oracle-only
