# Third-party provenance

This repository is a clean-room implementation and does not bundle publisher
PDFs or unlicensed upstream MATLAB/Python archives.

Algorithm identities, parameter choices, and source/paper conflicts are
recorded in:

- `shared/contracts/algorithm_provenance.tsv`;
- `shared/contracts/paper_implementation_ledger.tsv`; and
- `shared/contracts/uncertainty_decisions.md`.

The build links against the system C++ standard library, POSIX threads, and an
OpenMP runtime supplied by the compiler/toolchain. Those components retain
their own licenses.

The repository's Apache-2.0 license applies only to repository code. It does
not relicense research papers, names, citations, or third-party software.

## Optional T19 SRMP target

`CORE99_ENABLE_T19=ON` downloads the paper-linked SRMP v1.01 archive from
`https://pub.ist.ac.at/~vnk/software/SRMP-v1.01.zip`, verifies SHA-256
`e2bf3376f5d4b68c3ed9c88ef37ede187455f907ee0ee71b8572ae04f4be6848`,
and applies one modern-C++ safety patch: `FactorType` receives an empty
virtual destructor because upstream `Energy` deletes derived factor objects
through the base pointer. ASAN otherwise reports new-delete-type-mismatch;
the patch changes no solver arithmetic or data layout. SRMP is copyright
Vladimir Kolmogorov and licensed under GPL-3.0-or-later. Consequently, the linked
`core99_t19_*` optional targets are GPL-3.0-or-later. The rest of this
aggregate remains under its existing licenses and is not relicensed by that
optional build. The unavailable author-modified GEMPLP triplet generator is
not bundled or represented as public source. SRMP v1.01's shared-pairwise
class exposes its historical two-argument initialization overload despite a
three-argument base signature; project code supplies a forwarding subclass
for modern compiler compatibility without changing upstream solver code.
The shared factor's upstream MPLP-message method is intentionally
unimplemented, so T19 uses it only for the paper's no-triplet 2,500-cell
roles; triplet roles retain source-native general pair factors. T19 selects
the source-supported fixed insertion order (`sort_flag=-1`) because the
author wrapper/order is unavailable and v1.01's equal-key factor quicksort
does not terminate safely for the dense triplet graph on the target modern
AArch64 toolchain.
