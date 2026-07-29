# H0-H4 design: S05__clshade__wfadde_native_declared_24_p3_v1__comparator

- Paper: `10.2139/ssrn.6135326`
- Method: `clshade_paper_derived_e0_physical_fes_v1`
- Problem: `wfadde_native_declared_24_p3_v1`
- Protocol: `ssrn2026_wfadde_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `ab53f25f08fd3c58f2ca7b00f4b2f36008d8d38c92d73d2d6d7a3d32189c920d`

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

Primary source symbol: `hpc/wflop_cpp/src/algorithms.cpp::optimize_lshade`.

Claim boundary: P3 preprint-guided 8 wind conditions x N30/50/80 uses hashed same-lineage arrays and remains distinct from unavailable author originals
