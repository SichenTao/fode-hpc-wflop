# H0-H4 design: T44__clshade__bde2025_standard_daegwallyeong__comparator

- Paper: `10.1016/j.energy.2025.137885`
- Method: `clshade_paper_derived_e0_physical_fes_v1`
- Problem: `bde2025_standard_daegwallyeong`
- Protocol: `energy2025_bde_native_25_v1`
- Status: `planned_missing_native_comparator`
- JSON SHA-256: `5f0500024d41f47c4ef7fe883e62037702a35229b0dfec6e31ca1988de6c8266`

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

Claim boundary: WS1-WS4 source replay is accepted; WS5-WS6 are distinct paper-derived P3 composites
