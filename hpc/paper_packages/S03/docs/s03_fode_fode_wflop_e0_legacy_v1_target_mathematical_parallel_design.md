# H0-H4 design: S03__fode__fode_wflop_e0_legacy_v1__target

- Paper: `10.3390/math13020282`
- Method: `fode_e0_physical_fes`
- Problem: `fode_wflop_e0_legacy_v1`
- Protocol: `math2025_fode_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `df3cf9bdf2d9be913f30e68a6a47b2c45a5d4e68847d63141e23b36672de2870`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_fractional_history_mutation -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_fractional_history_mutation`: W=B*D+D*D, S=D*D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/fode_cpp/src/optimizer.cpp::optimize_fode_hpc`.

Claim boundary: Accepted quality results are reusable; canonical CLI and current pair-keyed H0-H6 receipt still require consolidation
