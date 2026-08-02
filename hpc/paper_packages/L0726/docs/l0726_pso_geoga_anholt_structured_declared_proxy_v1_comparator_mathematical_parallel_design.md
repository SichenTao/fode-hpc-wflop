# H0-H4 design: L0726__pso__geoga_anholt_structured_declared_proxy_v1__comparator

- Paper: `10.1109/cbd69312.2025.00059`
- Method: `pso_canonical_integer_wflop_v1`
- Problem: `geoga_anholt_structured_declared_proxy_v1`
- Protocol: `cbd2025_geoga_native_25_v1`
- Status: `planned_missing_native_comparator`
- JSON SHA-256: `20aab7a3df3b2dc328959f8688baf6c252a596dac4c076d3344afde2896770c7`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_velocity_position_update -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_velocity_position_update`: W=B*D, S=D, barrier_delimited.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `planned_unimplemented_native_comparator`.

Claim boundary: Anholt structured P3 proxy is distinct from the historical GGA-site interoperability proxy
