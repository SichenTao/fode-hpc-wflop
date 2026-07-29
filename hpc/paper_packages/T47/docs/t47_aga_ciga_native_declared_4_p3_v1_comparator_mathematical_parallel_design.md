# H0-H4 design: T47__aga__ciga_native_declared_4_p3_v1__comparator

- Paper: `10.1145/3766671.3766786`
- Method: `aga_paper_first_e0_physical_fes_v1`
- Problem: `ciga_native_declared_4_p3_v1`
- Protocol: `eitce2025_ciga_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `bfbb055d669fc0552861b6436071989219f1c254b61623d93125892cc29b7a0a`

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

Claim boundary: P3 four-condition N15 reconstruction uses L0 because the paper does not expose its stated masks; it is never labeled original constrained data
