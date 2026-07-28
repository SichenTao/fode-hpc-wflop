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
