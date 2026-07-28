# Contributing

Contributions are welcome when they preserve scientific identity and evidence
boundaries.

Every new paper package must register and pass R0 through R4:

- R0: bibliographic identity, source identity, license, and hashes;
- R1: objective, constraints, physical model, sampling, and FES semantics;
- R2: algorithm equations, parameters, update order, and random events;
- R3: bounded source/paper reproduction and discrepancy registration;
- R4: pure C++ implementation, semantic tests, scaling evidence, and a frozen
  formal contract.

Do not commit publisher PDFs, credentials, machine-specific absolute paths,
unlicensed upstream source, or unreviewed raw formal results. A missing detail
must be recorded as an uncertainty decision. A performance claim must state
its baseline, workload, machine, compiler, worker count, and semantic gate.

Before opening a pull request:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
bash scripts/public_audit.sh
```
