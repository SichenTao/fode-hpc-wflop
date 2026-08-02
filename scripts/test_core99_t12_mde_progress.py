#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T12 3s-MDE deterministic index-rejection regression.
Paper/DOI: Wilson et al.; 10.1016/j.renene.2018.03.052.
Public source: https://github.com/d9w/WindFLO revision
9e85a67bb2ca019768ea51dd0b634a46c8406ba2, MIT license.
Conflict and reconstruction: the academic reproduction uses counter-keyed
random draws.  Replaying one key after an excluded-index draw repeats that
index forever, so every rejection advances a deterministic retry key and a
finite ordered fallback guarantees termination.
Purpose: enter the first paper-scale MDE generation, prove forward progress,
and verify schedule-independent science between 4 and 20 workers.
Claim boundary: regression and worker-equivalence evidence; no quality claim.
Last evidence-audit date: 2026-08-02
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory


def run(binary: Path, output: Path, workers: int) -> dict[str, object]:
    subprocess.run(
        [
            str(binary),
            "--scenario", "1",
            "--algorithm", "t12_3s_mde",
            "--seed", "20260731",
            "--physical-fes-limit", "1520",
            "--workers", str(workers),
            "--output", str(output),
        ],
        check=True,
        timeout=30,
    )
    return json.loads(output.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()
    with TemporaryDirectory(prefix="core99-t12-mde-progress-") as directory:
        root = Path(directory)
        four = run(binary, root / "workers-04.json", 4)
        twenty = run(binary, root / "workers-20.json", 20)

    for label, record in (("workers-04", four), ("workers-20", twenty)):
        if record["physical_fes"] != 1520:
            raise RuntimeError(f"{label} did not complete the fixed work")
        if record["best_constraint_violation_m"] > 1.0e-8:
            raise RuntimeError(f"{label} returned an infeasible result")
        if record["observed_workers"] <= 1:
            raise RuntimeError(f"{label} did not exercise parallel work")
    if four["scientific_hash"] != twenty["scientific_hash"]:
        raise RuntimeError("T12 MDE worker count changed the scientific result")
    print(json.dumps({
        "status": "pass",
        "physical_fes": twenty["physical_fes"],
        "scientific_hash": twenty["scientific_hash"],
        "workers": [four["observed_workers"], twenty["observed_workers"]],
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
