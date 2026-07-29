# H0-H4 design: L0608__cjade__alshade_complex_wake_117_v1__comparator

- Paper: `10.1109/pic62406.2024.10892732`
- Method: `cjade_archived_source_reconstruction_v1`
- Problem: `alshade_complex_wake_117_v1`
- Protocol: `pic2024_alshade_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `a68ab2a2dc0084474203a060fdb08821938c41a9ff711004eafebdaa373a4dbc`

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

Claim boundary: P1 paper-declared WC1-WC3 x N15/20/25 x L0-L12 contract is executable; comparator closure and H0-H6 admission remain required
