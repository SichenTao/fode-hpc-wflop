# H0-H4 design: Y34__bde__lsde_large_declared_12_p3_v1__comparator

- Paper: `10.1049/cit2.70150`
- Method: `bde_paper_equations_physical_fes_v1`
- Problem: `lsde_large_declared_12_p3_v1`
- Protocol: `cit2026_lsde_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `61bb8fb766652a4fdf1224f4e30e299cc16f39608d58884785521b6dbbc58959`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_bipopulation_fusion_mutation -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_bipopulation_fusion_mutation`: W=B*D, S=D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/wflop_cpp/src/algorithms.cpp::optimize_bde`.

Claim boundary: P3 15x15 x N30/50/100 contract uses hashed same-lineage 4-7-direction arrays because original arrays are unpublished
