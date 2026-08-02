# H0-H4 design: S05__cede__wfadde_native_declared_24_p3_v1__comparator

- Paper: `10.2139/ssrn.6135326`
- Method: `cede_paper_equations_physical_fes_v1`
- Problem: `wfadde_native_declared_24_p3_v1`
- Protocol: `ssrn2026_wfadde_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `6152d43941ef89171ea1381517852a581d2d7fea3cd2d3be1412f5f6153068c1`

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

Primary source symbol: `hpc/wflop_cpp/src/algorithms.cpp::optimize_cede`.

Claim boundary: P3 preprint-guided 8 wind conditions x N30/50/80 uses hashed same-lineage arrays and remains distinct from unavailable author originals
