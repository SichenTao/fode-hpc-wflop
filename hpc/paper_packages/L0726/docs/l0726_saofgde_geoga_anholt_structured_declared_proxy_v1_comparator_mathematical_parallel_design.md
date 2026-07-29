# H0-H4 design: L0726__saofgde__geoga_anholt_structured_declared_proxy_v1__comparator

- Paper: `10.1109/cbd69312.2025.00059`
- Method: `saofgde_paper_comparator_reconstruction_v1`
- Problem: `geoga_anholt_structured_declared_proxy_v1`
- Protocol: `cbd2025_geoga_native_25_v1`
- Status: `planned_missing_native_comparator`
- JSON SHA-256: `3b11c8b7b5ecf9a5c46c6ce9dbfa9fa593ae1001aa1de3a74df0cadcd07cc187`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_geometry_aware_de_variation -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_geometry_aware_de_variation`: W=B*D, S=D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `planned_unimplemented_native_comparator`.

Claim boundary: Anholt structured P3 proxy is distinct from the historical GGA-site interoperability proxy
