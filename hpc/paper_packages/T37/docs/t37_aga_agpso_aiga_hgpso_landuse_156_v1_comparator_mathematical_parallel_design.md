# H0-H4 design: T37__aga__agpso_aiga_hgpso_landuse_156_v1__comparator

- Paper: `10.1016/j.enconman.2022.116174`
- Method: `aga_paper_first_e0_physical_fes_v1`
- Problem: `agpso_aiga_hgpso_landuse_156_v1`
- Protocol: `enconman2022_agpso_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `a9d158507e1fd1b963642bec0e83628bcdb5ec71052da76182875919eb8590d2`

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

Claim boundary: Paper-staged semantics are primary; native 4x3x13 cases are distinct from FODE 50-case transfer and source ordering remains a separate replay identity
