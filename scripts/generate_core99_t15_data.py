#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T15 IEA37 case-study immutable data generator
Paper/DOI: Best Practices for Wake Model and Optimization Algorithm Selection
in Wind Farm Layout Optimization; 10.2514/6.2019-0540
Public source: https://github.com/byuflowlab/iea37-wflo-casestudies
tag v2.0.1, revision af88908d22795030ac2dfbe37bc38e912aee8ed6
Provided assets: common Python evaluator, turbine/wind YAML, three examples,
12 later repository submissions for each case-1 size, five case-2 layouts,
and participant-reported case-2 AEP data
Missing/conflicts: the paper tables contain participants 1--10, whereas the
current archive also contains later participants 11--12; case study 2
intentionally has no common wake model and its participant implementations
are not released
Reconstruction and resolution: generate only paper-native participants 1--10
plus the three examples for case study 1; retain case-study-2 layouts and
reported matrix as protocol assets but do not fabricate missing wake models
Method/problem semantic IDs: t15_iea37_comparison_protocol_v1;
t15_iea37_cs1_three_farms_cs2_cross_model_v1
Controlling contract: shared/contracts/core99_t15_iea37_2019.json
Claim boundary: generated public factual inputs, not participant optimizer or
wake-model implementations
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import yaml


def load_layout(path: Path) -> tuple[list[float], float]:
    document = yaml.safe_load(path.read_text(encoding="utf-8"))
    definitions = document["definitions"]
    position = definitions["position"]["items"]
    coordinates: list[float] = []
    for x, y in zip(position["xc"], position["yc"]):
        coordinates.extend([float(x), float(y)])
    expected = float(
        definitions["plant_energy"]["properties"]
        ["annual_energy_production"]["default"]
    )
    return coordinates, expected


def cpp_array(name: str, values: list[float | int], value_type: str) -> str:
    encoded = ",\n        ".join(
        str(value) if value_type != "double" else f"{float(value):.17g}"
        for value in values
    )
    return (
        f"inline constexpr std::array<{value_type}, {len(values)}> {name} = {{\n"
        f"        {encoded}\n"
        "};\n\n"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--oracle-data-output", type=Path, required=True)
    arguments = parser.parse_args()
    coordinates: list[float] = []
    offsets = [0]
    turbines: list[int] = []
    participants: list[int] = []
    expected: list[float] = []
    identifiers: list[str] = []
    oracle_records: list[dict[str, object]] = []
    for turbine_count in (16, 36, 64):
        paths = [
            arguments.source_root / f"iea37-ex{turbine_count}.yaml",
            *[
                arguments.source_root / "iea37-cs1-results"
                / f"iea37-par{participant}-opt{turbine_count}.yaml"
                for participant in range(1, 11)
            ],
        ]
        for participant, path in enumerate(paths):
            values, aep = load_layout(path)
            identifier = (
                f"t15_example_{turbine_count}"
                if participant == 0
                else f"t15_participant_{participant}_opt{turbine_count}"
            )
            coordinates.extend(values)
            offsets.append(len(coordinates) // 2)
            turbines.append(turbine_count)
            participants.append(participant)
            expected.append(aep)
            identifiers.append(identifier)
            oracle_records.append({
                "coordinates": [
                    [values[index], values[index + 1]]
                    for index in range(0, len(values), 2)
                ],
                "expected_aep_mwh": aep,
                "id": identifier,
                "participant": participant,
                "turbines": turbine_count,
            })
    header = [
        "/*\n",
        "WFLOP IMPLEMENTATION FACT DECLARATION\n",
        "Implementation unit: generated T15 IEA37 case-study arrays\n",
        "Paper DOI: 10.2514/6.2019-0540\n",
        "Public source: https://github.com/byuflowlab/iea37-wflo-casestudies revision af88908d22795030ac2dfbe37bc38e912aee8ed6\n",
        "Missing/conflicts and reconstruction: scripts/generate_core99_t15_data.py\n",
        "Method/problem semantic IDs: t15_iea37_comparison_protocol_v1; t15_iea37_cs1_three_farms_cs2_cross_model_v1\n",
        "Controlling contract: shared/contracts/core99_t15_iea37_2019.json\n",
        "Claim boundary: generated public factual inputs, not participant methods\n",
        "Last evidence-audit date: 2026-07-31\n",
        "END WFLOP IMPLEMENTATION FACT DECLARATION\n",
        "*/\n",
        "#pragma once\n\n",
        "#include <array>\n",
        "#include <string_view>\n\n",
        "namespace core99::t15::data {\n\n",
        cpp_array("kCoordinates", coordinates, "double"),
        cpp_array("kOffsets", offsets, "std::size_t"),
        cpp_array("kTurbines", turbines, "int"),
        cpp_array("kParticipants", participants, "int"),
        cpp_array("kExpectedAepMWh", expected, "double"),
        "inline constexpr std::array<std::string_view, "
        f"{len(identifiers)}> kIdentifiers = {{\n        ",
        ",\n        ".join(f'"{value}"' for value in identifiers),
        "\n};\n\n",
        "}  // namespace core99::t15::data\n",
    ]
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text("".join(header), encoding="utf-8")
    arguments.oracle_data_output.parent.mkdir(parents=True, exist_ok=True)
    arguments.oracle_data_output.write_text(
        json.dumps({
            "paper_doi": "10.2514/6.2019-0540",
            "public_source_revision":
                "af88908d22795030ac2dfbe37bc38e912aee8ed6",
            "records": oracle_records,
            "wind": {
                "directions_deg": [
                    0.0, 22.5, 45.0, 67.5, 90.0, 112.5, 135.0, 157.5,
                    180.0, 202.5, 225.0, 247.5, 270.0, 292.5, 315.0, 337.5
                ],
                "frequencies": [
                    0.025, 0.024, 0.029, 0.036, 0.063, 0.065, 0.100,
                    0.122, 0.063, 0.038, 0.039, 0.083, 0.213, 0.046,
                    0.032, 0.022
                ],
                "speed_mps": 9.8
            }
        }, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
