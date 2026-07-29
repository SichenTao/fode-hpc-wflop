# H0-H4 design: S03__ise__fode_wflop_e0_legacy_v1__comparator

- Paper: `10.3390/math13020282`
- Method: `ise_paper_derived_e0_physical_fes_v1`
- Problem: `fode_wflop_e0_legacy_v1`
- Protocol: `math2025_fode_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `a8f1bdb6f43df7be91a6605fa145a3e1c375dfd5e4336fccd7057e80330d93ca`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_spherical_coordinate_variation -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_spherical_coordinate_variation`: W=B*D, S=D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/wflop_cpp/src/algorithms.cpp::optimize_ise`.

Claim boundary: Accepted quality results are reusable; canonical CLI and current pair-keyed H0-H6 receipt still require consolidation
