#!/usr/bin/env python3
"""Admit the Nysted T-MOEA reconstruction without overstating source identity."""

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
    "nondominated_count",
    "front",
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
            "tmoea",
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


def dominates(left: list[float], right: list[float]) -> bool:
    return all(a <= b for a, b in zip(left, right)) and any(
        a < b for a, b in zip(left, right)
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--problem", type=Path, required=True)
    parser.add_argument("--physical-fes", type=int, default=150)
    parser.add_argument("--seed", type=int, default=20260729)
    arguments = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="tmoea-admission-") as temp:
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
                f"T-MOEA worker-count semantics differ: {differing}"
            )
        if one["algorithm_id"] != "tmoea":
            raise RuntimeError("T-MOEA algorithm identity is missing")
        if (
            one["method_id"]
            != "TMOEA_NYSTED_GGA_ASSET_RECONSTRUCTION_V1"
        ):
            raise RuntimeError("T-MOEA reconstruction identity is missing")
        if one["problem_id"] != "nysted_gga_asset_reconstruction":
            raise RuntimeError("T-MOEA reconstruction boundary is missing")
        if one["case_id"] != "Denmark_Nysted":
            raise RuntimeError("T-MOEA admission must use the Nysted asset")
        if one["physical_fes"] != arguments.physical_fes:
            raise RuntimeError("T-MOEA did not stop at exact physical work")
        if one["population_size"] != 30:
            raise RuntimeError("T-MOEA paper population size is not preserved")
        if one["best_lcoe"] is not None:
            raise RuntimeError("T-MOEA evaluated a foreign LCOE objective")
        front = one["front"]
        if not front or len(front) != one["nondominated_count"]:
            raise RuntimeError("T-MOEA front artifact is empty or incomplete")
        for left_index, left in enumerate(front):
            layout = left["layout_0based"]
            if len(layout) != 72 or len(layout) != len(set(layout)):
                raise RuntimeError("T-MOEA returned an invalid Nysted layout")
            if left["objectives"] != [-left["aep_kwh"], left["cable_cost"]]:
                raise RuntimeError("T-MOEA stored inconsistent objectives")
            for right_index, right in enumerate(front):
                if left_index != right_index and dominates(
                    left["objectives"], right["objectives"]
                ):
                    raise RuntimeError(
                        "T-MOEA artifact contains a dominated front member"
                    )
            replay_path = temp_path / f"replay-{left_index}.json"
            subprocess.run(
                [
                    str(arguments.binary),
                    "--algorithm",
                    "tmoea",
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
            for field in ("best_aep_kwh", "best_cable_cost"):
                expected = (
                    left["aep_kwh"]
                    if field == "best_aep_kwh"
                    else left["cable_cost"]
                )
                if replay[field] != expected:
                    raise RuntimeError(
                        f"T-MOEA front replay differs in {field}"
                    )
    print(
        "tmoea_cpp_admission_pass "
        f"physical_fes={arguments.physical_fes} workers=1,20 "
        "profile=nysted_gga_asset_reconstruction"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
