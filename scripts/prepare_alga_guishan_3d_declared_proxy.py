#!/usr/bin/env python3
"""Generate the declared 3D Guishan-family ALGA problem package.

The paper exposes the 12x12 grid, 500 m spacing, turbine counts, MySE11-230
surface parameters, four ideal and four seasonal classes, and the 3D Gaussian
wake equations.  It does not publish the Guishan elevation or seasonal arrays.
This generator therefore creates a separately named P3 analytic terrain and
deterministic wind completion; it never uses the original-problem identity.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path


SEMANTICS = "alga_guishan_3d_declared_proxy_v1"


def normalized(values: list[float]) -> list[float]:
    total = sum(values)
    return [value / total for value in values]


def terrain() -> list[float]:
    values = []
    for row in range(12):
        for col in range(12):
            x = col / 11.0
            y = row / 11.0
            values.append(
                18.0
                + 8.0 * math.sin(math.pi * x) * math.cos(math.pi * y)
                + 3.0 * math.sin(2.0 * math.pi * y)
            )
    return values


def seasonal_joint(season: int) -> tuple[list[float], list[float], list[list[float]]]:
    directions = [2.0 * math.pi * index / 12.0 for index in range(12)]
    speeds = [5.75, 8.5, 11.25, 14.0, 16.75]
    center = [1, 4, 7, 10][season]
    direction_weight = normalized(
        [
            0.15
            + math.exp(
                1.6
                * math.cos(2.0 * math.pi * (index - center) / 12.0)
            )
            for index in range(12)
        ]
    )
    speed_weight = [
        [0.08, 0.25, 0.37, 0.22, 0.08],
        [0.06, 0.20, 0.34, 0.27, 0.13],
        [0.11, 0.30, 0.36, 0.17, 0.06],
        [0.07, 0.23, 0.35, 0.24, 0.11],
    ][season]
    return (
        directions,
        speeds,
        [
            [
                direction_weight[direction] * speed_weight[speed]
                for speed in range(len(speeds))
            ]
            for direction in range(len(directions))
        ],
    )


def build() -> dict:
    elevation = terrain()
    cases = []
    for scenario in range(8):
        if scenario < 4:
            directions = [scenario * math.pi / 2.0]
            speeds = [9.3]
            joint = [[1.0]]
            scenario_id = f"IDEAL{scenario + 1}"
            scenario_type = "paper_ideal_direction_completion"
        else:
            directions, speeds, joint = seasonal_joint(scenario - 4)
            scenario_id = f"SEASON{scenario - 3}"
            scenario_type = "declared_seasonal_joint_wind_completion"
        for turbine_count in (20, 30, 40):
            cases.append(
                {
                    "case_id": f"ALGA_Guishan3D_{scenario_id}_tn{turbine_count}",
                    "semantics_id": SEMANTICS,
                    "scenario_type": scenario_type,
                    "rows": 12,
                    "cols": 12,
                    "turbine_count": turbine_count,
                    "cell_width": 500.0,
                    "rotor_diameter": 135.0,
                    "hub_height": 100.0,
                    "surface_roughness": 0.00025,
                    "wake_deficit_coefficient": 2.0 / 3.0,
                    "wake_model": "terrain_gaussian_rss",
                    "gaussian_wake_expansion": 0.05,
                    "terrain_shear_exponent": 0.1,
                    "terrain_elevation_m": elevation,
                    "power_curve_model": "cutin_shifted_cubic",
                    "power_curve_cubic_coefficient": 0.3,
                    "power_curve_rated_kw": 3000.0,
                    "power_curve_cutin_mps": 3.0,
                    "power_curve_rated_mps": 9.3,
                    "power_curve_cutout_mps": 20.0,
                    "unavailable_cells_1based": [],
                    "wind_directions_rad": directions,
                    "wind_speeds_mps": speeds,
                    "joint_probabilities": joint,
                }
            )
    payload = {
        "schema_version": 1,
        "problem_semantic_id": SEMANTICS,
        "evidence_tier": "P3_DECLARED_PROXY",
        "paper_doi": "10.1016/j.swevo.2025.102018",
        "paper_preserved": (
            "12x12 grid; 500 m cells; N=20,30,40; four ideal and four "
            "seasonal classes; MySE11-230 visible parameters; 3D Gaussian "
            "wake and RSS superposition"
        ),
        "missing_author_assets": [
            "Guishan elevation array",
            "four seasonal joint-wind arrays",
            "author turbine curve samples",
        ],
        "declared_completion": {
            "terrain": (
                "z=18+8*sin(pi*x)*cos(pi*y)+3*sin(2*pi*y), normalized "
                "grid coordinates"
            ),
            "ideal_wind": (
                "four cardinal directions at the paper rated speed"
            ),
            "seasonal_wind": (
                "four deterministic von-Mises-like directional families "
                "times declared five-bin speed marginals"
            ),
        },
        "case_count": len(cases),
        "cases": cases,
    }
    canonical = json.dumps(
        payload, sort_keys=True, separators=(",", ":")
    ).encode()
    payload["content_sha256_without_hash"] = hashlib.sha256(
        canonical
    ).hexdigest()
    return payload


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(
            "shared/contracts/alga_guishan_3d_declared_proxy_cases.json"
        ),
    )
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(build(), indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"alga_guishan_3d_cases_written={args.output} cases=24")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
