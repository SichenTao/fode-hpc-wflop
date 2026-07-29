# H0-H4 design: T45__ga__alga_guishan_3d_declared_proxy_v1__comparator

- Paper: `10.1016/j.swevo.2025.102018`
- Method: `ga_paper_comparator_reconstruction_v1`
- Problem: `alga_guishan_3d_declared_proxy_v1`
- Protocol: `swevo2025_alga_native_25_v1`
- Status: `planned_missing_native_comparator`
- JSON SHA-256: `3098b3069113882964a4e64fa547efea058140be45ddbdd17654f72e4c13556f`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_selection_crossover_mutation_repair -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_selection_crossover_mutation_repair`: W=B*D, S=D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `planned_unimplemented_native_comparator`.

Claim boundary: Original Guishan arrays and learned state are unavailable; current planar transfer cannot satisfy the native paper row
