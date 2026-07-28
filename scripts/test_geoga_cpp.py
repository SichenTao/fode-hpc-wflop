#!/usr/bin/env python3
"""Admit the declared GeoGA reconstruction without overstating Anholt identity."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path


SEMANTIC_FIELDS = [
    "algorithm_id",
    "method_id",
    "problem_id",
    "problem_semantics_id",
    "case_id",
    "seed",
    "physical_fes",
    "generations",
    "population_size",
    "best_lcoe",
    "best_capacity_factor",
    "best_aep_kwh",
    "best_cable_cost",
    "best_layout_0based",
]


def run(
    binary: Path,
    problem: Path,
    workers: int,
    physical_fes: int,
    seed: int,
    output: Path,
) -> dict:
    subprocess.run(
        [
            str(binary),
            "--algorithm",
            "geoga",
            "--problem",
            str(problem),
            "--physical-fes",
            str(physical_fes),
            "--workers",
            str(workers),
            "--seed",
            str(seed),
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
    parser.add_argument("--problem", type=Path, required=True)
    parser.add_argument("--physical-fes", type=int, default=150)
    parser.add_argument("--seed", type=int, default=20260729)
    arguments = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="geoga-admission-") as temp:
        temp_path = Path(temp)
        one = run(
            arguments.binary,
            arguments.problem,
            1,
            arguments.physical_fes,
            arguments.seed,
            temp_path / "one.json",
        )
        twenty = run(
            arguments.binary,
            arguments.problem,
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
                f"GeoGA worker-count semantics differ: {differing}"
            )
        if one["algorithm_id"] != "geoga":
            raise RuntimeError("GeoGA algorithm identity is missing")
        if one["method_id"] != "GEOGA_DECLARED_RECONSTRUCTION_V1":
            raise RuntimeError("GeoGA reconstruction identity is missing")
        if one["problem_id"] != "admitted_gga_problem_asset_proxy":
            raise RuntimeError("GeoGA proxy problem boundary is missing")
        if one["physical_fes"] != arguments.physical_fes:
            raise RuntimeError("GeoGA did not stop at exact physical work")
        if one["population_size"] != 50:
            raise RuntimeError("GeoGA paper population size is not preserved")
        layout = one["best_layout_0based"]
        if len(layout) != len(set(layout)):
            raise RuntimeError("GeoGA returned duplicate candidate indices")
        if one["best_lcoe"] is not None or one["best_cable_cost"] != 0:
            raise RuntimeError("GeoGA AEP-only evaluator computed foreign costs")
        if not (0 < one["best_capacity_factor"] <= 1):
            raise RuntimeError("GeoGA capacity factor is outside physical range")

        replay_path = temp_path / "replay.json"
        subprocess.run(
            [
                str(arguments.binary),
                "--algorithm",
                "geoga",
                "--problem",
                str(arguments.problem),
                "--workers",
                "20",
                "--evaluate-layout",
                ",".join(str(value) for value in layout),
                "--output",
                str(replay_path),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        replay = json.loads(replay_path.read_text(encoding="utf-8"))
        for field in ("best_aep_kwh", "best_capacity_factor"):
            if replay[field] != one[field]:
                raise RuntimeError(
                    f"GeoGA best-layout replay differs in {field}"
                )
    print(
        "geoga_cpp_admission_pass "
        f"physical_fes={arguments.physical_fes} workers=1,20 "
        "profile=declared_reconstruction_not_anholt_reproduction"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
