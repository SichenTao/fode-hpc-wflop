# Gao–Tao WFLOP-HPC roadmap

## Scope

The inclusion boundary is every public, citable paper that directly studies a
wind-farm layout optimization problem and is signed by Shangce Gao or Sichen
Tao. Generic optimizer papers and private manuscripts are excluded until they
define or evaluate a direct WFLOP study.

The registry is append-only. A new homepage record is added; an old record is
not silently removed. Coauthor spelling is preserved so works by Jiayi Li,
Baohang Zhang, Chen Zhang, Yuhang Ma, Jiaru Yang, and every other coauthor in
scope remain discoverable.

The 2026-07-29 frozen scope contains 23 unique DOI records. The coverage
contract in `lineage_scope_contract.json` and
`scripts/audit_lineage_registry.py` prevents those records or the explicitly
named coauthor lines from disappearing during later refactors. The count is a
minimum, not a permanent maximum.

## Reproducible package lifecycle

| Gate | Required output | Blocking condition |
|---|---|---|
| R0 identity | DOI, title, authors, PDF/source status, license, hashes | paper identity unresolved |
| R1 problem | variables, objective, constraints, wake model, sampling, FES | mathematical problem ambiguous |
| R2 algorithm | equations, parameters, stage order, random-event contract | method identity ambiguous |
| R3 reproduction | bounded reference run, discrepancy ledger, fixtures | implementation not executable |
| R4 HPC | pure C++ kernel, safe parallel stages, semantic tests, scaling receipt | semantic or exact-work gate fails |

Missing public source is not fatal: the package becomes `paper_derived` and
every inferred value is declared. Missing full text is recorded and blocks only
that package's R1/R2 work. A package cannot enter a formal comparison merely
because it compiles.

Paper/source conflicts are registered in
`semantic_discrepancy_ledger.tsv`. When a conflict changes a state transition,
budget, or objective, the paper-defined and source-replay behaviors receive
different semantic identifiers. They are never pooled as one method.

## Current waves

1. **Baseline:** FODE, AGA, SUGGA, ISE, AGPSO, CGPSO, LSHADE, CLSHADE on the
   common 50-case FODE-E0-L problem.
2. **Source-rich lineage:** CEDE, MS-SHADE, RL-assisted FODE, RLPSO, discrete
   bi-population DE, geometry-guided layout/cable optimization, HGPSO, and the
   probabilistic-bootstrap three-objective study.
3. **Remaining direct lineage:** every other registry row, ordered by R0/R1
   readiness rather than publication prestige.
4. **Problem expansion:** irregular terrain, complex wake, 3D, offshore
   topology/cables, and multiobjective contracts behind the same
   algorithm–problem interface.

Formal runs begin only after the package's semantic admission passes. Timing
and solution quality are reported separately.
