#!/usr/bin/env python3
"""Cross-check the pure C++ repaired GGA evaluator with a SciPy oracle."""

from __future__ import annotations

import argparse
import json
import math
import subprocess
import tempfile
from pathlib import Path

import numpy as np
from scipy.interpolate import CubicSpline


def scalar(lines: list[list[str]], label: str, cast=float):
    rows = [row for row in lines if row and row[0] == label]
    if len(rows) != 1 or len(rows[0]) != 2:
        raise RuntimeError(f"invalid scalar {label}")
    return cast(rows[0][1])


def vector(lines: list[list[str]], label: str) -> np.ndarray:
    rows = [row for row in lines if row and row[0] == label]
    if len(rows) != 1:
        raise RuntimeError(f"invalid vector {label}")
    count = int(rows[0][1])
    values = np.asarray([float(value) for value in rows[0][2:]])
    if values.size != count:
        raise RuntimeError(f"invalid vector cardinality {label}")
    return values


def load_problem(path: Path) -> dict:
    lines = [
        line.split()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    curve = np.asarray([
        [float(row[1]), float(row[2]), float(row[3])]
        for row in lines
        if row[0] == "curve"
    ])
    return {
        "case": scalar(lines, "case", str),
        "turbines": scalar(lines, "turbine_count", int),
        "substation": np.asarray([
            float(value)
            for row in lines
            if row[0] == "substation_m"
            for value in row[1:]
        ]),
        "candidates": np.asarray([
            [float(row[1]), float(row[2])]
            for row in lines
            if row[0] == "candidate"
        ]),
        "theta": vector(lines, "theta_radians"),
        "velocity": vector(lines, "velocity_mps"),
        "probability": vector(lines, "joint_probability_dir_major"),
        "curve": curve,
        "pinst_kw": scalar(lines, "turbine_pinst_kw"),
        "hub_height_m": scalar(lines, "hub_height_m"),
        "rotor_diameter_m": scalar(lines, "rotor_diameter_m"),
        "cut_in_mps": scalar(lines, "cut_in_mps"),
        "cut_out_mps": scalar(lines, "cut_out_mps"),
        "sea_depth_m": scalar(lines, "sea_depth_m"),
        "offshore_length_m": scalar(lines, "offshore_length_m"),
        "cable_capacity": vector(lines, "inner_cable_capacity").astype(int),
        "cable_price": vector(lines, "inner_cable_price"),
        "export_cable_price": scalar(lines, "export_cable_price"),
    }


def group_tree(
    group: list[int],
    coordinates: np.ndarray,
    substation: np.ndarray,
) -> list[tuple[int, int, float]]:
    size = len(group)
    keys = [math.inf] * size
    parent = [-1] * size
    used = [False] * size
    keys[0] = 0.0
    edges: list[tuple[int, int, float]] = []
    for _ in range(size):
        selected = min(
            (index for index in range(size) if not used[index]),
            key=lambda index: (keys[index], index),
        )
        used[selected] = True
        if parent[selected] >= 0:
            source = parent[selected]
            edges.append((
                group[source],
                group[selected],
                float(np.linalg.norm(coordinates[source] - coordinates[selected])),
            ))
        for other in range(size):
            candidate = float(np.linalg.norm(
                coordinates[selected] - coordinates[other]
            ))
            if not used[other] and candidate < keys[other]:
                keys[other] = candidate
                parent[other] = selected
    closest = min(
        range(size),
        key=lambda index: (
            float(np.linalg.norm(coordinates[index] - substation)),
            index,
        ),
    )
    edges.append((
        -1,
        group[closest],
        float(np.linalg.norm(coordinates[closest] - substation)),
    ))
    return edges


def priced_tree(
    edges: list[tuple[int, int, float]],
    turbines: int,
    capacity: np.ndarray,
    prices: np.ndarray,
) -> float:
    root = turbines
    adjacency: list[list[tuple[int, int]]] = [
        [] for _ in range(turbines + 1)
    ]
    for edge_index, (left, right, _) in enumerate(edges):
        left_node = root if left < 0 else left
        adjacency[left_node].append((right, edge_index))
        adjacency[right].append((left_node, edge_index))
    parent = [-2] * (turbines + 1)
    parent_edge = [-1] * (turbines + 1)
    order = [root]
    parent[root] = -1
    for node in order:
        for next_node, edge_index in adjacency[node]:
            if parent[next_node] == -2:
                parent[next_node] = node
                parent_edge[next_node] = edge_index
                order.append(next_node)
    subtree = [1] * (turbines + 1)
    subtree[root] = 0
    cost = 0.0
    for node in reversed(order):
        if node == root:
            continue
        load = subtree[node]
        cable_type = next(
            (index for index, limit in enumerate(capacity) if load <= limit),
            None,
        )
        if cable_type is None:
            return math.inf
        cost += edges[parent_edge[node]][2] * prices[cable_type]
        subtree[parent[node]] += load
    return cost


def matlab_round_positive(value: float) -> int:
    return math.floor(value + 0.5)


def route_bsr(coordinates: np.ndarray, problem: dict) -> float:
    turbines = len(coordinates)
    capacity = int(problem["cable_capacity"][-1])
    delta = coordinates - problem["substation"]
    angles = np.arctan2(delta[:, 1], delta[:, 0])
    sorted_indices = list(np.argsort(angles, kind="stable"))
    groups = math.ceil(turbines / capacity)
    best = math.inf
    for start in range(capacity):
        order = [
            sorted_indices[(start + offset) % turbines]
            for offset in range(turbines)
        ]
        edges: list[tuple[int, int, float]] = []
        for group_index in range(groups):
            begin = matlab_round_positive(group_index * turbines / groups)
            end = matlab_round_positive((group_index + 1) * turbines / groups)
            members = order[begin:end]
            points = coordinates[members]
            edges.extend(group_tree(
                members,
                points,
                problem["substation"],
            ))
        best = min(best, priced_tree(
            edges,
            turbines,
            problem["cable_capacity"],
            problem["cable_price"],
        ))
    return best


def evaluate(layout: list[int], problem: dict) -> dict:
    coordinates = problem["candidates"][layout]
    cable_cost = route_bsr(coordinates, problem)
    curve = problem["curve"]
    power_curve = CubicSpline(
        curve[:, 0],
        curve[:, 1],
        bc_type="not-a-knot",
    )
    expected_power_kw = 0.0
    rotor_radius = problem["rotor_diameter_m"] / 2.0
    for direction, angle in enumerate(problem["theta"]):
        rotation = np.asarray([
            [math.cos(angle), -math.sin(angle)],
            [math.sin(angle), math.cos(angle)],
        ])
        rotated = coordinates @ rotation.T
        order = list(np.argsort(-rotated[:, 1], kind="stable"))
        for speed_index, free_speed in enumerate(problem["velocity"]):
            ct = float(np.interp(
                free_speed,
                curve[:, 0],
                curve[:, 2],
            ))
            ct = min(0.999999, max(0.0, ct))
            axial = 0.5 * (1.0 - math.sqrt(1.0 - ct))
            effective = np.full(len(coordinates), free_speed)
            for downstream_position in range(1, len(order)):
                downstream = order[downstream_position]
                squared_deficit = 0.0
                for upstream_position in range(downstream_position):
                    upstream = order[upstream_position]
                    along = rotated[upstream, 1] - rotated[downstream, 1]
                    cross = abs(
                        rotated[downstream, 0] - rotated[upstream, 0]
                    )
                    wake_radius = rotor_radius + 0.1 * along
                    if along > 0.0 and cross < wake_radius:
                        deficit = (
                            2.0 * axial * (rotor_radius / wake_radius) ** 2
                        )
                        squared_deficit += deficit * deficit
                effective[downstream] = free_speed * max(
                    0.0, 1.0 - math.sqrt(squared_deficit)
                )
            farm_power = sum(
                float(power_curve(speed))
                for speed in effective
                if problem["cut_in_mps"] <= speed <= problem["cut_out_mps"]
            )
            probability_index = (
                direction * len(problem["velocity"]) + speed_index
            )
            expected_power_kw += (
                farm_power * problem["probability"][probability_index]
            )

    aep = expected_power_kw * 365.0 * 24.0
    capacity_factor = aep / (
        365.0 * 24.0 * problem["pinst_kw"] * problem["turbines"]
    )
    power_mw = problem["pinst_kw"] / 1000.0
    installed_mw = power_mw * problem["turbines"]
    c_w = 2.95e3 * math.log(power_mw) - 375.2
    c_ist = 0.113 * c_w
    c_f = (
        320.0
        * power_mw
        * (1.0 + 0.02 * (problem["sea_depth_m"] - 8.0))
        * (
            1.0
            + 0.8e-6
            * (
                problem["hub_height_m"]
                * (problem["rotor_diameter_m"] / 2.0) ** 2
                - 1.0e5
            )
        )
    )
    turbine_cost = (
        (c_w + c_ist + 1.5 * c_f) * problem["turbines"]
    )
    cable_total = (
        cable_cost
        + problem["offshore_length_m"] * problem["export_cable_price"]
    ) / 1000.0
    substation_cost = (
        539.0 * installed_mw**0.678 + 87.250 * installed_mw
    )
    cost_per_mw = np.asarray([7, 92, 88, 52, 144, 130, 89, 109, 365])
    historical_power = np.asarray([495, 120, 108, 60, -1, 40, 40, -1, -1])
    years = np.asarray([2004, 2010, 2007, 2006, 2010, 2010, 2005, 2009, 2010])
    valid = historical_power > 0
    weights = historical_power[valid] / (2024 - years[valid])
    development_cost = (
        float(np.sum(cost_per_mw[valid] * weights) / np.sum(weights))
        * installed_mw
    )
    decommissioning = 0.0093 * c_w * problem["turbines"]
    annual_opex = 78.2 * installed_mw
    other = (
        0.114 * (turbine_cost + substation_cost + cable_total)
        + 0.092 * annual_opex * 25.0
    )
    investment = (
        turbine_cost
        + substation_cost
        + cable_total
        + development_cost
        + decommissioning
        + other
    )
    discounted_cost = investment
    discounted_energy = 0.0
    for year in range(1, 26):
        discount = 1.05**year
        discounted_cost += annual_opex / discount
        discounted_energy += aep / discount
    return {
        "best_lcoe": discounted_cost / discounted_energy * 1000.0,
        "best_capacity_factor": capacity_factor,
        "best_aep_kwh": aep,
        "best_cable_cost": cable_cost,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--receipt", type=Path)
    arguments = parser.parse_args()
    records = []
    maximum_absolute_error = 0.0
    maximum_scaled_absolute_error = 0.0
    with tempfile.TemporaryDirectory(prefix="gga-evaluator-oracle-") as temp:
        for problem_path in sorted(arguments.assets.glob("*.wfp")):
            problem = load_problem(problem_path)
            layout = list(range(problem["turbines"]))
            output_path = Path(temp) / f"{problem['case']}.json"
            subprocess.run(
                [
                    str(arguments.binary),
                    "--problem",
                    str(problem_path),
                    "--evaluate-layout",
                    ",".join(str(index) for index in layout),
                    "--output",
                    str(output_path),
                ],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            observed = json.loads(output_path.read_text(encoding="utf-8"))
            expected = evaluate(layout, problem)
            errors = {}
            for key, oracle_value in expected.items():
                cpp_value = observed[key]
                absolute_error = abs(cpp_value - oracle_value)
                relative_error = absolute_error / max(1.0, abs(oracle_value))
                maximum_absolute_error = max(
                    maximum_absolute_error, absolute_error
                )
                maximum_scaled_absolute_error = max(
                    maximum_scaled_absolute_error, relative_error
                )
                errors[key] = {
                    "cpp": cpp_value,
                    "oracle": oracle_value,
                    "absolute_error": absolute_error,
                    "scaled_relative_error": relative_error,
                }
                if relative_error > 2e-12:
                    raise RuntimeError(
                        f"{problem['case']} {key} differs: "
                        f"{cpp_value} vs {oracle_value}"
                    )
            records.append({
                "case": problem["case"],
                "layout": "first_turbine_count_candidates",
                "status": "pass",
                "errors": errors,
            })
    receipt = {
        "schema_version": 1,
        "status": "pass",
        "oracle": "independent_python_scipy_not_a_knot_bsr",
        "case_count": len(records),
        "maximum_absolute_error": maximum_absolute_error,
        "maximum_scaled_absolute_error": maximum_scaled_absolute_error,
        "scaled_tolerance": 2.0e-12,
        "cases": records,
    }
    if arguments.receipt:
        arguments.receipt.parent.mkdir(parents=True, exist_ok=True)
        arguments.receipt.write_text(
            json.dumps(receipt, indent=2) + "\n",
            encoding="utf-8",
        )
    print(json.dumps({
        "status": "pass",
        "cases": len(records),
        "maximum_absolute_error": maximum_absolute_error,
        "maximum_scaled_absolute_error": maximum_scaled_absolute_error,
        "scaled_tolerance": 2.0e-12,
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
