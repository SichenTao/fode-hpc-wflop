#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T30 semantic and worker-equivalence validator
Paper DOI: 10.1007/s10732-015-9283-4
Public source: none found; open author thesis 20.500.12608/17839.
Missing/conflicts/reconstruction/HPC/claim boundary:
include/core99/fischetti_t30.hpp and the frozen contract.
Independence boundary: the unavailable author instances and private wind
clusters prevent published-number identity. This validator checks executable
mathematical invariants, fixed-work worker identity and both MIP stages.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def invoke(binary: str, workers: int, cache: Path) -> dict:
    completed = subprocess.run(
        [
            binary,
            "--sites", "1000",
            "--instance", "0",
            "--workers", str(workers),
            "--time-limit", "2",
            "--fixed-moves", "40",
            "--seed", "30",
            "--matrix-cache", str(cache),
        ],
        text=True,
        capture_output=True,
        timeout=120,
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr or completed.stdout)
    return json.loads(completed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="core99-t30-h5-") as directory:
        root = Path(directory)
        one = invoke(args.binary, 1, root / "one.bin")
        all_core = invoke(args.binary, 20, root / "all.bin")
    exact_fields = (
        "initial_objective_mw",
        "best_objective_mw",
        "turbines",
        "selected",
        "scientific_hash",
    )
    for field in exact_fields:
        if one[field] != all_core[field]:
            raise RuntimeError(f"T30 worker-count mismatch in {field}")
    if one["minimum_spacing_m"] < 400.0:
        raise RuntimeError("T30 minimum-spacing invariant failed")
    required_stages = ("simplified:", "complete:")
    if not all(stage in one["mip_status"] for stage in required_stages):
        raise RuntimeError("T30 two-stage proximity search was not exercised")
    print(
        "core99_t30_h5_pass "
        f"objective_mw={one['best_objective_mw']:.12g} "
        f"minimum_spacing_m={one['minimum_spacing_m']:.12g} "
        f"scientific_hash={one['scientific_hash']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
