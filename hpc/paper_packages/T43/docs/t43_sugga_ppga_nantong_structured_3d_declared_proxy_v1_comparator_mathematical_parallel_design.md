# H0-H4 design: T43__sugga__ppga_nantong_structured_3d_declared_proxy_v1__comparator

- Paper: `10.1109/jas.2025.125351`
- Method: `sugga_frozen_surrogate_e0_physical_fes_v1`
- Problem: `ppga_nantong_structured_3d_declared_proxy_v1`
- Protocol: `jas2025_ppga_native_25_v1`
- Status: `planned_missing_native_comparator`
- JSON SHA-256: `3067947bbed08cae0e9a7a7f0467e1886c3bb0e070836226f400b2e6ad1c2eaf`

## H0 state machine

T0_train_or_load_artifact -> S0_initialize -> S1_evaluate -> S2_surrogate_guided_variation -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `T0_train_or_load_artifact`: W=Ns*D, S=ceil(Ns/B)*D, barrier_delimited.
- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_surrogate_guided_variation`: W=B*D, S=D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `planned_unimplemented_native_comparator`.

Claim boundary: P3 16-by-27 structured terrain proxy preserves paper-visible dimensions; original arrays remain unavailable
