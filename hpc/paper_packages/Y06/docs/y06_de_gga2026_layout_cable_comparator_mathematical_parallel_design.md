# H0-H4 design: Y06__de__gga2026_layout_cable__comparator

- Paper: `10.1016/j.apenergy.2026.127895`
- Method: `de_rand_1_bin_integer_wflop_v1`
- Problem: `gga2026_layout_cable`
- Protocol: `apenergy2026_gga_native_25_v1`
- Status: `planned_missing_native_comparator`
- JSON SHA-256: `1901d0f7d7424c2aced8429313788c1cb4c5393a8a328ecc975790679861c121`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_mutation_crossover_repair -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_mutation_crossover_repair`: W=B*D, S=D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `planned_unimplemented_native_comparator`.

Claim boundary: Public GitHub is pinned but has no standard license; behavior and problem assets are oracle-only
