# H0-H4 design: T39__lshade__ise_landuse_117_v1__comparator

- Paper: `10.1016/j.engappai.2023.106198`
- Method: `lshade_paper_first_e0_physical_fes_v1`
- Problem: `ise_landuse_117_v1`
- Protocol: `engappai2023_ise_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `97bf8a3d0e874616346495e6d1db4122a0980da32313268676d0f7bdbc1c6d09`

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

Claim boundary: Paper-native P1-P3 x N15/20/25 x L0-L12 uses 154 m cells and 20,000 evaluations; FODE transfer results are not reusable
