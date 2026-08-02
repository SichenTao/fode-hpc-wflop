# H0-H4 design: T43__aga__ppga_nantong_structured_3d_declared_proxy_v1__comparator

- Paper: `10.1109/jas.2025.125351`
- Method: `aga_paper_first_e0_physical_fes_v1`
- Problem: `ppga_nantong_structured_3d_declared_proxy_v1`
- Protocol: `jas2025_ppga_native_25_v1`
- Status: `planned_missing_native_comparator`
- JSON SHA-256: `bc8c940c140aebf5f794d8f285feba773e2b157fe4d040d21e03418257fab892`

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

Claim boundary: P3 16-by-27 structured terrain proxy preserves paper-visible dimensions; original arrays remain unavailable
