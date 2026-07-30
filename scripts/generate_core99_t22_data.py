#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T22 IEA Task 37 case-study-4 data compiler
Paper/DOI: A Comparison of Eight Optimization Methods Applied to a Wind
Farm Layout Optimization Problem; 10.5194/wes-8-865-2023
Public source: https://github.com/jaredthomas68/thomas2022-8-opt-algs-wflop
at revision 41d7290b8cc9bf3d90b25d844312f4790037806d; archived by
10.5281/zenodo.7125349
Provided assets: five boundary polygons, 360-direction by 20-speed wind
resource, turbine attributes, baseline layout, DEBO result layout, and
Python/Julia evaluation models
Missing/conflicts: the public repository does not contain a license file or
the executable DEBO optimizer; the Python module header calls the evaluator
case study 3 while its data and paper use case study 4; the DEBO YAML embeds
an AEP value that is stale relative to its frozen wind/model and paper table
Resolution: this script extracts numeric research data only; production
equations and DEBO are independently reimplemented from the CC-BY paper.
Generated files preserve source revision and hashes and are not represented
as author code.
Method/problem semantic IDs: t22_debo_paper_reconstruction_v1;
t22_iea37_cs4_gaussian_aep_v1
Controlling contract: shared/contracts/core99_t22_iea37_cs4.json
Production backend: generated C++ constants consumed by pure-C++ CPU-HPC
Claim boundary: academic declared reproduction, not author-source DEBO
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

import yaml


SOURCE_REVISION = "41d7290b8cc9bf3d90b25d844312f4790037806d"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_yaml(path: Path) -> dict[str, Any]:
    return yaml.safe_load(path.read_text(encoding="utf-8"))


def layout(path: Path) -> tuple[list[list[float]], float]:
    definitions = load_yaml(path)["definitions"]
    positions = definitions["position"]["items"]
    aep = definitions["plant_energy"]["properties"][
        "annual_energy_production"
    ]["default"]
    return positions, float(aep)


def fmt(value: float) -> str:
    return f"{float(value):.17g}"


def emit_array(name: str, values: list[float], width: int = 6) -> str:
    lines = [
        f"inline constexpr std::array<double, {len(values)}> {name} = {{{{"
    ]
    for start in range(0, len(values), width):
        chunk = ", ".join(fmt(value) for value in values[start : start + width])
        lines.append(f"    {chunk},")
    lines.append("}};")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--contract", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    args = parser.parse_args()

    source = args.source_root
    boundary_path = source / "src/input-files/farms/iea37-boundary-cs4.yaml"
    wind_path = source / "src/input-files/wind/iea37-windrose-cs4.yaml"
    turbine_path = source / "src/input-files/turbines/iea37-10mw.yaml"
    base_path = source / "results/optimization-results/base.yaml"
    debo_path = source / "results/optimization-results/debo.yaml"

    boundary_yaml = load_yaml(boundary_path)
    wind_yaml = load_yaml(wind_path)
    turbine_yaml = load_yaml(turbine_path)
    base_positions, base_aep = layout(base_path)
    debo_positions, debo_aep = layout(debo_path)

    boundaries = [
        [[float(x), float(y)] for x, y in vertices]
        for vertices in boundary_yaml["boundaries"].values()
    ]
    inflow = wind_yaml["definitions"]["wind_inflow"]["properties"]
    turbine = turbine_yaml["definitions"]
    directions = [float(x) for x in inflow["direction"]["bins"]]
    direction_frequency = [
        float(x) for x in inflow["direction"]["frequency"]
    ]
    speeds = [float(x) for x in inflow["speed"]["bins"]]
    speed_frequency = [
        [float(x) for x in row] for row in inflow["speed"]["frequency"]
    ]
    if len(directions) != 360 or len(speeds) != 20:
        raise ValueError("T22 paper-native wind-resource dimensions changed")
    if len(speed_frequency) != 360 or any(
        len(row) != 20 for row in speed_frequency
    ):
        raise ValueError("T22 speed-probability matrix dimensions changed")
    if len(base_positions) != 81 or len(debo_positions) != 81:
        raise ValueError("T22 paper-native layout size changed")

    contract = {
        "schema_version": 1,
        "corpus_id": "T22",
        "paper_doi": "10.5194/wes-8-865-2023",
        "archive_doi": "10.5281/zenodo.7125349",
        "public_source_url": (
            "https://github.com/jaredthomas68/"
            "thomas2022-8-opt-algs-wflop"
        ),
        "public_source_revision": SOURCE_REVISION,
        "source_license_file": None,
        "source_hashes": {
            path.name: sha256(path)
            for path in (
                boundary_path,
                wind_path,
                turbine_path,
                base_path,
                debo_path,
            )
        },
        "problem_semantic_id": "t22_iea37_cs4_gaussian_aep_v1",
        "method_semantic_id": "t22_debo_paper_reconstruction_v1",
        "boundaries": boundaries,
        "wind": {
            "directions_deg": directions,
            "direction_frequency": direction_frequency,
            "speeds_mps": speeds,
            "conditional_speed_frequency": speed_frequency,
        },
        "turbine": {
            "count": 81,
            "diameter_m": float(turbine["rotor"]["diameter"]["default"]),
            "cut_in_mps": float(
                turbine["operating_mode"]["cut_in_wind_speed"]["default"]
            ),
            "cut_out_mps": float(
                turbine["operating_mode"]["cut_out_wind_speed"]["default"]
            ),
            "rated_speed_mps": float(
                turbine["operating_mode"]["rated_wind_speed"]["default"]
            ),
            "rated_power_w": float(
                turbine["wind_turbine"]["rated_power"]["maximum"]
            ),
            "thrust_coefficient": 8.0 / 9.0,
            "wake_expansion": 0.0324555,
        },
        "constraints": {"minimum_spacing_m": 396.0},
        "debo": {
            "dx_m": 100.0,
            "dy_m": 100.0,
            "dmax_m": 990.0,
            "neighborhood_initial_m": 1000.0,
            "neighborhood_minimum_m": 5.0,
            "neighborhood_half_divisions": 6,
            "neighborhood_reduction": 0.75,
        },
        "author_receipts": {
            "base": {
                "positions_m": base_positions,
                "aep_mwh": base_aep,
            },
            "debo": {
                "positions_m": debo_positions,
                "aep_mwh": debo_aep,
            },
        },
    }
    args.contract.parent.mkdir(parents=True, exist_ok=True)
    args.contract.write_text(
        json.dumps(contract, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    flat_boundaries: list[float] = []
    offsets = [0]
    for polygon in boundaries:
        for x, y in polygon:
            flat_boundaries.extend([x, y])
        offsets.append(offsets[-1] + len(polygon))
    flat_speed_frequency = [
        value for row in speed_frequency for value in row
    ]
    flat_base = [value for pair in base_positions for value in pair]
    flat_debo = [value for pair in debo_positions for value in pair]
    header = f"""/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T22 generated IEA Task 37 case-study-4 data
Generated numeric data for T22. Do not edit manually.
Paper DOI: 10.5194/wes-8-865-2023
Archive DOI: 10.5281/zenodo.7125349
Public source: paper-linked archive at revision {SOURCE_REVISION}
Missing information and reconstruction: numeric data are compiled from the
archive; no author optimizer code is included
Method/problem semantic IDs: t22_debo_paper_reconstruction_v1;
t22_iea37_cs4_gaussian_aep_v1
Controlling contract: shared/contracts/core99_t22_iea37_cs4.json
Generator: scripts/generate_core99_t22_data.py
Claim boundary: numeric research data, not copied author optimizer code
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once
#include <array>
#include <cstddef>
namespace core99::t22::data {{
inline constexpr std::size_t kDirections = 360;
inline constexpr std::size_t kSpeeds = 20;
inline constexpr std::size_t kTurbines = 81;
inline constexpr double kDiameter = 198.0;
inline constexpr double kMinimumSpacing = 396.0;
inline constexpr double kCutIn = 4.0;
inline constexpr double kCutOut = 25.0;
inline constexpr double kRatedSpeed = 11.0;
inline constexpr double kRatedPower = 10000000.0;
inline constexpr double kThrustCoefficient = 8.0 / 9.0;
inline constexpr double kWakeExpansion = 0.0324555;
inline constexpr double kBaseAepMwh = {fmt(base_aep)};
inline constexpr double kDeboAepMwh = {fmt(debo_aep)};
{emit_array("kBoundaryCoordinates", flat_boundaries)}
inline constexpr std::array<std::size_t, {len(offsets)}> kBoundaryOffsets = {{{{
    {", ".join(str(value) for value in offsets)}
}}}};
{emit_array("kDirectionDegrees", directions)}
{emit_array("kDirectionFrequency", direction_frequency)}
{emit_array("kSpeedBins", speeds)}
{emit_array("kConditionalSpeedFrequency", flat_speed_frequency)}
{emit_array("kBasePositions", flat_base)}
{emit_array("kDeboPositions", flat_debo)}
}}  // namespace core99::t22::data
"""
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(header, encoding="utf-8")


if __name__ == "__main__":
    main()
