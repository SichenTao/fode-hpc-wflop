# H0-H4 design: T36__smpso__nysted_paper_eq16_cpu_r4_v2__comparator

- Paper: `10.1109/cpeee69412.2026.11521465`
- Method: `smpso_paper_comparator_reconstruction_v1`
- Problem: `nysted_paper_eq16_cpu_r4_v2`
- Protocol: `cpeee2026_tmoea_native_25_v1`
- Status: `planned_missing_native_comparator`
- JSON SHA-256: `896225962f00c72b41414915540637c39ebd34413ce9565447e1186cb3e7f8d2`

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

Claim boundary: Same-author Nysted assets and audited router form a distinct reconstruction; original paper arrays and topology encoding remain unavailable
