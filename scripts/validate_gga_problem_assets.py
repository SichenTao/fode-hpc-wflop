#!/usr/bin/env python3
"""Validate generated GGA problem snapshots without running an optimizer."""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path


EXPECTED_CASES = {
    "China_Zhuhai_Guishan_Hai",
    "Netherlands_Egmond_aan_Zee",
    "China_Shanghai_Lingang",
    "Netherlands_Prinses_Amaliawindpark",
    "Denmark_Nysted",
    "UK_Sheringham_Shoal",
    "Denmark_Rodsand_II",
    "UK_London_Array",
}


def read_scalar(lines: list[list[str]], label: str) -> str:
    rows = [row for row in lines if row and row[0] == label]
    if len(rows) != 1 or len(rows[0]) != 2:
        raise RuntimeError(f"expected one scalar {label}")
    return rows[0][1]


def read_vector(lines: list[list[str]], label: str) -> list[float]:
    rows = [row for row in lines if row and row[0] == label]
    if len(rows) != 1 or len(rows[0]) < 2:
        raise RuntimeError(f"expected one vector {label}")
    count = int(rows[0][1])
    values = [float(value) for value in rows[0][2:]]
    if len(values) != count:
        raise RuntimeError(f"wrong cardinality for {label}")
    return values


def inside_polygon(
    point: tuple[float, float],
    polygon: list[tuple[float, float]],
) -> bool:
    x, y = point
    inside = False
    previous = polygon[-1]
    for current in polygon:
        x1, y1 = previous
        x2, y2 = current
        crosses = (y1 > y) != (y2 > y)
        if crosses:
            crossing_x = (x2 - x1) * (y - y1) / (y2 - y1) + x1
            if x < crossing_x:
                inside = not inside
        previous = current
    return inside


def validate_snapshot(path: Path, record: dict) -> dict:
    raw = path.read_bytes()
    lines = [
        line.split()
        for line in raw.decode("utf-8").splitlines()
        if line.strip()
    ]
    if lines[0] != ["GGA_CASE_V1"]:
        raise RuntimeError(f"invalid header: {path}")
    case = read_scalar(lines, "case")
    if case != record["case"] or path.stem != case:
        raise RuntimeError(f"case identity mismatch: {path}")
    if read_scalar(lines, "profile") != "geojson_radians_ct_rss_repaired_v1":
        raise RuntimeError(f"wrong semantic profile: {path}")

    turbines = int(read_scalar(lines, "turbine_count"))
    candidates = [
        (float(row[1]), float(row[2]))
        for row in lines
        if row[0] == "candidate"
    ]
    boundary = [
        (float(row[1]), float(row[2]))
        for row in lines
        if row[0] == "boundary"
    ]
    declared_candidates = int(read_scalar(lines, "candidate_count"))
    spacing = float(read_scalar(lines, "minimum_spacing_m"))
    if turbines != record["turbines"]:
        raise RuntimeError(f"turbine count mismatch: {path}")
    if len(candidates) != declared_candidates or len(candidates) < turbines:
        raise RuntimeError(f"candidate count invalid: {path}")
    if len(boundary) < 3:
        raise RuntimeError(f"boundary invalid: {path}")
    if not all(inside_polygon(point, boundary) for point in candidates):
        raise RuntimeError(f"candidate outside GeoJSON boundary: {path}")

    minimum_pair_distance = min(
        math.dist(left, right)
        for left, right in itertools.combinations(candidates, 2)
    )
    if minimum_pair_distance + 1e-8 < spacing:
        raise RuntimeError(f"candidate spacing violated: {path}")

    theta = read_vector(lines, "theta_radians")
    velocity = read_vector(lines, "velocity_mps")
    probability = read_vector(lines, "joint_probability_dir_major")
    if not theta or min(theta) < 0.0 or max(theta) >= 2.0 * math.pi:
        raise RuntimeError(f"wind direction is not canonical radians: {path}")
    if len(probability) != len(theta) * len(velocity):
        raise RuntimeError(f"joint probability cardinality mismatch: {path}")
    if min(probability) < 0.0:
        raise RuntimeError(f"negative joint probability: {path}")
    if not math.isclose(sum(probability), 1.0, rel_tol=0.0, abs_tol=1e-10):
        raise RuntimeError(f"joint probability does not sum to one: {path}")

    capacity = [
        round(value)
        for value in read_vector(lines, "inner_cable_capacity")
    ]
    if capacity != [5, 8, 11]:
        raise RuntimeError(f"wrong cable capacity: {path}")
    digest = hashlib.sha256(raw).hexdigest()
    if digest != record["snapshot_sha256"]:
        raise RuntimeError(f"snapshot hash mismatch: {path}")
    return {
        "case": case,
        "turbines": turbines,
        "candidates": len(candidates),
        "minimum_spacing_m": spacing,
        "observed_minimum_pair_distance_m": minimum_pair_distance,
        "probability_sum": sum(probability),
        "inner_cable_capacity": capacity,
        "snapshot_sha256": digest,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--receipt", type=Path)
    arguments = parser.parse_args()
    manifest = json.loads(
        (arguments.assets / "manifest.json").read_text(encoding="utf-8")
    )
    records = manifest["cases"]
    observed_cases = {record["case"] for record in records}
    if observed_cases != EXPECTED_CASES:
        raise RuntimeError("GGA manifest does not contain the frozen eight cases")
    validated = [
        validate_snapshot(arguments.assets / f"{record['case']}.wfp", record)
        for record in records
    ]
    receipt = {
        "schema_version": 1,
        "status": "pass",
        "semantic_profile": manifest["profile"],
        "source_commit": manifest["source_commit"],
        "case_count": len(validated),
        "cases": validated,
    }
    if arguments.receipt:
        arguments.receipt.parent.mkdir(parents=True, exist_ok=True)
        arguments.receipt.write_text(
            json.dumps(receipt, indent=2) + "\n",
            encoding="utf-8",
        )
    print(
        "gga_asset_validation_pass "
        f"cases={len(validated)} profile={manifest['profile']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
