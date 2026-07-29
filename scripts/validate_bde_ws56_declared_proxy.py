#!/usr/bin/env python3
"""Validate BDE WS5/WS6 P3 C++ against the independent Park/Jensen oracle."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

from validate_bde_source_replay import evaluate, turbine_power


SCIENTIFIC_FIELDS = (
    "method_semantic_id",
    "execution_profile_id",
    "problem_semantic_id",
    "case_id",
    "objective_semantics_hash",
    "feasible_set_hash",
    "seed",
    "physical_fes",
    "generations",
    "schedule_imax",
    "population_size",
    "work_receipt",
    "best_expected_power_kw",
    "no_wake_expected_power_kw",
    "conversion_efficiency_percent",
    "best_layout_1based",
    "best_layout_hash",
    "population_layout_hash",
)


def run(
    binary: Path,
    cases: Path,
    case_id: str,
    workers: int,
    physical_fes: int,
) -> dict:
    completed = subprocess.run(
        [
            str(binary),
            "--cases",
            str(cases),
            "--case",
            case_id,
            "--seed",
            "20260729",
            "--physical-fes",
            str(physical_fes),
            "--workers",
            str(workers),
            "--execution-mode",
            "auto",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--cases", type=Path, required=True)
    parser.add_argument("--physical-fes", type=int, default=75)
    parser.add_argument("--all-workers", type=int, default=0)
    arguments = parser.parse_args()

    manifest = json.loads(arguments.cases.read_text(encoding="utf-8"))
    cases = {case["case_id"]: case for case in manifest["cases"]}
    for case_id in ("BDEWS5P3DAEtn30", "BDEWS6P3STDtn30"):
        serial = run(
            arguments.binary,
            arguments.cases,
            case_id,
            1,
            arguments.physical_fes,
        )
        parallel = run(
            arguments.binary,
            arguments.cases,
            case_id,
            arguments.all_workers,
            arguments.physical_fes,
        )
        differing = [
            field
            for field in SCIENTIFIC_FIELDS
            if serial[field] != parallel[field]
        ]
        if differing:
            raise RuntimeError(f"{case_id}: 1/all mismatch {differing}")
        oracle = evaluate(
            cases[case_id],
            [int(cell) for cell in serial["best_layout_1based"]],
        )
        cpp = float(serial["best_expected_power_kw"])
        if abs(cpp - oracle) > 1.0e-10 * max(1.0, abs(oracle)):
            raise RuntimeError(
                f"{case_id}: C++ {cpp} differs from oracle {oracle}"
            )
        case = cases[case_id]
        probabilities = case["joint_probabilities"]
        speeds = case["wind_speeds_mps"]
        no_wake_per_turbine = sum(
            turbine_power(float(speed)) * float(probabilities[d][s])
            for d in range(len(probabilities))
            for s, speed in enumerate(speeds)
        )
        no_wake = int(case["turbine_count"]) * no_wake_per_turbine
        if abs(float(serial["no_wake_expected_power_kw"]) - no_wake) > (
            1.0e-12 * no_wake
        ):
            raise RuntimeError(f"{case_id}: no-wake oracle differs")
        efficiency = 100.0 * oracle / no_wake
        if abs(
            float(serial["conversion_efficiency_percent"]) - efficiency
        ) > 1.0e-10:
            raise RuntimeError(
                f"{case_id}: conversion-efficiency oracle differs"
            )
        if serial["physical_fes"] != arguments.physical_fes:
            raise RuntimeError(f"{case_id}: non-exact physical FES")
        if serial["work_receipt"]["complete_layout_evaluations"] != (
            arguments.physical_fes
        ):
            raise RuntimeError(f"{case_id}: work receipt differs from FES")
    print(
        "bde_ws56_declared_proxy_validation_pass "
        f"cases=2 workers=1,all physical_fes={arguments.physical_fes}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
