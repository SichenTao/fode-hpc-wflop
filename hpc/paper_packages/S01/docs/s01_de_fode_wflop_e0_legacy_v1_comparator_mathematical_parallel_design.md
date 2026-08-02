# H0-H4 design: S01__de__fode_wflop_e0_legacy_v1__comparator

- Paper: `10.3390/math12233762`
- Method: `de_rand_1_bin_integer_wflop_v1`
- Problem: `fode_wflop_e0_legacy_v1`
- Protocol: `math2024_cede_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `569c127dbb775755c4c9a4a844908633e19dd58055023c4dbdc2e220945fb123`

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

Primary source symbol: `hpc/wflop_cpp/src/algorithms.cpp::optimize_de_comparator`.

Claim boundary: Paper FES and two local source reduction directions conflict; identities must remain separate
