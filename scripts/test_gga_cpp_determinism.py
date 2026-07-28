#!/usr/bin/env python3
"""Verify that GGA C++ semantics do not depend on worker count."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path


SEMANTIC_FIELDS = [
    "problem_semantics_id",
    "case_id",
    "seed",
    "physical_fes",
    "generations",
    "best_lcoe",
    "best_capacity_factor",
    "best_aep_kwh",
    "best_cable_cost",
    "best_layout_0based",
]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--problem", type=Path, required=True)
    parser.add_argument("--physical-fes", type=int, default=300)
    parser.add_argument("--seed", type=int, default=20260316)
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="gga-determinism-") as temp:
        outputs = {}
        for workers in (1, 20):
            output = Path(temp) / f"workers_{workers}.json"
            subprocess.run(
                [
                    str(arguments.binary),
                    "--problem",
                    str(arguments.problem),
                    "--physical-fes",
                    str(arguments.physical_fes),
                    "--workers",
                    str(workers),
                    "--seed",
                    str(arguments.seed),
                    "--output",
                    str(output),
                ],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            outputs[workers] = json.loads(output.read_text(encoding="utf-8"))
    differing = [
        field
        for field in SEMANTIC_FIELDS
        if outputs[1][field] != outputs[20][field]
    ]
    if differing:
        raise RuntimeError(
            f"GGA worker-count semantics differ in fields: {differing}"
        )
    print(
        "gga_cpp_determinism_pass "
        f"physical_fes={arguments.physical_fes} workers=1,20"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
