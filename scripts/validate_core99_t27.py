#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent official-FLORIS fixture validator for T27
Paper DOI: 10.1145/3711896.3737181
Public sources: dbsxodud-11/layopt at 19ff389; FLORIS 4.1.1 at 2c3be8f.
Missing/conflicts/resolution/HPC/claim boundary:
hpc/t27_libtorch/include/core99/shin_t27.hpp.
Independence boundary: consumes the official FLORIS fixture but is not called
by the production evaluator, model, training, sampling, or timing path.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--fixture", required=True)
    args = parser.parse_args()

    official = json.loads(Path(args.fixture).read_text(encoding="utf-8"))
    completed = subprocess.run(
        (
            args.binary,
            "--mode",
            "evaluator-fixture",
            "--backend",
            "cpu",
            "--workers",
            "4",
        ),
        text=True,
        capture_output=True,
        timeout=120,
    )
    require(completed.returncode == 0, completed.stderr or completed.stdout)
    native = json.loads(completed.stdout)
    official_by_name = {row["name"]: row for row in official["cases"]}
    errors: dict[str, float] = {}
    for row in native["cases"]:
        reference = official_by_name[row["name"]]
        relative = abs(row["farm_power_w"] - reference["farm_power_w"]) / abs(
            reference["farm_power_w"]
        )
        errors[row["name"]] = relative
        require(relative <= 0.005, f"T27 {row['name']} error {relative}")
        require(
            len(row["turbine_power_w"]) == len(reference["turbine_power_w"]),
            f"T27 {row['name']} turbine count",
        )
    require(
        set(errors) == set(official_by_name),
        "T27 fixture case coverage mismatch",
    )
    print(
        "core99_t27_h5_pass "
        f"max_farm_power_relative_error={max(errors.values()):.9g} "
        f"cases={len(errors)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
