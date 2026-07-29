# H0-H4 design: T47__ciga__ciga_native_declared_4_p3_v1__target

- Paper: `10.1145/3766671.3766786`
- Method: `ciga_paper_derived_e0_physical_fes_v1`
- Problem: `ciga_native_declared_4_p3_v1`
- Protocol: `eitce2025_ciga_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `3ddb9aa1fd1c2a99c27b61b261fe5b7f664e260b2900efa812a50c6902a5dbeb`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_compact_distribution_update -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_compact_distribution_update`: W=B*D, S=D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/wflop_cpp/src/algorithms.cpp::optimize_ciga`.

Claim boundary: P3 four-condition N15 reconstruction uses L0 because the paper does not expose its stated masks; it is never labeled original constrained data
