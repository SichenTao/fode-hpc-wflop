# H0-H4 design: T44__bde__bde2025_standard_daegwallyeong__target

- Paper: `10.1016/j.energy.2025.137885`
- Method: `bde_paper_source_completed_v1`
- Problem: `bde2025_standard_daegwallyeong`
- Protocol: `energy2025_bde_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `fa2d7cc1f5290c4f7d13cbe5ce66b39214eb2b115c07946108630b605df959b7`

## H0 state machine

S0_initialize -> S1_evaluate -> S2_bipopulation_fusion_mutation -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_bipopulation_fusion_mutation`: W=B*D, S=D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/wflop_cpp/src/algorithms.cpp::optimize_bde`.

Claim boundary: WS1-WS4 source replay is accepted; WS5-WS6 are distinct paper-derived P3 composites
