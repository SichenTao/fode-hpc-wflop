#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T12 WindFLO 2015 scenario-data compiler
Paper DOI: 10.1016/j.renene.2018.03.052
Public source: https://github.com/d9w/WindFLO at revision
9e85a67bb2ca019768ea51dd0b634a46c8406ba2, MIT license
Provided assets: five hidden-competition scenarios later released as XML,
offline evaluators, and four winning-entry archives
Missing/conflicts and reconstruction: numeric XML data are compiled without
alteration; algorithm-language/source conflicts are handled in the T12 source
dossier and not in this data compiler
Method/problem semantic IDs: t12_four_competition_methods_v1;
t12_windflo_2015_five_scenarios_v1
Controlling contract: shared/contracts/core99_t12_windflo_2015.json
Claim boundary: generated numeric data, not author optimizer code
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any


SOURCE_REVISION = "9e85a67bb2ca019768ea51dd0b634a46c8406ba2"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fmt(value: float) -> str:
    return f"{float(value):.17g}"


def emit_array(name: str, values: list[float], width: int = 6) -> str:
    lines = [
        f"inline constexpr std::array<double, {len(values)}> {name} = {{{{"
    ]
    for start in range(0, len(values), width):
        lines.append(
            "    "
            + ", ".join(fmt(v) for v in values[start : start + width])
            + ","
        )
    lines.append("}};")
    return "\n".join(lines)


def parse_scenario(path: Path) -> dict[str, Any]:
    root = ET.parse(path).getroot()
    angle_root = root.find("Angles")
    obstacle_root = root.find("Obstacles")
    angles = [
        {
            "c": float(node.attrib["c"]),
            "k": float(node.attrib["k"]),
            "omega": float(node.attrib["omega"]),
            "theta_deg": float(node.attrib["theta"]),
        }
        for node in ([] if angle_root is None else angle_root)
    ]
    obstacles = [
        [
            float(node.attrib["xmin"]),
            float(node.attrib["ymin"]),
            float(node.attrib["xmax"]),
            float(node.attrib["ymax"]),
        ]
        for node in ([] if obstacle_root is None else obstacle_root)
    ]
    parameters = root.find("Parameters")
    if parameters is None or len(angles) != 24:
        raise ValueError(f"invalid WindFLO scenario: {path}")
    return {
        "id": f"t12_windflo_s{int(path.stem) + 1}",
        "source_file": path.name,
        "source_sha256": sha256(path),
        "width_m": float(parameters.findtext("Width", "")),
        "height_m": float(parameters.findtext("Height", "")),
        "nominal_turbines": int(parameters.findtext("NTurbines", "")),
        "wake_free_energy": float(
            parameters.findtext("WakeFreeEnergy", "")
        ),
        "angles": angles,
        "obstacles": obstacles,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--contract", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    args = parser.parse_args()
    scenarios = [
        parse_scenario(args.source_root / f"{index}.xml")
        for index in range(5)
    ]
    contract = {
        "schema_version": 1,
        "corpus_id": "T12",
        "paper_doi": "10.1016/j.renene.2018.03.052",
        "public_source_url": "https://github.com/d9w/WindFLO",
        "public_source_revision": SOURCE_REVISION,
        "public_source_license": "MIT",
        "method_semantic_id": "t12_four_competition_methods_v1",
        "problem_semantic_id": "t12_windflo_2015_five_scenarios_v1",
        "turbine": {
            "thrust_coefficient": 0.8,
            "rated_power_kw": 1500.0,
            "radius_m": 38.5,
            "power_intercept": -500.0,
            "wake_expansion": 0.075,
            "power_slope": 140.86,
            "cut_in_mps": 3.5,
            "cut_out_mps": 20.0,
            "rated_speed_mps": 14.0,
        },
        "economics": {
            "turbine_cost": 750000.0,
            "substation_cost": 8000000.0,
            "turbines_per_substation": 30,
            "interest_rate": 0.03,
            "farm_lifetime_years": 20,
            "operation_maintenance_cost": 20000.0,
        },
        "minimum_spacing_m": 308.0,
        "physical_fes": {
            "per_scenario": 2000,
            "five_scenario_total": 10000,
        },
        "algorithms": [
            "t12_3s_mde",
            "t12_cmaes_geometric",
            "t12_sshh",
            "t12_goldman_lattice",
        ],
        "scenarios": scenarios,
    }
    args.contract.parent.mkdir(parents=True, exist_ok=True)
    args.contract.write_text(
        json.dumps(contract, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    widths = [s["width_m"] for s in scenarios]
    heights = [s["height_m"] for s in scenarios]
    counts = [float(s["nominal_turbines"]) for s in scenarios]
    wake_free = [s["wake_free_energy"] for s in scenarios]
    c_values = [
        angle["c"] for scenario in scenarios for angle in scenario["angles"]
    ]
    k_values = [
        angle["k"] for scenario in scenarios for angle in scenario["angles"]
    ]
    omega_values = [
        angle["omega"]
        for scenario in scenarios
        for angle in scenario["angles"]
    ]
    obstacles: list[float] = []
    obstacle_offsets = [0]
    for scenario in scenarios:
        for obstacle in scenario["obstacles"]:
            obstacles.extend(obstacle)
        obstacle_offsets.append(
            obstacle_offsets[-1] + len(scenario["obstacles"])
        )
    header = f"""/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T12 generated WindFLO 2015 scenario data
Paper DOI: 10.1016/j.renene.2018.03.052
Public source: https://github.com/d9w/WindFLO revision {SOURCE_REVISION}
Missing information and reconstruction: numeric XML values compiled unchanged
Method/problem semantic IDs: t12_four_competition_methods_v1;
t12_windflo_2015_five_scenarios_v1
Controlling contract: shared/contracts/core99_t12_windflo_2015.json
Claim boundary: numeric MIT-licensed scenario data, not author optimizer code
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once
#include <array>
#include <cstddef>
namespace core99::t12::data {{
inline constexpr std::size_t kScenarios = 5;
inline constexpr std::size_t kDirections = 24;
{emit_array("kWidth", widths)}
{emit_array("kHeight", heights)}
{emit_array("kNominalTurbines", counts)}
{emit_array("kWakeFreeEnergy", wake_free)}
{emit_array("kWeibullScale", c_values)}
{emit_array("kWeibullShape", k_values)}
{emit_array("kDirectionDensity", omega_values)}
{emit_array("kObstacles", obstacles)}
inline constexpr std::array<std::size_t, {len(obstacle_offsets)}>
kObstacleOffsets = {{{{
    {", ".join(str(v) for v in obstacle_offsets)}
}}}};
}}  // namespace core99::t12::data
"""
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(header, encoding="utf-8")


if __name__ == "__main__":
    main()
