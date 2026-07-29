# H0-H4 design: Y36__taae_transformer_evolution_declared_reconstruction_v1__taae_zhangbei_structured_declared_proxy_v1__target

- Paper: `10.1109/jas.2026.126233`
- Method: `taae_transformer_evolution_declared_reconstruction_v1`
- Problem: `taae_zhangbei_structured_declared_proxy_v1`
- Protocol: `ma2026_native_25_v1`
- Status: `executable_baseline`
- JSON SHA-256: `795aec6ba17055aa24f050df5123a9d7a5a0d9b2e5cffea197573ddf00138df9`

## H0 state machine

T0_train_or_load_artifact -> S0_initialize -> S1_evaluate -> S2_encode_train_latent_variation_decode_repair -> S3_ordered_commit -> S4_serialize

## H1 work and reuse

- `T0_train_or_load_artifact`: W=Ns*D, S=ceil(Ns/B)*D, barrier_delimited.
- `S0_initialize`: W=B*D, S=D, independent.
- `S1_evaluate`: W=B*Nd*Nt*Nt*Nv, S=Nd*Nt*Nt*Nv, independent.
- `S2_encode_train_latent_variation_decode_repair`: W=Ns*D+B*D*D, S=Ns*D+B*D, independent.
- `S3_ordered_commit`: W=B*log2(B)+B*M, S=B*log2(B)+B*M, ordered_reduction.
- `S4_serialize`: W=B*D, S=B*D, intrinsically_serial.

## H2 dependencies

Independent candidate/model work is barrier-delimited before deterministic stable selection and state publication.

## H3 granularity and bound

retain short loops ordered when task count is below persistent-team dispatch crossover; heavy layout or wind-state work uses all visible workers

## H4 mapping

Primary source symbol: `hpc/taae_cpp/src/evolution.cpp::run_declared_reconstruction`.

Claim boundary: P3 structured Zhangbei-family reconstruction; author arrays and checkpoint remain unavailable
