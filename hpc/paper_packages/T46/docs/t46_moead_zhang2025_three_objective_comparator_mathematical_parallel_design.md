# H0-H4 design: T46__moead__zhang2025_three_objective__comparator

- Paper: `10.1016/j.swevo.2025.101972`
- Method: `moead_paper_comparator_reconstruction_v1`
- Problem: `zhang2025_three_objective`
- Protocol: `swevo2025_moeadp_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `5dc12eec0e7e7e5150584d8bc2e350473add44eda799326eb290cd9c8b29af95`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_neighborhood_variation_scalarization_archive -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_neighborhood_variation_scalarization_archive`: W=B*B*M+B*D, S=B*M+D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/pbea_cpp/src/main.cpp::run_optimizer`.

Claim boundary: Local MATLAB bundle contains partial pcode and no redistribution license; C++ evaluator and six-method results remain independently auditable
