# H0-H4 design: Y36__nsgaii__taae_zhangbei_structured_declared_proxy_v1__comparator

- Paper: `10.1109/jas.2026.126233`
- Method: `nsgaii_paper_comparator_reconstruction_v1`
- Problem: `taae_zhangbei_structured_declared_proxy_v1`
- Protocol: `ma2026_native_25_v1`
- Status: `planned_missing_native_comparator`
- JSON SHA-256: `bc6067df8e330a3afd844432a920b8fde2253e190cb2c83d353e78944f9728fd`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_variation_nondominated_sort_archive -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_variation_nondominated_sort_archive`: W=B*B*M+B*D, S=B*M+D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `planned_unimplemented_native_comparator`.

Claim boundary: P3 structured Zhangbei-family reconstruction; author arrays and checkpoint remain unavailable
