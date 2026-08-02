#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T07 independent continuous-wake/H5 oracle
Paper DOI: 10.1016/j.apenergy.2015.03.139.
Public source: no paper-linked author code or data found.
Missing: exact Horns Rev coordinates, rotor quadrature, wind-speed bins and
author CVX/SCP files.
Reconstruction: independently evaluate Eqs. (9), (12), (15), (17), and (27)
on the declared Figure-10 geometry; compare bounded C++ SCP runs separately.
Claim boundary: independent equation-level oracle, not author replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile


DIAMETER_M = 126.0
ROTOR_RADIUS_M = 63.0
WAKE_EXPANSION = 0.033
CALIBRATION = 0.9
TURBINES = 80
PUBLISHED = {
    "t07_single_0": 0.828,
    "t07_single_41": 0.583,
    "t07_single_90": 0.432,
}


def layout() -> list[tuple[float, float]]:
    angle = math.radians(97.35)
    basis = 7.0 * DIAMETER_M
    bx, by = basis * math.cos(angle), basis * math.sin(angle)
    return [
        (column * basis + row * bx, row * by)
        for row in range(8)
        for column in range(10)
    ]


def averaged_deficit(
    downstream: float, crosswind: float
) -> float:
    if downstream <= 0.0:
        return 0.0
    radius = ROTOR_RADIUS_M + WAKE_EXPANSION * downstream
    total = 0.0
    for radial in range(4):
        local_radius = ROTOR_RADIUS_M * math.sqrt((radial + 0.5) / 4)
        for azimuth in range(16):
            angle = 2.0 * math.pi * (azimuth + 0.5) / 16
            local_cross = local_radius * math.cos(angle)
            local_vertical = local_radius * math.sin(angle)
            radial2 = (
                (crosswind - local_cross) ** 2 + local_vertical**2
            )
            total += (
                CALIBRATION
                * (2.0 / 3.0)
                * (ROTOR_RADIUS_M / radius) ** 2
                * math.exp(-radial2 / radius**2)
            )
    return total / 64.0


def independent_efficiency(direction_degrees: float) -> float:
    radians = math.radians(direction_degrees)
    flow_x, flow_y = -math.sin(radians), -math.cos(radians)
    cross_x, cross_y = -flow_y, flow_x
    points = layout()
    total = 0.0
    for tx, ty in points:
        squared = 0.0
        for sx, sy in points:
            dx, dy = tx - sx, ty - sy
            deficit = averaged_deficit(
                dx * flow_x + dy * flow_y,
                dx * cross_x + dy * cross_y,
            )
            squared += deficit * deficit
        inflow = max(0.0, 1.0 - math.sqrt(squared))
        total += inflow**3
    return total / TURBINES


def run(
    binary: str,
    case_id: str,
    workers: int,
    directory: Path,
) -> dict:
    output = directory / f"{case_id}-w{workers}.json"
    subprocess.run(
        (
            binary,
            "--case",
            case_id,
            "--workers",
            str(workers),
            "--scp-iterations",
            "2",
            "--qp-evaluations",
            "60",
            "--output",
            str(output),
        ),
        check=True,
        timeout=300,
    )
    return json.loads(output.read_text())["runs"][0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output")
    args = parser.parse_args()
    failures: list[str] = []
    oracle = {
        case_id: independent_efficiency(float(case_id.removeprefix(
            "t07_single_"
        )))
        for case_id in PUBLISHED
    }
    with tempfile.TemporaryDirectory(prefix="t07-h5-") as temporary:
        directory = Path(temporary)
        rows = {
            case_id: run(args.binary, case_id, 4, directory)
            for case_id in PUBLISHED
        }
        for case_id, row in rows.items():
            if not math.isclose(
                row["initial"]["efficiency"],
                oracle[case_id],
                rel_tol=2e-12,
                abs_tol=2e-12,
            ):
                failures.append(f"{case_id}: independent equation oracle")
            if abs(
                row["initial"]["efficiency"] - PUBLISHED[case_id]
            ) > 0.003:
                failures.append(f"{case_id}: published initial scale")
            if (
                row["final"]["efficiency"]
                + 1e-13
                < row["initial"]["efficiency"]
            ):
                failures.append(f"{case_id}: bounded SCP improvement")
            if row["maximum_constraint_violation_m"] > 1e-5:
                failures.append(f"{case_id}: feasibility")
        serial = run(args.binary, "t07_single_0", 1, directory)
        parallel = rows["t07_single_0"]
        if serial["scientific_hash"] != parallel["scientific_hash"]:
            failures.append("one/four-worker scientific hash")
        payload = {
            "schema_version": 1,
            "corpus_id": "T07",
            "doi": "10.1016/j.apenergy.2015.03.139",
            "status": "H5_pass" if not failures else "H5_fail",
            "independent_oracle":
                "separate Python implementation of paper wake/power equations",
            "oracle_initial_efficiency": oracle,
            "bounded_rows": rows,
            "one_four_replay": {
                "same_scientific_hash":
                    serial["scientific_hash"]
                    == parallel["scientific_hash"],
                "end_to_end_speedup":
                    serial["end_to_end_seconds"]
                    / parallel["end_to_end_seconds"],
            },
            "failures": failures,
            "claim_boundary":
                "paper-equation reconstruction with declared geometry, "
                "quadrature, wind-bin and open-QP completions",
        }
        encoded = json.dumps(payload, indent=2, sort_keys=True) + "\n"
        payload["receipt_sha256_without_self"] = hashlib.sha256(
            encoded.encode()
        ).hexdigest()
        encoded = json.dumps(payload, indent=2, sort_keys=True) + "\n"
        if args.output:
            target = Path(args.output)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(encoded)
        print(encoded, end="")
        if failures:
            raise RuntimeError("; ".join(failures))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
