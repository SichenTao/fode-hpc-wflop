# H0-H4 design: T36__tmoea__nysted_paper_eq16_cpu_r4_v2__target

- Paper: `10.1109/cpeee69412.2026.11521465`
- Method: `tmoea_nysted_paper_eq16_v2`
- Problem: `nysted_paper_eq16_cpu_r4_v2`
- Protocol: `cpeee2026_tmoea_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `8bbe803ee6cc6bb693ad834c0f7116d3e8e1ed39d751f86ac6a632e86a4a37aa`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_topology_variation_front_archive -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_topology_variation_front_archive`: W=B*(D*D)+B*B*M, S=D*D+B*M, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/gga_cpp/src/main.cpp::optimize_tmoea`.

Claim boundary: Same-author Nysted assets and audited router form a distinct reconstruction; original paper arrays and topology encoding remain unavailable
