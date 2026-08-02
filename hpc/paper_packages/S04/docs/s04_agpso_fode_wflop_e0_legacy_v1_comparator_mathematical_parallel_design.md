# H0-H4 design: S04__agpso__fode_wflop_e0_legacy_v1__comparator

- Paper: `10.3390/math13182935`
- Method: `agpso_paper_staged_parallel_e0_physical_fes_v1`
- Problem: `fode_wflop_e0_legacy_v1`
- Protocol: `math2025_fqfode_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `e2d8f959890be7a279b44676f845c0d7ae4baaace0345f20201277c38b37b0c6`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_velocity_position_update -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_velocity_position_update`: W=B*D, S=D, barrier_delimited.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/wflop_cpp/src/algorithms.cpp::optimize_pso`.

Claim boundary: Author Q tables are optional replay evidence; full reproduction regenerates all Q tables from the declared training contract
