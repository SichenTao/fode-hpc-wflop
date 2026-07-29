# H0-H4 design: Y34__lsde__lsde_large_declared_12_p3_v1__target

- Paper: `10.1049/cit2.70150`
- Method: `lsde_paper_derived_e0_physical_fes_v1`
- Problem: `lsde_large_declared_12_p3_v1`
- Protocol: `cit2026_lsde_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `19c70a2ffa814b3ea4f8c417657c92ed1868f83799669d5cfa16d99d3b03505f`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_subpopulation_update_migration -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_subpopulation_update_migration`: W=B*D, S=D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/wflop_cpp/src/algorithms.cpp::optimize_lsde`.

Claim boundary: P3 15x15 x N30/50/100 contract uses hashed same-lineage 4-7-direction arrays because original arrays are unpublished
