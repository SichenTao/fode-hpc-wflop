#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T64 command-line, case-matrix, equation,
constraint-mode, and one/all-worker H5 validator
Paper/DOI: The Impact of Land Use Constraints in Multi-Objective
Energy-Noise Wind Farm Layout Optimization; 10.1016/j.renene.2015.06.026
Public source: no target source or native paper arrays were located.
Missing/conflicts/reconstruction:
hpc/core99_cpp/include/core99/sorkhabi_t64.hpp
Independence: this validator checks the frozen paper-role matrix, land
availability, feasible reference evaluations, all published constraint
handling modes, exact physical-evaluation budgets, and schedule-independent
scientific results through the public executable only.
Method/problem semantic IDs: t64_nsga2_three_penalties_declared_reconstruction_v1;
t64_energy_noise_land13role_declared_reconstruction_v1
Controlling contract: shared/contracts/core99_t64_sorkhabi_2016.json
Claim boundary: independent interface and invariant validation of the declared
academic reconstruction, not unavailable native-map or author-front replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path

import validate_core99_t72 as lineage_oracle


def invoke(binary: Path, *arguments: str) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def cubic_power_kw(speed: float) -> float:
    if speed < 4.0 or speed > 25.0:
        return 0.0
    if speed < 15.0:
        return 0.3 * speed**3
    return 1500.0


def t64_paper_physics(
    layout_json: list[list[float]],
    availability: int,
    turbines: int,
) -> dict[str, float]:
    layout = [
        lineage_oracle.Point(float(point[0]), float(point[1]))
        for point in layout_json
    ]
    problem_map = lineage_oracle.Map(availability, turbines)
    states = lineage_oracle.wind_states()
    expected_power = 0.0
    for direction, speed_states in states:
        angle = math.radians(270.0 - direction)
        cosine = math.cos(angle)
        sine = math.sin(angle)
        factors = []
        for downstream, target in enumerate(layout):
            target_along = cosine * target.x + sine * target.y
            target_across = -sine * target.x + cosine * target.y
            squared_deficit = 0.0
            for upstream, source in enumerate(layout):
                if upstream == downstream:
                    continue
                source_along = cosine * source.x + sine * source.y
                distance = target_along - source_along
                if distance <= 0.0:
                    continue
                source_across = -sine * source.x + cosine * source.y
                radius = (
                    lineage_oracle.ROTOR_RADIUS
                    + lineage_oracle.WAKE_EXPANSION * distance
                )
                if abs(target_across - source_across) > radius:
                    continue
                deficit = (
                    lineage_oracle.WAKE_DEFICIT
                    * lineage_oracle.ROTOR_RADIUS**2 / radius**2
                )
                squared_deficit += deficit**2
            factors.append(max(0.0, 1.0 - math.sqrt(squared_deficit)))
        for speed, probability in speed_states:
            expected_power += probability * sum(
                cubic_power_kw(speed * factor) for factor in factors
            )
    aep_gwh = 8760.0 * expected_power / 1.0e6

    band_energy = [
        10.0 ** (0.1 * (100.0 + weighting))
        for weighting in lineage_oracle.A_WEIGHTING
    ]
    maximum_spl = -math.inf
    for receptor in problem_map.receptors:
        acoustic_energy = 0.0
        for source in layout:
            for band, frequency in enumerate(lineage_oracle.FREQUENCIES):
                acoustic_energy += band_energy[band] * 10.0 ** (
                    0.1 * lineage_oracle.transmission(
                        source, receptor, frequency
                    )
                )
        maximum_spl = max(
            maximum_spl,
            10.0 * math.log10(acoustic_energy),
        )
    return {
        "aep_gwh": aep_gwh,
        "maximum_spl_dba": maximum_spl,
    }


def close(left: float, right: float) -> bool:
    return abs(left - right) <= 5.0e-11 * max(
        1.0, abs(left), abs(right)
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()

    listing = invoke(binary, "--mode", "list-cases")
    assert listing["role_count"] == 13
    assert listing["unique_instance_count"] == 12
    assert len(listing["paper_case_roles"]) == 13
    assert len(set(listing["paper_case_roles"])) == 13

    unique_instances: list[tuple[int, int, int]] = [
        (availability, turbines, 0)
        for availability in (70, 80, 90)
        for turbines in (5, 10, 15)
    ]
    unique_instances.extend((80, 10, variant) for variant in (1, 2, 3))
    uniformities: set[float] = set()
    for availability, turbines, variant in unique_instances:
        common = (
            "--land-availability-percent", str(availability),
            "--turbines", str(turbines),
            "--map-variant", str(variant),
        )
        inspection = invoke(binary, "--mode", "inspect", *common)
        assert inspection["turbines"] == turbines
        assert inspection["map_variant"] == variant
        assert abs(
            inspection["measured_land_availability"]
            - availability / 100.0
        ) < 0.02
        assert inspection["forbidden_polygons_and_receptors"] > 0
        evaluation = invoke(binary, "--mode", "evaluate", *common)
        assert evaluation["evaluation"]["feasible"]
        assert evaluation["evaluation"]["aep_gwh"] > 0.0
        assert len(evaluation["layout"]) == turbines
        if availability == 80 and turbines == 10:
            uniformities.add(inspection["uniformity_parameter"])
    assert len(uniformities) == 4

    paper_fixture = invoke(
        binary,
        "--mode", "evaluate",
        "--land-availability-percent", "80",
        "--turbines", "10",
        "--map-variant", "0",
    )
    expected_physics = t64_paper_physics(
        paper_fixture["layout"], 80, 10
    )
    observed_physics = paper_fixture["evaluation"]
    maximum_equation_error = 0.0
    for field in ("aep_gwh", "maximum_spl_dba"):
        maximum_equation_error = max(
            maximum_equation_error,
            abs(observed_physics[field] - expected_physics[field]),
        )
        assert close(
            observed_physics[field], expected_physics[field]
        ), (
            field,
            observed_physics[field],
            expected_physics[field],
        )

    common_run = (
        "--mode", "optimize",
        "--land-availability-percent", "90",
        "--turbines", "5",
        "--map-variant", "0",
        "--physical-fes", "300",
        "--seed", "64016",
    )
    results = {}
    for mode in (
        "static_1e4",
        "static_4e4",
        "dynamic_cgen_ngen",
        "dynamic_cgen_half_ngen",
        "death",
    ):
        result = invoke(
            binary,
            *common_run,
            "--penalty-mode", mode,
            "--workers", "4",
        )
        assert result["penalty_mode"] == mode
        assert result["physical_fes"] == 300
        assert result["observed_workers"] >= 2
        assert result["front"]
        results[mode] = result

    serial = invoke(
        binary,
        *common_run,
        "--penalty-mode", "dynamic_cgen_ngen",
        "--workers", "1",
    )
    parallel = results["dynamic_cgen_ngen"]
    assert serial["scientific_hash"] == parallel["scientific_hash"]
    assert serial["front"] == parallel["front"]

    print(json.dumps({
        "status": "pass",
        "paper_case_roles": 13,
        "unique_problem_instances": 12,
        "constraint_modes": 5,
        "schedule_independent": True,
        "scientific_hash": parallel["scientific_hash"],
        "paper_cubic_power_and_constant_noise_validated": True,
        "maximum_absolute_equation_error": maximum_equation_error,
    }, sort_keys=True))


if __name__ == "__main__":
    main()
