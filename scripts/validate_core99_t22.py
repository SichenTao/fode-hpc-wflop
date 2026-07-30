#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T22 equation/data oracle
Paper/DOI: 10.5194/wes-8-865-2023
Source data: shared/contracts/core99_t22_iea37_cs4.json, compiled from the
paper-linked archive at revision 41d7290b8cc9bf3d90b25d844312f4790037806d
Independence boundary: direct Python equations and subprocess JSON only; no
production C++ function is imported
Known source fact: archived YAML AEP defaults need not equal recalculation
after wind-resource/model revisions, so admission compares both C++ and Python
against the same frozen numeric inputs and reports the archived-value gap
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path
from typing import Any


def power(speed: float, turbine: dict[str, float]) -> float:
    if turbine["cut_in_mps"] <= speed < turbine["rated_speed_mps"]:
        fraction = (
            (speed - turbine["cut_in_mps"])
            / (turbine["rated_speed_mps"] - turbine["cut_in_mps"])
        )
        return turbine["rated_power_w"] * fraction**3
    if turbine["rated_speed_mps"] <= speed < turbine["cut_out_mps"]:
        return turbine["rated_power_w"]
    return 0.0


def aep(layout: list[list[float]], contract: dict[str, Any]) -> float:
    wind = contract["wind"]
    turbine = contract["turbine"]
    total_expected_power = 0.0
    for direction_index, direction in enumerate(wind["directions_deg"]):
        radians = -math.radians(270.0 - direction)
        cosine = math.cos(radians)
        sine = math.sin(radians)
        downwind = [x * cosine - y * sine for x, y in layout]
        crosswind = [x * sine + y * cosine for x, y in layout]
        losses = []
        for downstream in range(len(layout)):
            squared_loss = 0.0
            for upstream in range(len(layout)):
                dx = downwind[downstream] - downwind[upstream]
                if dx <= 0.0:
                    continue
                dy = crosswind[downstream] - crosswind[upstream]
                sigma = (
                    turbine["wake_expansion"] * dx
                    + turbine["diameter_m"] / math.sqrt(8.0)
                )
                denominator = (
                    8.0 * sigma * sigma
                    / (turbine["diameter_m"] * turbine["diameter_m"])
                )
                radical = max(
                    0.0,
                    1.0 - turbine["thrust_coefficient"] / denominator,
                )
                deficit = (1.0 - math.sqrt(radical)) * math.exp(
                    -0.5 * (dy / sigma) ** 2
                )
                squared_loss += deficit * deficit
            losses.append(math.sqrt(squared_loss))
        direction_power = 0.0
        for speed_index, free_speed in enumerate(wind["speeds_mps"]):
            farm_power = sum(
                power(free_speed * (1.0 - loss), turbine)
                for loss in losses
            )
            direction_power += (
                farm_power
                * wind["conditional_speed_frequency"][direction_index][
                    speed_index
                ]
            )
        total_expected_power += (
            direction_power
            * wind["direction_frequency"][direction_index]
        )
    return total_expected_power * 365.0 * 24.0 / 1.0e6


def cpp(binary: Path, label: str, workers: int) -> dict[str, Any]:
    output = subprocess.check_output(
        [
            str(binary),
            "--author-layout",
            label,
            "--workers",
            str(workers),
        ],
        text=True,
    )
    return json.loads(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--contract", type=Path, required=True)
    args = parser.parse_args()
    contract = json.loads(args.contract.read_text(encoding="utf-8"))
    report: dict[str, Any] = {
        "status": "pass",
        "problem_semantic_id": contract["problem_semantic_id"],
        "layouts": {},
    }
    values: dict[str, float] = {}
    for label in ("base", "debo"):
        receipt = contract["author_receipts"][label]
        oracle = aep(receipt["positions_m"], contract)
        serial = cpp(args.binary, label, 1)
        parallel = cpp(args.binary, label, 4)
        absolute_error = abs(serial["aep_mwh"] - oracle)
        parallel_error = abs(parallel["aep_mwh"] - serial["aep_mwh"])
        tolerance = max(1.0e-6, 1.0e-11 * abs(oracle))
        if absolute_error > tolerance or parallel_error > tolerance:
            report["status"] = "fail"
        # The published baseline coordinates are rounded to four decimals and
        # sit 1.1306 m outside the detailed polygons in aggregate.  They are an
        # evaluator oracle, not a DEBO feasibility oracle.  The published DEBO
        # layout must remain feasible under the exact paper constraint.
        violation_tolerance = 2.0 if label == "base" else 1.0e-4
        if serial["constraint_violation_m"] > violation_tolerance:
            report["status"] = "fail"
        values[label] = oracle
        report["layouts"][label] = {
            "python_oracle_aep_mwh": oracle,
            "cpp_serial_aep_mwh": serial["aep_mwh"],
            "cpp_parallel_aep_mwh": parallel["aep_mwh"],
            "cpp_python_absolute_error_mwh": absolute_error,
            "parallel_absolute_error_mwh": parallel_error,
            "archive_yaml_default_aep_mwh": receipt["aep_mwh"],
            "archive_default_gap_mwh": oracle - receipt["aep_mwh"],
            "constraint_violation_m": serial["constraint_violation_m"],
            "constraint_violation_tolerance_m": violation_tolerance,
        }
    if values["debo"] <= values["base"]:
        report["status"] = "fail"
    print(json.dumps(report, indent=2, sort_keys=True))
    if report["status"] != "pass":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
