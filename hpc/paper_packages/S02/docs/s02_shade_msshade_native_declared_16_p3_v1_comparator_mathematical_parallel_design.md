# H0-H4 design: S02__shade__msshade_native_declared_16_p3_v1__comparator

- Paper: `10.3390/electronics13163196`
- Method: `shade_success_history_integer_wflop_v1`
- Problem: `msshade_native_declared_16_p3_v1`
- Protocol: `electronics2024_msshade_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `53ab439ed1b68ea2759b6016e936f162f131dbdff5aeb2ddc1a110c1b205fc0a`

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

Claim boundary: P3 hashed same-lineage 2-5-direction arrays reconstruct the unpublished random samples; paper roulette and local source blocks remain separate
