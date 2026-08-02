#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T15 IEA37 common-evaluator H5 oracle
Paper DOI: 10.2514/6.2019-0540
Public source: https://github.com/byuflowlab/iea37-wflo-casestudies
revision af88908d22795030ac2dfbe37bc38e912aee8ed6
Missing/conflicts and reconstruction: hpc/core99_cpp/include/core99/iea37_t15.hpp
Method/problem semantic IDs: t15_iea37_comparison_protocol_v1;
t15_iea37_cs1_three_farms_cs2_cross_model_v1
Controlling contract: shared/contracts/core99_t15_iea37_2019.json
Independence boundary: direct NumPy transcription of the released Python
equations and generated factual YAML inputs; no production C++ is imported
Claim boundary: common evaluator/ranking oracle, not participant methods
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

import numpy as np


def evaluate(record: dict[str, Any], wind: dict[str, Any]) -> float:
    coordinates = np.asarray(record["coordinates"])
    total = 0.0
    for direction, frequency in zip(
        wind["directions_deg"],
        wind["frequencies"],
    ):
        frame = math.radians(270.0 - direction)
        along = math.cos(frame) * coordinates[:, 0] + math.sin(frame) * coordinates[:, 1]
        across = -math.sin(frame) * coordinates[:, 0] + math.cos(frame) * coordinates[:, 1]
        dx = along[:, None] - along[None, :]
        dy = across[:, None] - across[None, :]
        mask = dx > 0.0
        sigma = 0.0324555 * dx + 130.0 / math.sqrt(8.0)
        radical = 1.0 - (8.0 / 9.0) / (8.0 * sigma**2 / 130.0**2)
        loss = np.zeros_like(dx)
        loss[mask] = (
            (1.0 - np.sqrt(radical[mask]))
            * np.exp(-0.5 * (dy[mask] / sigma[mask]) ** 2)
        )
        effective = wind["speed_mps"] * (
            1.0 - np.sqrt(np.sum(loss**2, axis=1))
        )
        power = np.where(
            (effective >= 4.0) & (effective < 9.8),
            3350000.0 * ((effective - 4.0) / 5.8) ** 3,
            np.where((effective >= 9.8) & (effective < 25.0), 3350000.0, 0.0),
        )
        total += 8760.0 * frequency * float(np.sum(power)) / 1.0e6
    return total


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--contract", type=Path, required=True)
    parser.add_argument("--resource-data", type=Path, required=True)
    arguments = parser.parse_args()
    contract = json.loads(arguments.contract.read_text(encoding="utf-8"))
    data = json.loads(arguments.resource_data.read_text(encoding="utf-8"))
    cpp = json.loads(subprocess.check_output(
        [str(arguments.binary), "--workers", "4"],
        text=True,
    ))
    rows = {row["id"]: row for row in cpp["rows"]}
    maximum_error = 0.0
    archived_error = 0.0
    for record in data["records"]:
        value = evaluate(record, data["wind"])
        maximum_error = max(
            maximum_error,
            abs(value - rows[record["id"]]["recalculated_aep_mwh"]),
        )
        archived_error = max(
            archived_error,
            abs(value - record["expected_aep_mwh"]),
        )
    tolerance = 1.0e-5
    report = {
        "status": (
            "pass"
            if maximum_error <= tolerance and archived_error <= tolerance
            else "fail"
        ),
        "method_semantic_id": contract["method_semantic_id"],
        "problem_semantic_id": contract["problem_semantic_id"],
        "records": len(data["records"]),
        "maximum_cpp_python_absolute_error_mwh": maximum_error,
        "maximum_python_archive_absolute_error_mwh": archived_error,
        "tolerance_mwh": tolerance,
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    if report["status"] != "pass":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
