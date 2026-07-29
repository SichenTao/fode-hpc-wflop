# H0-H4 design: Y35__cgpso__agpso_aiga_hgpso_landuse_156_v1__comparator

- Paper: `10.26599/tst.2026.9010059`
- Method: `cgpso_paper_staged_parallel_e0_physical_fes_v1`
- Problem: `agpso_aiga_hgpso_landuse_156_v1`
- Protocol: `tst2026_hgpso_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `37f450b3a251fc0bd584b5ecb7a697eb9d3680f8b87e79e264c360a16eb71624`

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

Claim boundary: Four wind scenarios x N15/20/25 x L0-L12 now match the paper/source contract; paper and source algorithm conflicts remain separate
