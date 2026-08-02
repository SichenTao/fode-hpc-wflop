# H0-H4 design: T40__algsa__cgpso_complex_large_16_v1__comparator

- Paper: `10.1109/jas.2023.123387`
- Method: `algsa_archived_source_reconstruction_v1`
- Problem: `cgpso_complex_large_16_v1`
- Protocol: `jas2023_cgpso_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `397a49a251964c6fd452c45fe37aaa80adcb4caf61f387e72c1745c5927dfd86`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_all_pair_force_and_update -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_all_pair_force_and_update`: W=B*B*D, S=B*D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/wflop_cpp/src/algorithms.cpp::optimize_gravitational_comparator`.

Claim boundary: Paper-native 21x21 four-complex-wind x N40/60/80/100 contract is distinct from FODE; barrier and immediate-update variants remain separate
