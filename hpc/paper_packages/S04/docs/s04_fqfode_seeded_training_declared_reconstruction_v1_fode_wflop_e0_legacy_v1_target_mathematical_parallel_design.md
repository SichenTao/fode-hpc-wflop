# H0-H4 design: S04__fqfode_seeded_training_declared_reconstruction_v1__fode_wflop_e0_legacy_v1__target

- Paper: `10.3390/math13182935`
- Method: `fqfode_seeded_training_declared_reconstruction_v1`
- Problem: `fode_wflop_e0_legacy_v1`
- Protocol: `math2025_fqfode_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `b4f8792f1bfb06750521bb662726a87fc0141107ca526d4fc8b1fa8c51edbe7e`

## H0 state machine

T0_train_or_load_artifact -> S0_initialize -> S1_evaluate -> S2_q_action_fractional_mutation -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `T0_train_or_load_artifact`: W=Ns*D, S=ceil(Ns/B)*D, barrier_delimited.
- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_q_action_fractional_mutation`: W=B*D+1, S=D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/wflop_cpp/src/algorithms/rlfode_seeded_reconstruction.cpp::optimize_rlfode_seeded_training_reconstruction`.

Claim boundary: Author Q tables are optional replay evidence; full reproduction regenerates all Q tables from the declared training contract
