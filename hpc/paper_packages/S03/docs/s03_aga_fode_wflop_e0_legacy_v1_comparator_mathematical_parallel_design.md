# H0-H4 design: S03__aga__fode_wflop_e0_legacy_v1__comparator

- Paper: `10.3390/math13020282`
- Method: `aga_paper_first_e0_physical_fes_v1`
- Problem: `fode_wflop_e0_legacy_v1`
- Protocol: `math2025_fode_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `bf0d31cee2f6e15da2a9541ae35847315355eaf144f324e5f389d427913e5694`

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

Primary source symbol: `hpc/wflop_cpp/src/algorithms.cpp::optimize_ga`.

Claim boundary: Accepted quality results are reusable; canonical CLI and current pair-keyed H0-H6 receipt still require consolidation
