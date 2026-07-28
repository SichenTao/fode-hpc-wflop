#!/usr/bin/env python3
"""Admit the declared PPGA transfer without claiming the missing 3D study."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path


SEMANTIC_FIELDS = [
    "algorithm_id",
    "method_id",
    "algorithm_provenance",
    "effective_semantics_id",
    "problem_id",
    "problem_semantics_id",
    "case_id",
    "seed",
    "physical_fes",
    "generations",
    "initial_population",
    "final_population",
    "best_expected_power_kw",
    "best_layout_1based",
]


def run(
    binary: Path,
    cases: Path,
    case_id: str,
    workers: int,
    physical_fes: int,
    seed: int,
    output: Path,
) -> dict:
    subprocess.run(
        [
            str(binary),
            "--algorithm",
            "ppga",
            "--case",
            case_id,
            "--physical-fes",
            str(physical_fes),
            "--workers",
            str(workers),
            "--seed",
            str(seed),
            "--cases",
            str(cases),
            "--output",
            str(output),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return json.loads(output.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--cases", type=Path, required=True)
    parser.add_argument("--case", default="WS5tn30")
    parser.add_argument("--physical-fes", type=int, default=150)
    parser.add_argument("--seed", type=int, default=20260729)
    arguments = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="ppga-admission-") as temp:
        temp_path = Path(temp)
        one = run(
            arguments.binary,
            arguments.cases,
            arguments.case,
            1,
            arguments.physical_fes,
            arguments.seed,
            temp_path / "one.json",
        )
        twenty = run(
            arguments.binary,
            arguments.cases,
            arguments.case,
            20,
            arguments.physical_fes,
            arguments.seed,
            temp_path / "twenty.json",
        )
        differing = [
            field for field in SEMANTIC_FIELDS if one[field] != twenty[field]
        ]
        if differing:
            raise RuntimeError(
                f"PPGA worker-count semantics differ: {differing}"
            )
        if one["method_id"] != "PPGA_DECLARED_RECONSTRUCTION_FODE_E0_V1":
            raise RuntimeError("PPGA reconstruction identity is missing")
        if one["problem_id"] != "fode_e0_common":
            raise RuntimeError("PPGA transfer problem identity is missing")
        if one["physical_fes"] != arguments.physical_fes:
            raise RuntimeError("PPGA did not stop at exact physical work")
        if one["initial_population"] != 30 or one["final_population"] != 30:
            raise RuntimeError("PPGA population size differs from the paper")
        layout = one["best_layout_1based"]
        if layout != sorted(layout) or len(layout) != len(set(layout)):
            raise RuntimeError("PPGA returned an infeasible layout encoding")
        if not one["best_expected_power_kw"] > 0:
            raise RuntimeError("PPGA returned a nonphysical best objective")
    print(
        "ppga_cpp_admission_pass "
        f"physical_fes={arguments.physical_fes} workers=1,20 "
        "profile=declared_fode_e0_transfer_not_nantong_reproduction"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
