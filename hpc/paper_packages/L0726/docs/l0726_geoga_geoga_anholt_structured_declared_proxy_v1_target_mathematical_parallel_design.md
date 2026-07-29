# H0-H4 design: L0726__geoga__geoga_anholt_structured_declared_proxy_v1__target

- Paper: `10.1109/cbd69312.2025.00059`
- Method: `geoga_declared_reconstruction_v1`
- Problem: `geoga_anholt_structured_declared_proxy_v1`
- Protocol: `cbd2025_geoga_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `89de599d6cccaadd517b7816db3fb4c8f1c01dd963cdd11616ef2509880442d2`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_geometry_mutation_repair -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_geometry_mutation_repair`: W=B*D, S=D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/geoga_cpp/src/evolution.cpp::run`.

Claim boundary: Anholt structured P3 proxy is distinct from the historical GGA-site interoperability proxy
