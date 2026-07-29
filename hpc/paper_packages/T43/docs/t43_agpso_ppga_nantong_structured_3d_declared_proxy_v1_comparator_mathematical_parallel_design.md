# H0-H4 design: T43__agpso__ppga_nantong_structured_3d_declared_proxy_v1__comparator

- Paper: `10.1109/jas.2025.125351`
- Method: `agpso_paper_staged_parallel_e0_physical_fes_v1`
- Problem: `ppga_nantong_structured_3d_declared_proxy_v1`
- Protocol: `jas2025_ppga_native_25_v1`
- Status: `planned_missing_native_comparator`
- JSON SHA-256: `a5f8fbfbc1593ce78088b4b48c7fec60f838db63479242ca050757ea210a6a40`

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

Primary source symbol: `planned_unimplemented_native_comparator`.

Claim boundary: P3 16-by-27 structured terrain proxy preserves paper-visible dimensions; original arrays remain unavailable
