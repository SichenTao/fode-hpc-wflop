# H0-H4 design: T46__morime__zhang2025_three_objective__comparator

- Paper: `10.1016/j.swevo.2025.101972`
- Method: `morime_paper_comparator_reconstruction_v1`
- Problem: `zhang2025_three_objective`
- Protocol: `swevo2025_moeadp_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `80cbecea40f4a414d406cee4ebd8311d7ca7eb05cb8eaac940e7f5166512e219`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_variation_nondominated_sort_archive -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_variation_nondominated_sort_archive`: W=B*B*M+B*D, S=B*M+D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/pbea_cpp/src/main.cpp::run_morime`.

Claim boundary: Local MATLAB bundle contains partial pcode and no redistribution license; C++ evaluator and six-method results remain independently auditable
