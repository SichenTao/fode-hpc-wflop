# WFLOP HPC benchmark package template

This directory is a copy-ready, fail-closed template for adding one algorithm
and one wind-farm layout optimization problem semantic to the benchmark. It
contains no restricted paper data, third-party source code, or executable
claim about an unavailable original method.

The authority ladder is mandatory. Search the complete authoritative source
surface first: paper and supplement, author repositories and release assets,
publisher data, project pages, cited implementations, and same-author
predecessors. Record URLs, dates, licenses, immutable identifiers, and hashes.
Only fields that remain absent after this bounded search may enter the
reconstruction decision ledger. Every such field receives a new semantic
identifier, rejected alternatives, downstream-effect analysis, and a
sensitivity obligation.

## Sixteen required package elements

| # | Element | Template artifact |
|---:|---|---|
| 1 | Paper/source identity dossier | `metadata/paper_source_identity.json` |
| 2 | Complete source search and bounded-negative evidence | `metadata/source_search_evidence.json`, `metadata/source_search_log.json` |
| 3 | Method/problem evidence-tier schema | `schemas/evidence_tiers.schema.json` |
| 4 | R0–R4 lifecycle checklist | `metadata/r0_r4_checklist.json` |
| 5 | Uncertainty and reconstruction decision ledger | `metadata/uncertainty_decision_ledger.json` |
| 6 | Learned-state and training-work contract | `metadata/learned_state_training_contract.json` |
| 7 | Problem assets and physical-FES contract | `metadata/problem_physical_fes_contract.json` |
| 8 | Scalar oracle and fixed-layout fixture | `fixtures/scalar_oracle.json`, `fixtures/fixed_layout_fixture.json` |
| 9 | Deterministic random-event contract | `metadata/deterministic_random_event_contract.json` |
| 10 | Fact-declared pure-C++ algorithm/problem registration skeleton | `cpp/` |
| 11 | CPU/hybrid/GPU backend interface and fail-closed capability map | `cpp/include/benchmark_template/backend.hpp`, `metadata/backend_capabilities.json` |
| 12 | Semantic, sensitivity, scaling, and formal-campaign tests | `cpp/tests/`, `metadata/formal_campaign_contract.json` |
| 13 | Claim boundary and non-pooling contract | `metadata/claim_boundary_non_pooling.json` |
| 14 | One-command pre-code audit | `scripts/audit_pre_code.py` |
| 15 | One-command post-code R1–R4 audit | `scripts/audit_r1_r4.py` |
| 16 | Reference-only worked-example metadata | `worked-example/fqfode_reference.json` |

`template_manifest.json` freezes this list and the self-test rejects missing,
renamed, or extra numbered elements.

## Use

Copy this directory into a new package, then replace every `REQUIRED_*`
placeholder in `metadata/`, `schemas/`, and `fixtures/`. Do not write the
algorithm or evaluator until the pre-code audit passes:

```bash
python3 scripts/audit_pre_code.py
```

After implementing the pure-C++ package, run the complete R1–R4 gate:

```bash
python3 scripts/audit_r1_r4.py
```

The post-code audit configures an out-of-tree Release build, builds the
package, and runs all four CTest categories. A formal campaign remains blocked
until the R4 checklist, exact physical-FES gate, worker-equivalence gate,
claim boundary, and non-pooling rule pass.

The template itself is checked with:

```bash
python3 scripts/self_test.py
```

Template mode validates placeholders as placeholders. It never converts them
into evidence and never admits an original reproduction claim.
