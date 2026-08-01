#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T26 bounded H5 source, training, evaluator and FES gate
Paper/DOI: Li et al.; 10.1016/j.apenergy.2025.125908.
Public source provenance, Missing facts, Reconstruction, semantic IDs,
production backend, controlling Contract and Claim boundary:
hpc/t26_libtorch/include/core99/li_t26.hpp.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess
import tempfile


def run(binary: str, arguments: list[str], timeout: float = 600.0) -> dict:
    completed = subprocess.run(
        [binary, *arguments], text=True, capture_output=True, timeout=timeout
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr or completed.stdout)
    return json.loads(completed.stdout)


def validate_optimization(payload: dict, population: int, generations: int) -> None:
    if payload["corpus_id"] != "T26":
        raise RuntimeError("T26 corpus receipt differs")
    if payload["physical_fes"] != population * (generations + 1):
        raise RuntimeError("T26 complete-layout physical FES differs")
    if abs(payload["initial_aep_gwh"] - 1554.20) > 0.02:
        raise RuntimeError("T26 regular-layout AEP anchor differs")
    if len(payload["layout_xy_m"]) != 160:
        raise RuntimeError("T26 80 two-component genes differ")
    for field in ("final_aep_gwh", "final_fitness", "seconds"):
        if not math.isfinite(payload[field]):
            raise RuntimeError(f"T26 invalid {field}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="core99-t26-h5-") as directory:
        artifact = Path(directory) / "pidnn.pt"
        trained = run(
            args.binary,
            ["--action", "train", "--backend", "cpu", "--artifact", str(artifact),
             "--iterations", "3", "--batch-size", "64", "--workers", "2",
             "--seed", "26001", "--smoke"],
        )
        if trained["iterations"] != 3 or not artifact.exists():
            raise RuntimeError("T26 bounded training lifecycle differs")
        if trained["table_direct_mae"] >= 0.02:
            raise RuntimeError("T26 PIDNN table interpolation gate failed")
        common = [
            "--action", "optimize", "--backend", "cpu", "--artifact", str(artifact),
            "--generations", "3", "--population", "16", "--seed", "26001",
        ]
        one = run(args.binary, [*common, "--workers", "1"])
        all_core = run(args.binary, [*common, "--workers", "4"])
        validate_optimization(one, 16, 3)
        validate_optimization(all_core, 16, 3)
        for field in ("initial_aep_gwh", "final_aep_gwh", "final_fitness",
                      "minimum_spacing_m", "maximum_spacing_violation_m"):
            if abs(one[field] - all_core[field]) > 1.0e-5:
                raise RuntimeError(f"T26 worker-dependent science in {field}")
    print("core99_t26_h5_pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
