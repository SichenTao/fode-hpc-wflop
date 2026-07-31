#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T85 paper-equation H5 oracle
Paper/DOI: Particle Swarm Optimization of a Wind Farm Layout with Active
Control of Turbine Yaws; 10.1016/j.renene.2023.02.058
Public source, cited predecessor, missing assets, reconstruction decisions,
semantic IDs, and claim boundary:
hpc/core99_cpp/include/core99/song_t85.hpp
Independence: this script re-derives Eqs. (1)--(6) and (11)--(17), the
digitized turbine tables, all six paper cases, and reference-layout AEP
without importing or linking the production C++ implementation. It also
checks a complete schedule-independent AGLDPSO trajectory.
Controlling contract: shared/contracts/core99_t85_song_2023.json
Claim boundary: equation and protocol oracle, not author-result replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess


CASES = ("wf1", "wf1_u6", "wf1_v112", "wf2", "wf3", "wf4")
EXPECTED_IDS = {
    "wf1": "t85_wf1_v80_u8_n25",
    "wf1_u6": "t85_wf1_v80_u6_n25",
    "wf1_v112": "t85_wf1_v112_u8_n25",
    "wf2": "t85_wf2_v80_u8_n25",
    "wf3": "t85_wf3_v80_u8_n36",
    "wf4": "t85_wf4_v80_uneven_n25",
}
METHOD_ID = "t85_agldpso_joint_yaw_declared_reconstruction_v1"
PROBLEM_ID = "t85_song_joint_layout_yaw_six_case_v1"
ROTOR_SAMPLES = [
    (
        math.sqrt((ring + 0.5) / 2.0)
        * math.cos((angle + 0.5 * ring) * math.pi / 2.0),
        math.sqrt((ring + 0.5) / 2.0)
        * math.sin((angle + 0.5 * ring) * math.pi / 2.0),
    )
    for ring in range(2)
    for angle in range(4)
]
V80_POWER = [
    0, 0, 0, 0, .12, .24, .41, .61, .84, 1.10, 1.38, 1.63,
    1.81, 1.92, 1.97, 1.99, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
]
V80_THRUST = [
    0, 0, 0, 0, .80, .82, .80, .82, .81, .80, .79, .78,
    .70, .41, .30, .24, .20, .17, .15, .13, .115, .10,
    .088, .078, .068, .058,
]
V112_POWER = [
    0, 0, 0, 0, .24, .45, .74, 1.08, 1.49, 1.96, 2.43, 2.84,
    2.98, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
]
V112_THRUST = [
    0, 0, 0, 0, .80, .82, .79, .82, .80, .79, .72, .58,
    .39, .29, .23, .19, .16, .14, .12, .105, .090, .078,
    .068, .058, .050, .045,
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def invoke(binary: Path, *arguments: str) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments],
        check=True,
        capture_output=True,
        text=True,
        timeout=900,
    )
    return json.loads(completed.stdout)


def interpolate(values: list[float], speed: float) -> float:
    if speed <= 0:
        return 0.0
    if speed >= len(values) - 1:
        return values[-1]
    lower = math.floor(speed)
    return values[lower] + (speed - lower) * (
        values[lower + 1] - values[lower]
    )


def turbine_curve(
    values: list[float],
    speed: float,
    upper: float,
) -> float:
    if speed < 4.0 or speed > 25.0:
        return 0.0
    return max(0.0, min(upper, interpolate(values, speed)))


def case_data(
    case_name: str,
) -> tuple[float, float, float, list[tuple[float, float, float]]]:
    diameter, hub, rated = (112.0, 84.0, 3.0) if case_name == "wf1_v112" \
        else (80.0, 70.0, 2.0)
    reference_speed = 6.0 if case_name == "wf1_u6" else 8.0
    winds = []
    for direction in range(8):
        speed, probability = reference_speed, 0.125
        if case_name == "wf4":
            if direction == 0:
                speed, probability = 11.0, 0.20
            elif direction in (1, 7):
                speed, probability = 9.0, 0.15
            else:
                speed, probability = 8.0, 0.10
        winds.append((direction * math.pi / 4.0, speed, probability))
    return diameter, hub, rated, winds


def oracle(case_name: str, layout: list[dict]) -> tuple[float, list[float]]:
    diameter, hub, rated, winds = case_data(case_name)
    power_table = V112_POWER if diameter == 112.0 else V80_POWER
    thrust_table = V112_THRUST if diameter == 112.0 else V80_THRUST
    expected_power = 0.0
    contributions = []
    for wind_index, (angle, reference_speed, probability) in enumerate(winds):
        cosine, sine = math.cos(angle), math.sin(angle)
        along = [
            cosine * turbine["x_m"] + sine * turbine["y_m"]
            for turbine in layout
        ]
        across = [
            -sine * turbine["x_m"] + cosine * turbine["y_m"]
            for turbine in layout
        ]
        order = sorted(range(len(layout)), key=lambda i: (along[i], i))
        inflow = [0.0] * len(layout)
        state_power = 0.0
        for downstream_position, downstream in enumerate(order):
            rotor_sum = 0.0
            for sample_y, sample_z in ROTOR_SAMPLES:
                sample_cross = across[downstream] + diameter * sample_y / 2
                sample_height = hub + diameter * sample_z / 2
                free_speed = reference_speed * (
                    max(1.0, sample_height) / 25.0
                ) ** 0.1
                deficit_speed = 0.0
                for upstream in order[:downstream_position]:
                    distance = along[downstream] - along[upstream]
                    gamma = math.radians(
                        layout[upstream]["yaw_deg"][wind_index]
                    )
                    cos_gamma = math.cos(gamma)
                    ct = turbine_curve(
                        thrust_table, inflow[upstream], 0.95
                    )
                    if distance <= 0 or cos_gamma <= 0 or ct <= 0:
                        continue
                    ct_star = ct * cos_gamma**2
                    beta_root = math.sqrt(
                        max(1.0e-12, 1.0 - ct_star * cos_gamma)
                    )
                    beta = (1.0 + beta_root) / (2.0 * beta_root)
                    sigma_yaw = (
                        0.0125 * distance / (diameter * cos_gamma)
                        + math.sqrt(beta) / 5.0
                    )
                    sigma_z = (
                        0.0125 * distance / diameter
                        + math.sqrt(beta) / 5.0
                    )
                    yaw_term = ct * math.sin(gamma)
                    signed = math.copysign(abs(yaw_term) ** 0.75, yaw_term) \
                        if yaw_term else 0.0
                    offset = (
                        diameter * 0.607 * ct * signed
                        * cos_gamma**1.5 * math.sqrt(distance / diameter)
                    )
                    vertical = sample_height - hub
                    offset_at_height = offset * math.exp(
                        -0.5 * vertical**2 / (diameter**2 * sigma_z**2)
                    )
                    cross = (
                        sample_cross - across[upstream] - offset_at_height
                    )
                    exponent = (
                        -0.5 * cross**2
                        / (diameter**2 * cos_gamma**2 * sigma_yaw**2)
                        -0.5 * vertical**2 / (diameter**2 * sigma_z**2)
                    )
                    radical = max(
                        0.0,
                        min(
                            1.0,
                            1.0 - ct_star * cos_gamma
                            / (8.0 * sigma_yaw * sigma_z),
                        ),
                    )
                    fraction = (
                        1.0 - math.sqrt(radical)
                    ) * math.exp(exponent)
                    deficit_speed += max(0.0, inflow[upstream]) * fraction
                rotor_sum += max(0.0, free_speed - deficit_speed)
            inflow[downstream] = rotor_sum / len(ROTOR_SAMPLES)
            yaw = math.radians(layout[downstream]["yaw_deg"][wind_index])
            state_power += turbine_curve(
                power_table, inflow[downstream], rated
            ) * max(0.0, math.cos(yaw))
        expected_power += probability * state_power
        contributions.append(8.76 * probability * state_power)
    return 8.76 * expected_power, contributions


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    arguments = parser.parse_args()

    case_receipts = {}
    for case_name in CASES:
        inspected = invoke(
            arguments.binary, "--mode", "inspect", "--case", case_name
        )
        require(
            inspected["problem_id"] == EXPECTED_IDS[case_name],
            f"{case_name}: problem ID mismatch",
        )
        require(inspected["wind_state_count"] == 8, "wind count mismatch")
        require(
            inspected["decision_dimension"]
            == inspected["turbine_count"] * 10,
            f"{case_name}: joint dimension mismatch",
        )
        require(
            inspected["declared_population"] == 500
            and inspected["declared_physical_fes"] == 10000
            and inspected["declared_repeats"] == 25,
            f"{case_name}: declared completion profile mismatch",
        )
        evaluated = invoke(
            arguments.binary, "--mode", "evaluate", "--case", case_name
        )
        expected, expected_contributions = oracle(
            case_name, evaluated["layout"]
        )
        observed = evaluated["evaluation"]["aep_gwh"]
        require(evaluated["evaluation"]["feasible"], "reference infeasible")
        require(
            math.isclose(observed, expected, rel_tol=2e-12, abs_tol=2e-10),
            f"{case_name}: equation oracle mismatch {observed} vs {expected}",
        )
        for wind, (observed_part, expected_part) in enumerate(zip(
            evaluated["evaluation"]["wind_aep_contribution_gwh"],
            expected_contributions,
        )):
            require(
                math.isclose(
                    observed_part,
                    expected_part,
                    rel_tol=2e-12,
                    abs_tol=2e-10,
                ),
                f"{case_name}: wind {wind} oracle mismatch "
                f"{observed_part} vs {expected_part}",
            )
        case_receipts[case_name] = {
            "reference_aep_gwh": observed,
            "independent_oracle_aep_gwh": expected,
        }

    common = [
        "--mode", "optimize",
        "--case", "wf1",
        "--population", "100",
        "--physical-fes-limit", "400",
        "--seed", "20260731",
    ]
    serial = invoke(arguments.binary, *common, "--workers", "1")
    parallel = invoke(arguments.binary, *common, "--workers", "20")
    require(
        serial["scientific_hash"] == parallel["scientific_hash"],
        "T85 one/all-core scientific trajectory mismatch",
    )
    require(
        serial["best_aep_gwh"] == parallel["best_aep_gwh"],
        "T85 one/all-core objective mismatch",
    )
    require(
        serial["physical_fes"] == parallel["physical_fes"] == 400,
        "T85 smoke physical-work mismatch",
    )
    require(
        parallel["observed_workers"] == 20,
        "T85 all-core activation mismatch",
    )

    paper_profile = invoke(
        arguments.binary,
        "--mode", "optimize",
        "--case", "wf1",
        "--workers", "20",
        "--population", "500",
        "--physical-fes-limit", "10000",
        "--seed", "20260731",
    )
    require(
        paper_profile["problem_semantic_id"] == PROBLEM_ID
        and paper_profile["method_semantic_id"] == METHOD_ID,
        "T85 semantic identity mismatch",
    )
    require(
        paper_profile["best_aep_gwh"]
        >= paper_profile["initial_best_aep_gwh"],
        "T85 retained-best invariant failed",
    )
    require(
        150.0 <= paper_profile["best_aep_gwh"] <= 200.0,
        "T85 WF1 declared reconstruction outside paper-scale AEP range",
    )

    receipt = {
        "status": "pass",
        "case_oracles": case_receipts,
        "parallel_equivalence": {
            "physical_fes": 400,
            "serial_hash": serial["scientific_hash"],
            "parallel_hash": parallel["scientific_hash"],
            "observed_parallel_workers": parallel["observed_workers"],
        },
        "wf1_declared_profile": {
            "physical_fes": paper_profile["physical_fes"],
            "best_aep_gwh": paper_profile["best_aep_gwh"],
            "paper_reported_joint_agld_aep_gwh": 174.74,
            "claim_boundary": (
                "scale anchor only; reconstruction is not author-result replay"
            ),
        },
    }
    print(json.dumps(receipt, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
