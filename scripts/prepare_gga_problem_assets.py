#!/usr/bin/env python3
"""Build deterministic local text snapshots for the repaired GGA problem.

The official repository remains the authority and is not redistributed here.
Generated snapshots stay under .source-cache and are consumed by the pure C++
runtime. The repaired profile uses GeoJSON boundaries, radians, the supplied
Ct curve, and a per-site deterministic Poisson stream.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path

from scipy.io import loadmat


CASES = [
    "China_Zhuhai_Guishan_Hai",
    "Netherlands_Egmond_aan_Zee",
    "China_Shanghai_Lingang",
    "Netherlands_Prinses_Amaliawindpark",
    "Denmark_Nysted",
    "UK_Sheringham_Shoal",
    "Denmark_Rodsand_II",
    "UK_London_Array",
]


class SplitMix64:
    def __init__(self, seed: int) -> None:
        self.state = seed & ((1 << 64) - 1)

    def uniform(self) -> float:
        self.state = (self.state + 0x9E3779B97F4A7C15) & ((1 << 64) - 1)
        value = self.state
        value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & (
            (1 << 64) - 1
        )
        value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & (
            (1 << 64) - 1
        )
        value ^= value >> 31
        return ((value >> 11) & ((1 << 53) - 1)) / float(1 << 53)

    def integer(self, stop: int) -> int:
        if stop <= 0:
            raise ValueError("empty integer range")
        return min(stop - 1, int(self.uniform() * stop))


def xy_from_lon_lat(
    lon: float, lat: float, lon0: float, lat0: float
) -> tuple[float, float]:
    radius_m = 6_371_000.0
    x = math.radians(lon - lon0) * radius_m * math.cos(math.radians(lat0))
    y = math.radians(lat - lat0) * radius_m
    return x, y


def inside_polygon(
    point: tuple[float, float], polygon: list[tuple[float, float]]
) -> bool:
    x, y = point
    inside = False
    previous = polygon[-1]
    for current in polygon:
        x1, y1 = previous
        x2, y2 = current
        crosses = (y1 > y) != (y2 > y)
        if crosses:
            x_cross = (x2 - x1) * (y - y1) / (y2 - y1) + x1
            if x < x_cross:
                inside = not inside
        previous = current
    return inside


def poisson(
    polygon: list[tuple[float, float]],
    spacing: float,
    rng: SplitMix64,
    attempts: int = 30,
) -> list[tuple[float, float]]:
    minimum_x = min(x for x, _ in polygon)
    maximum_x = max(x for x, _ in polygon)
    minimum_y = min(y for _, y in polygon)
    maximum_y = max(y for _, y in polygon)
    width = spacing / math.sqrt(2.0)
    columns = max(1, math.ceil((maximum_x - minimum_x) / width))
    rows = max(1, math.ceil((maximum_y - minimum_y) / width))
    grid = [-1] * (rows * columns)

    def slot(point: tuple[float, float]) -> tuple[int, int]:
        column = min(
            columns - 1, max(0, int((point[0] - minimum_x) / width))
        )
        row = min(rows - 1, max(0, int((point[1] - minimum_y) / width)))
        return row, column

    points: list[tuple[float, float]] = []
    for _ in range(1_000_000):
        point = (
            minimum_x + rng.uniform() * (maximum_x - minimum_x),
            minimum_y + rng.uniform() * (maximum_y - minimum_y),
        )
        if inside_polygon(point, polygon):
            points.append(point)
            row, column = slot(point)
            grid[row * columns + column] = 0
            break
    if not points:
        raise RuntimeError("could not seed Poisson sampler inside polygon")

    active = [0]
    while active:
        active_position = rng.integer(len(active))
        base_index = active[active_position]
        base = points[base_index]
        found = False
        for _ in range(attempts):
            angle = 2.0 * math.pi * rng.uniform()
            magnitude = spacing * (1.0 + rng.uniform())
            point = (
                base[0] + magnitude * math.cos(angle),
                base[1] + magnitude * math.sin(angle),
            )
            if not (
                minimum_x <= point[0] <= maximum_x
                and minimum_y <= point[1] <= maximum_y
                and inside_polygon(point, polygon)
            ):
                continue
            row, column = slot(point)
            valid = True
            for delta_row in range(-2, 3):
                for delta_column in range(-2, 3):
                    other_row = row + delta_row
                    other_column = column + delta_column
                    if not (
                        0 <= other_row < rows
                        and 0 <= other_column < columns
                    ):
                        continue
                    other = grid[other_row * columns + other_column]
                    if other >= 0 and math.dist(points[other], point) < spacing:
                        valid = False
                        break
                if not valid:
                    break
            if valid:
                points.append(point)
                active.append(len(points) - 1)
                grid[row * columns + column] = len(points) - 1
                found = True
                break
        if not found:
            active.pop(active_position)
    return points


def read_layout(path: Path) -> list[tuple[float, float]]:
    with path.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    lat0 = float(rows[0]["centr_lat"])
    lon0 = float(rows[0]["centr_lon"])
    return [
        xy_from_lon_lat(
            float(row["centr_lon"]),
            float(row["centr_lat"]),
            lon0,
            lat0,
        )
        for row in rows
    ]


def read_boundary(
    path: Path, layout_path: Path
) -> list[tuple[float, float]]:
    with layout_path.open(encoding="utf-8", newline="") as handle:
        first = next(csv.DictReader(handle))
    lat0 = float(first["centr_lat"])
    lon0 = float(first["centr_lon"])
    data = json.loads(path.read_text(encoding="utf-8"))
    geometry = data["features"][0]["geometry"]
    if geometry["type"] != "Polygon":
        raise RuntimeError(f"unsupported boundary geometry: {geometry['type']}")
    ring = geometry["coordinates"][0]
    polygon = [
        xy_from_lon_lat(float(lon), float(lat), lon0, lat0)
        for lon, lat, *_ in ring
    ]
    if polygon[0] == polygon[-1]:
        polygon.pop()
    return polygon


def write_values(handle, label: str, values) -> None:
    serialized = " ".join(f"{float(value):.17g}" for value in values)
    handle.write(f"{label} {len(values)} {serialized}\n")


def build_case(source: Path, output: Path, case: str) -> dict:
    layout_path = source / "data" / "layout" / f"{case}.csv"
    boundary_path = source / "data" / "layout" / f"{case}.geojson"
    wind_path = source / "data" / "wind" / f"{case}.mat"
    layout = read_layout(layout_path)
    boundary = read_boundary(boundary_path, layout_path)
    turbine_count = len(layout)
    candidate_seed = 42 + sum(case.encode("utf-8"))
    spacing = 3.0 * 117.0
    for reduction in range(1000):
        rng = SplitMix64(candidate_seed)
        candidates = poisson(boundary, spacing, rng)
        if len(candidates) >= turbine_count:
            break
        spacing *= 0.99
    else:
        raise RuntimeError(f"insufficient candidates for {case}")

    wind = loadmat(wind_path, squeeze_me=True)
    theta_degrees = [float(value) for value in wind["theta"].tolist()]
    theta_radians = [math.radians(value) for value in theta_degrees]
    velocities = [float(value) for value in wind["velocity"].tolist()]
    raw_probabilities = wind["f_theta_v"]
    probabilities = [
        float(raw_probabilities[speed, direction])
        for direction in range(len(theta_radians))
        for speed in range(len(velocities))
    ]
    probability_sum = sum(probabilities)
    if not math.isclose(probability_sum, 1.0, rel_tol=0.0, abs_tol=1e-10):
        raise RuntimeError(
            f"joint wind probability for {case} sums to {probability_sum}"
        )

    turbine_path = (
        source
        / "data"
        / "turbine"
        / "Vetas_4200kwturbine_speed-pv-Ct_value.csv"
    )
    curve = []
    with turbine_path.open(encoding="latin-1", newline="") as handle:
        reader = csv.reader(handle)
        next(reader)
        for row in reader:
            if len(row) >= 3 and row[0].strip():
                curve.append(tuple(float(row[index]) for index in range(3)))

    substation = (
        sum(x for x, _ in layout) / turbine_count,
        sum(y for _, y in layout) / turbine_count,
    )
    turbine_current = 4.2e6 / (math.sqrt(3.0) * 33.0e3 * 0.95)
    cable_capacity = [
        math.floor(current / turbine_current)
        for current in (430.0, 680.0, 900.0)
    ]
    if cable_capacity != [5, 8, 11]:
        raise RuntimeError(
            f"unexpected cable capacities for {case}: {cable_capacity}"
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as handle:
        handle.write("GGA_CASE_V1\n")
        handle.write("profile geojson_radians_ct_rss_repaired_v1\n")
        handle.write(f"case {case}\n")
        handle.write(f"candidate_seed {candidate_seed}\n")
        handle.write(f"turbine_count {turbine_count}\n")
        handle.write(f"minimum_spacing_m {spacing:.17g}\n")
        handle.write(
            f"substation_m {substation[0]:.17g} {substation[1]:.17g}\n"
        )
        handle.write(f"boundary_count {len(boundary)}\n")
        for x, y in boundary:
            handle.write(f"boundary {x:.17g} {y:.17g}\n")
        handle.write(f"candidate_count {len(candidates)}\n")
        for x, y in candidates:
            handle.write(f"candidate {x:.17g} {y:.17g}\n")
        write_values(handle, "theta_radians", theta_radians)
        write_values(handle, "velocity_mps", velocities)
        write_values(handle, "joint_probability_dir_major", probabilities)
        handle.write(f"curve_count {len(curve)}\n")
        for speed, power, ct in curve:
            handle.write(f"curve {speed:.17g} {power:.17g} {ct:.17g}\n")
        handle.write("turbine_pinst_kw 4200\n")
        handle.write("hub_height_m 91.5\n")
        handle.write("rotor_diameter_m 117\n")
        handle.write("cut_in_mps 3\n")
        handle.write("cut_out_mps 25\n")
        handle.write("sea_depth_m 7\n")
        handle.write("offshore_length_m 0\n")
        write_values(
            handle,
            "inner_cable_capacity",
            cable_capacity,
        )
        write_values(handle, "inner_cable_price", [192, 321, 506])
        handle.write("export_cable_price 601.5\n")

    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    return {
        "case": case,
        "turbines": turbine_count,
        "candidates": len(candidates),
        "minimum_spacing_m": spacing,
        "candidate_seed": candidate_seed,
        "direction_count": len(theta_radians),
        "speed_bin_count": len(velocities),
        "joint_probability_sum": probability_sum,
        "inner_cable_capacity": cable_capacity,
        "snapshot_sha256": digest,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    records = [
        build_case(
            arguments.source,
            arguments.output / f"{case}.wfp",
            case,
        )
        for case in CASES
    ]
    manifest = {
        "schema_version": 1,
        "profile": "geojson_radians_ct_rss_repaired_v1",
        "source_commit": "6ce41326e6c1d3685a01e038baf6d1d07aa46126",
        "cases": records,
    }
    manifest_path = arguments.output / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
    )
    print(
        f"gga_asset_prepare_pass cases={len(records)} "
        f"manifest={manifest_path}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
